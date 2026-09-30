#include "PluginEditor.h"

namespace Colours
{
    const juce::Colour background { 0xff1b1c1f };
    const juce::Colour panel      { 0xff26272b };
    const juce::Colour accent     { 0xffe8773a };
    const juce::Colour text       { 0xffe6e6e6 };
    const juce::Colour dimText    { 0xff9a9ba0 };
}

//==============================================================================
void VinylDisplay::timerCallback()
{
    const float rate = processor.getCurrentRate();
    shownRate = rate;
    // 33 1/3 rpm a 60 fps -> vuelta completa cada 1,8 s.
    angle += rate * juce::MathConstants<float>::twoPi * (33.333f / 60.0f) / 60.0f;
    if (angle > juce::MathConstants<float>::twoPi)
        angle -= juce::MathConstants<float>::twoPi;
    repaint();
}

void VinylDisplay::paint (juce::Graphics& g)
{
    auto area   = getLocalBounds().toFloat().reduced (6.0f);
    const auto size = juce::jmin (area.getWidth(), area.getHeight());
    auto disc   = area.withSizeKeepingCentre (size, size);
    const auto c = disc.getCentre();
    const float r = size * 0.5f;

    // Plato.
    g.setColour (juce::Colour (0xff3a3b40));
    g.fillEllipse (disc.expanded (2.0f));

    // Disco con surcos.
    g.setColour (juce::Colour (0xff0d0d0f));
    g.fillEllipse (disc);
    for (float k = 0.40f; k < 0.97f; k += 0.035f)
    {
        g.setColour (juce::Colour (0xff1c1c20).withAlpha (0.8f));
        g.drawEllipse (juce::Rectangle<float> (r * 2 * k, r * 2 * k).withCentre (c), 0.8f);
    }

    // Brillo que gira con el disco (así se ve la velocidad).
    const auto rot = juce::AffineTransform::rotation (angle, c.x, c.y);
    juce::Path shine;
    shine.addPieSegment (disc, -0.25f, 0.25f, 0.38f);
    shine.addPieSegment (disc, juce::MathConstants<float>::pi - 0.25f, juce::MathConstants<float>::pi + 0.25f, 0.38f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillPath (shine, rot);

    // Etiqueta central.
    auto label = juce::Rectangle<float> (r * 0.72f, r * 0.72f).withCentre (c);
    g.setColour (Colours::accent);
    g.fillEllipse (label);
    juce::Path mark;
    mark.addRectangle (c.x - 1.5f, label.getY() + 4.0f, 3.0f, r * 0.2f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillPath (mark, rot);
    g.setColour (Colours::background);
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (c));

    // Velocidad en texto.
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::String (juce::roundToInt (shownRate * 100.0f)) + " %",
                disc.withTrimmedTop (size * 0.62f).withHeight (18.0f), juce::Justification::centred);

    // Diagnóstico: cuántas veces ha reiniciado el host el procesado.
    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("Reinicios del host: " + juce::String (processor.hostResets.load()),
                getLocalBounds().removeFromBottom (12), juce::Justification::centredRight);
}

//==============================================================================
TocadiscosStopEditor::TocadiscosStopEditor (TocadiscosStopProcessor& p)
    : AudioProcessorEditor (&p), processor (p), vinyl (p),
      engageParam (*p.apvts.getParameter (ParamIDs::engage))
{
    lnf.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    lnf.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3d3e44));
    lnf.setColour (juce::Slider::thumbColourId, Colours::text);
    lnf.setColour (juce::Slider::textBoxTextColourId, Colours::text);
    lnf.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lnf.setColour (juce::Label::textColourId, Colours::dimText);
    lnf.setColour (juce::TextButton::buttonColourId, Colours::panel);
    lnf.setColour (juce::TextButton::buttonOnColourId, Colours::accent);
    lnf.setColour (juce::TextButton::textColourOffId, Colours::text);
    lnf.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setLookAndFeel (&lnf);

    addAndMakeVisible (vinyl);

    stopButton.setClickingTogglesState (false);
    stopButton.setTooltip ("Activa para que el disco frene; desactiva para que vuelva a girar. Se puede automatizar.");
    stopButton.onClick = [this] { stopButtonClicked(); };
    addAndMakeVisible (stopButton);
    timerCallback();
    startTimerHz (15);

    setupKnob (stopTime,  ParamIDs::stopTime,  "Frenado");
    setupKnob (curve,     ParamIDs::curve,     "Curva");
    setupKnob (startTime, ParamIDs::startTime, "Arranque");
    setupKnob (tone,      ParamIDs::tone,      "Oscurecer");
    setupKnob (fade,      ParamIDs::fade,      "Desvanecer");

    setSize (620, 340);
}

void TocadiscosStopEditor::stopButtonClicked()
{
    // Si "Parar" quedó encendido de una reproducción anterior, un clic vuelve
    // a lanzar el frenado sin tocar el parámetro (ni la automatización).
    if (processor.isWaitingForRetrigger())
    {
        processor.requestRetrigger();
        return;
    }

    const bool on = engageParam.getValue() > 0.5f;
    engageParam.beginChangeGesture();
    engageParam.setValueNotifyingHost (on ? 0.0f : 1.0f);
    engageParam.endChangeGesture();
}

void TocadiscosStopEditor::timerCallback()
{
    const bool waiting = processor.isWaitingForRetrigger();
    const bool on      = engageParam.getValue() > 0.5f;

    stopButton.setToggleState (on && ! waiting, juce::dontSendNotification);
    stopButton.setButtonText (waiting ? "REPETIR" : "PARAR");
    stopButton.setTooltip (waiting ? "PARAR sigue activado de la reproduccion anterior. Pulsa para volver a frenar."
                                   : "Activa para que el disco frene; desactiva para que vuelva a girar. Se puede automatizar.");
}

TocadiscosStopEditor::~TocadiscosStopEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TocadiscosStopEditor::setupKnob (Knob& k, const char* paramID, const juce::String& text)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 18);
    addAndMakeVisible (k.slider);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramID, k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k.label);
}

void TocadiscosStopEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("TOCADISCOS STOP", 20, 12, 300, 26, juce::Justification::centredLeft);
    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Elmio", getWidth() - 120, 12, 100, 26, juce::Justification::centredRight);

    g.setColour (Colours::panel);
    g.fillRoundedRectangle (juce::Rectangle<float> (270.0f, 50.0f, (float) getWidth() - 290.0f, (float) getHeight() - 70.0f), 8.0f);
}

void TocadiscosStopEditor::resized()
{
    vinyl.setBounds (14, 48, 240, 220);
    stopButton.setBounds (44, 280, 180, 40);

    auto panel = juce::Rectangle<int> (270, 50, getWidth() - 290, getHeight() - 70).reduced (8);
    auto top    = panel.removeFromTop (panel.getHeight() / 2);
    auto bottom = panel;

    auto place = [] (Knob& k, juce::Rectangle<int> r)
    {
        k.label.setBounds (r.removeFromTop (18));
        k.slider.setBounds (r);
    };

    const int w3 = top.getWidth() / 3;
    place (stopTime,  top.removeFromLeft (w3));
    place (curve,     top.removeFromLeft (w3));
    place (startTime, top);

    const int w2 = bottom.getWidth() / 2;
    place (tone, bottom.removeFromLeft (w2).reduced (w2 / 6, 0));
    place (fade, bottom.reduced (w2 / 6, 0));
}

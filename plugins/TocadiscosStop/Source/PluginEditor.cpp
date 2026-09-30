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
}

//==============================================================================
TocadiscosStopEditor::TocadiscosStopEditor (TocadiscosStopProcessor& p)
    : AudioProcessorEditor (&p), processor (p), vinyl (p)
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
    lnf.setColour (juce::TextEditor::backgroundColourId, Colours::panel);
    lnf.setColour (juce::TextEditor::textColourId, Colours::text);
    lnf.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff3d3e44));
    lnf.setColour (juce::TextEditor::focusedOutlineColourId, Colours::accent);
    setLookAndFeel (&lnf);

    addAndMakeVisible (vinyl);

    markButton.setColour (juce::TextButton::buttonColourId, Colours::accent);
    markButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    markButton.setTooltip (juce::String::fromUTF8 ("Pulsa mientras reproduces: el disco frenar\xc3\xa1 en ese punto cada vez que pase por ah\xc3\xad."));
    markButton.onClick = [this] { markHere(); };
    addAndMakeVisible (markButton);

    pointLabel.setText ("Punto de parada", juce::dontSendNotification);
    addAndMakeVisible (pointLabel);

    timecodeEditor.setJustification (juce::Justification::centred);
    timecodeEditor.setFont (juce::FontOptions (16.0f));
    timecodeEditor.setTooltip ("Escribe el codigo de tiempo (HH:MM:SS:FF) y pulsa Intro.");
    timecodeEditor.onReturnKey = [this] { applyTypedTimecode(); };
    timecodeEditor.onFocusLost = [this] { applyTypedTimecode(); };
    timecodeEditor.onEscapeKey = [this] { shownStopPosition = -2; refreshTimecode(); };
    addAndMakeVisible (timecodeEditor);

    minusButton.setTooltip ("Un fotograma antes");
    plusButton.setTooltip  ("Un fotograma despues");
    clearButton.setTooltip ("Quitar el punto de parada (la musica suena normal)");
    minusButton.onClick = [this] { nudge (-1); };
    plusButton.onClick  = [this] { nudge (+1); };
    clearButton.onClick = [this] { processor.setStopPosition (-1); };
    addAndMakeVisible (minusButton);
    addAndMakeVisible (plusButton);
    addAndMakeVisible (clearButton);

    hintLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (hintLabel);

    setupKnob (stopTime, ParamIDs::stopTime, "Frenado");
    setupKnob (curve,    ParamIDs::curve,    "Curva");
    setupKnob (tone,     ParamIDs::tone,     "Oscurecer");
    setupKnob (fade,     ParamIDs::fade,     "Desvanecer");

    setSize (620, 420);
    timerCallback();
    startTimerHz (10);
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

void TocadiscosStopEditor::markHere()
{
    const auto pos = processor.getLastPosition();
    if (pos >= 0)
        processor.setStopPosition (pos);
}

void TocadiscosStopEditor::nudge (int frames)
{
    const auto current = processor.getStopPosition();
    if (current < 0)
        return;
    const auto step = Timecode::frameLength (processor.getHostSampleRate(), processor.getHostFrameRate());
    processor.setStopPosition (juce::jmax ((int64_t) 0, current + frames * step));
}

void TocadiscosStopEditor::applyTypedTimecode()
{
    const auto text = timecodeEditor.getText().trim();
    if (text.isEmpty())
    {
        processor.setStopPosition (-1);
        return;
    }

    const auto samples = Timecode::parse (text.toStdString(), processor.getHostSampleRate(), processor.getHostFrameRate());
    if (samples >= 0)
        processor.setStopPosition (samples);

    shownStopPosition = -2; // volver a mostrar el valor válido
    refreshTimecode();
}

void TocadiscosStopEditor::refreshTimecode()
{
    const auto pos = processor.getStopPosition();
    if (pos == shownStopPosition || timecodeEditor.hasKeyboardFocus (true))
        return;

    shownStopPosition = pos;
    timecodeEditor.setText (pos >= 0 ? juce::String (Timecode::format (pos, processor.getHostSampleRate(), processor.getHostFrameRate()))
                                     : juce::String(),
                            juce::dontSendNotification);
}

void TocadiscosStopEditor::timerCallback()
{
    const bool hasPoint   = processor.getStopPosition() >= 0;
    const bool hasPlayed  = processor.getLastPosition() >= 0;
    const bool knowsRate  = processor.getHostSampleRate() > 0.0;

    markButton.setEnabled (hasPlayed);
    timecodeEditor.setEnabled (knowsRate);
    minusButton.setEnabled (hasPoint);
    plusButton.setEnabled (hasPoint);
    clearButton.setEnabled (hasPoint);

    if (! hasPlayed)
        hintLabel.setText (juce::String::fromUTF8 ("Reproduce la l\xc3\xadnea de tiempo y pulsa PARAR AQU\xc3\x8d en el momento en que quieras que frene."),
                           juce::dontSendNotification);
    else if (! hasPoint)
        hintLabel.setText (juce::String::fromUTF8 ("Sin punto de parada: la m\xc3\xbasica suena normal. Pulsa PARAR AQU\xc3\x8d mientras reproduces."),
                           juce::dontSendNotification);
    else
        hintLabel.setText (juce::String::fromUTF8 ("El disco frena en ese punto en cada reproducci\xc3\xb3n y en el render. Aj\xc3\xbastalo con -1 / +1 o escribi\xc3\xa9ndolo."),
                           juce::dontSendNotification);

    refreshTimecode();
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
    g.fillRoundedRectangle (juce::Rectangle<float> (270.0f, 50.0f, (float) getWidth() - 290.0f, 250.0f), 8.0f);
    g.fillRoundedRectangle (juce::Rectangle<float> (14.0f, 312.0f, (float) getWidth() - 34.0f, 80.0f), 8.0f);
}

void TocadiscosStopEditor::resized()
{
    vinyl.setBounds (14, 48, 240, 200);
    markButton.setBounds (34, 256, 200, 42);

    auto panel = juce::Rectangle<int> (270, 50, getWidth() - 290, 250).reduced (8);
    auto top    = panel.removeFromTop (panel.getHeight() / 2);
    auto bottom = panel;

    auto place = [] (Knob& k, juce::Rectangle<int> r)
    {
        k.label.setBounds (r.removeFromTop (18));
        k.slider.setBounds (r);
    };

    const int w2 = top.getWidth() / 2;
    place (stopTime, top.removeFromLeft (w2));
    place (curve,    top);
    place (tone,     bottom.removeFromLeft (w2));
    place (fade,     bottom);

    auto row = juce::Rectangle<int> (26, 322, getWidth() - 58, 30);
    pointLabel.setBounds (row.removeFromLeft (120));
    timecodeEditor.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (10);
    minusButton.setBounds (row.removeFromLeft (44));
    row.removeFromLeft (6);
    plusButton.setBounds (row.removeFromLeft (44));
    row.removeFromLeft (10);
    clearButton.setBounds (row.removeFromLeft (80));

    hintLabel.setBounds (26, 356, getWidth() - 58, 30);
}

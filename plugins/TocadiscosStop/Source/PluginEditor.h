#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Timecode.h"

// Disco de vinilo que gira a la velocidad real del motor.
class VinylDisplay : public juce::Component, private juce::Timer
{
public:
    explicit VinylDisplay (TocadiscosStopProcessor& p) : processor (p) { startTimerHz (60); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    TocadiscosStopProcessor& processor;
    float angle = 0.0f;
    float shownRate = 1.0f;
};

class TocadiscosStopEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TocadiscosStopEditor (TocadiscosStopProcessor&);
    ~TocadiscosStopEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void setupKnob (Knob&, const char* paramID, const juce::String& text);
    void timerCallback() override;

    void markHere();
    void nudge (int frames);
    void applyTypedTimecode();
    void refreshTimecode();

    TocadiscosStopProcessor& processor;
    juce::LookAndFeel_V4 lnf;

    VinylDisplay vinyl;
    juce::TextButton markButton  { juce::String::fromUTF8 ("PARAR AQU\xc3\x8d") };
    juce::Label      pointLabel;
    juce::TextEditor timecodeEditor;
    juce::TextButton minusButton { "-1" };
    juce::TextButton plusButton  { "+1" };
    juce::TextButton clearButton { "Quitar" };
    juce::Label      hintLabel;

    Knob stopTime, curve, tone, fade;

    int64_t shownStopPosition = -2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TocadiscosStopEditor)
};

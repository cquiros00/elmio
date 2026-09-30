#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "TurntableEngine.h"

namespace ParamIDs
{
    inline constexpr const char* engage    = "engage";
    inline constexpr const char* stopTime  = "stopTime";
    inline constexpr const char* curve     = "curve";
    inline constexpr const char* startTime = "startTime";
    inline constexpr const char* tone      = "tone";
    inline constexpr const char* fade      = "fade";
}

class TocadiscosStopProcessor : public juce::AudioProcessor
{
public:
    TocadiscosStopProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    float getCurrentRate() const noexcept { return engine.currentRate.load (std::memory_order_relaxed); }

    // "Parar" sigue encendido de una reproducción anterior y el disco gira:
    // la interfaz muestra REPETIR y requestRetrigger() vuelve a frenar.
    bool isWaitingForRetrigger() const noexcept { return waitingForRetrigger.load (std::memory_order_relaxed); }
    void requestRetrigger() noexcept            { retriggerRequested.store (true); }

    // Veces que el host ha reiniciado el procesado (se muestra en la interfaz para diagnosticar).
    std::atomic<int> hostResets { 0 };

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    TurntableEngine engine;

    std::atomic<float>* engageParam    = nullptr;
    std::atomic<float>* stopTimeParam  = nullptr;
    std::atomic<float>* curveParam     = nullptr;
    std::atomic<float>* startTimeParam = nullptr;
    std::atomic<float>* toneParam      = nullptr;
    std::atomic<float>* fadeParam      = nullptr;

    double  lastBlockMs      = -1.0;
    double  lastPlayingMs    = -1.0;
    int64_t lastSamplePos    = -1;
    bool    waitForRetrigger = false;
    std::atomic<bool> waitingForRetrigger { false };
    std::atomic<bool> retriggerRequested  { false };

    double preparedSampleRate = 0.0;
    int    preparedChannels   = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TocadiscosStopProcessor)
};

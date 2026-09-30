#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "TurntableEngine.h"
#include "DiagnosticLog.h"

namespace ParamIDs
{
    inline constexpr const char* stopTime  = "stopTime";
    inline constexpr const char* curve     = "curve";
    inline constexpr const char* tone      = "tone";
    inline constexpr const char* fade      = "fade";
}

// Propiedad (no automatizable) que guarda el punto de parada en el proyecto.
inline constexpr const char* stopPositionProperty = "stopPosition";

class TocadiscosStopProcessor : public juce::AudioProcessor
{
public:
    TocadiscosStopProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
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

    // Punto de parada en la línea de tiempo, en muestras (-1 = sin punto).
    // Llamar desde el hilo de mensajes.
    void    setStopPosition (int64_t samples);
    int64_t getStopPosition() const noexcept { return stopPosition.load(); }

    // Última posición que ha procesado el host (-1 = todavía no ha reproducido).
    int64_t getLastPosition() const noexcept { return lastPosition.load(); }
    // Frecuencia de muestreo y fotogramas por segundo que informa el host (0 = desconocido).
    double  getHostSampleRate() const noexcept { return hostSampleRate.load(); }
    double  getHostFrameRate() const noexcept  { return hostFrameRate.load(); }

    // Veces que el host ha reiniciado el procesado (se muestra en la interfaz para diagnosticar).
    std::atomic<int> hostResets { 0 };

    // Registro de diagnóstico (archivo en Documentos).
    DiagnosticLog diagnostics;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    TurntableEngine engine;

    std::atomic<float>* stopTimeParam  = nullptr;
    std::atomic<float>* curveParam     = nullptr;
    std::atomic<float>* toneParam      = nullptr;
    std::atomic<float>* fadeParam      = nullptr;

    std::atomic<int64_t> stopPosition   { -1 };
    std::atomic<int64_t> lastPosition   { -1 };
    std::atomic<double>  hostSampleRate { 0.0 };
    std::atomic<double>  hostFrameRate  { 0.0 };

    // Posición propia para hosts que no informan de la posición.
    int64_t fallbackPosition = 0;

    double preparedSampleRate = 0.0;
    int    preparedChannels   = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TocadiscosStopProcessor)
};

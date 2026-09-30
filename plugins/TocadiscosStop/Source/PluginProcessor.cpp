#include "PluginProcessor.h"
#include "PluginEditor.h"

TocadiscosStopProcessor::TocadiscosStopProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Entrada", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Salida",  juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TocadiscosStop", createLayout())
{
    stopTimeParam  = apvts.getRawParameterValue (ParamIDs::stopTime);
    curveParam     = apvts.getRawParameterValue (ParamIDs::curve);
    toneParam      = apvts.getRawParameterValue (ParamIDs::tone);
    fadeParam      = apvts.getRawParameterValue (ParamIDs::fade);
}

juce::AudioProcessorValueTreeState::ParameterLayout TocadiscosStopProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto seconds = [] (float v, int) { return String (v, 2) + " s"; };
    auto percent = [] (float v, int) { return String (juce::roundToInt (v * 100.0f)) + " %"; };

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::stopTime, 1 }, "Tiempo de frenado",
        NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.5f), 2.5f,
        AudioParameterFloatAttributes().withLabel ("s").withStringFromValueFunction (seconds)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::curve, 1 }, "Curva",
        NormalisableRange<float> (0.25f, 4.0f, 0.01f, 0.5f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            if (v < 0.9f) return String (v, 2) + " (plato pesado)";
            if (v > 1.1f) return String (v, 2) + " (freno)";
            return String (v, 2) + " (natural)";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::tone, 1 }, "Oscurecer",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.4f,
        AudioParameterFloatAttributes().withStringFromValueFunction (percent)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::fade, 1 }, "Desvanecer",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction (percent)));

    return layout;
}

bool TocadiscosStopProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void TocadiscosStopProcessor::prepareToPlay (double sampleRate, int)
{
    // Algunos hosts vuelven a llamar a prepareToPlay en mitad de la
    // reproducción (por ejemplo al cambiar un parámetro). Si la configuración
    // no ha cambiado no se toca el motor, para no cortar un frenado en curso.
    const int channels = getTotalNumOutputChannels();
    if (std::abs (sampleRate - preparedSampleRate) < 1.0e-6 && channels == preparedChannels)
        return;

    engine.prepare (sampleRate, channels);
    preparedSampleRate = sampleRate;
    preparedChannels   = channels;
}

void TocadiscosStopProcessor::reset()
{
    // A propósito no se reinicia el motor: el efecto depende solo de la
    // posición en la línea de tiempo (ver TurntableEngine).
}

void TocadiscosStopProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();

    // Posición de este bloque en la línea de tiempo. DaVinci Resolve la
    // informa con exactitud; si un host no lo hace, se usa un contador propio.
    int64_t blockPos = fallbackPosition;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto samplePos = pos->getTimeInSamples())
                blockPos = *samplePos;
            if (auto fps = pos->getFrameRate())
                hostFrameRate.store (fps->getEffectiveRate());
        }
    }
    fallbackPosition = blockPos + numSamples;
    lastPosition.store (blockPos + numSamples);
    hostSampleRate.store (getSampleRate());

    TurntableEngine::Params p;
    p.stopSeconds = stopTimeParam->load();
    p.curve       = curveParam->load();
    p.tone        = toneParam->load();
    p.fade        = fadeParam->load();

    const int64_t stopPos = stopPosition.load();
    engine.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), numSamples, blockPos, stopPos, p);
}

void TocadiscosStopProcessor::setStopPosition (int64_t samples)
{
    stopPosition.store (samples);
    apvts.state.setProperty (stopPositionProperty, juce::String ((juce::int64) samples), nullptr);
}

juce::AudioProcessorEditor* TocadiscosStopProcessor::createEditor()
{
    return new TocadiscosStopEditor (*this);
}

void TocadiscosStopProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void TocadiscosStopProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            const auto saved = apvts.state.getProperty (stopPositionProperty, "-1").toString();
            stopPosition.store (saved.getLargeIntValue());
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TocadiscosStopProcessor();
}

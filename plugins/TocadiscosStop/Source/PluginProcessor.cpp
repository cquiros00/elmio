#include "PluginProcessor.h"
#include "PluginEditor.h"

TocadiscosStopProcessor::TocadiscosStopProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Entrada", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Salida",  juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TocadiscosStop", createLayout())
{
    engageParam    = apvts.getRawParameterValue (ParamIDs::engage);
    stopTimeParam  = apvts.getRawParameterValue (ParamIDs::stopTime);
    curveParam     = apvts.getRawParameterValue (ParamIDs::curve);
    startTimeParam = apvts.getRawParameterValue (ParamIDs::startTime);
    toneParam      = apvts.getRawParameterValue (ParamIDs::tone);
    fadeParam      = apvts.getRawParameterValue (ParamIDs::fade);
}

juce::AudioProcessorValueTreeState::ParameterLayout TocadiscosStopProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto seconds = [] (float v, int) { return String (v, 2) + " s"; };
    auto percent = [] (float v, int) { return String (juce::roundToInt (v * 100.0f)) + " %"; };

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::engage, 1 }, "Parar", false));

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

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::startTime, 1 }, "Tiempo de arranque",
        NormalisableRange<float> (0.0f, 5.0f, 0.01f, 0.5f), 0.6f,
        AudioParameterFloatAttributes().withLabel ("s").withStringFromValueFunction ([] (float v, int)
        {
            return v < 0.001f ? String ("Instantaneo") : String (v, 2) + " s";
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
    hostResets.fetch_add (1, std::memory_order_relaxed);

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
    // A propósito no se reinicia el motor: ver prepareToPlay.
    hostResets.fetch_add (1, std::memory_order_relaxed);
}

void TocadiscosStopProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const bool engaged = engageParam->load() > 0.5f;

    // Cada vez que empieza una reproducción nueva el disco vuelve a girar, para
    // poder revisar el efecto las veces que haga falta. Se considera
    // reproducción nueva: el primer bloque, una pausa de más de 400 ms sin
    // audio o sin transporte en marcha, o un salto hacia atrás del cabezal.
    // Nunca se salta directamente al estado "parado": el frenado siempre se
    // oye entero.
    const double now = juce::Time::getMillisecondCounterHiRes();
    bool newPlayback = lastBlockMs < 0.0 || now - lastBlockMs > 400.0;
    lastBlockMs = now;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (pos->getIsPlaying())
            {
                if (lastPlayingMs >= 0.0 && now - lastPlayingMs > 400.0)
                    newPlayback = true;
                lastPlayingMs = now;
            }

            if (auto samplePos = pos->getTimeInSamples())
            {
                if (lastSamplePos >= 0 && *samplePos < lastSamplePos - (int64_t) (0.5 * getSampleRate()))
                    newPlayback = true;
                lastSamplePos = *samplePos;
            }
        }
    }

    if (newPlayback)
    {
        engine.snapTo (false);
        // Si "Parar" sigue encendido de la vez anterior, no se vuelve a frenar
        // hasta que se apague o se pulse REPETIR en la interfaz.
        waitForRetrigger = engaged;
    }

    const bool retrigger = retriggerRequested.exchange (false);
    if (! engaged || retrigger)
        waitForRetrigger = false;

    waitingForRetrigger.store (waitForRetrigger, std::memory_order_relaxed);
    const bool engineEngaged = engaged && ! waitForRetrigger;

    TurntableEngine::Params p;
    p.stopSeconds  = stopTimeParam->load();
    p.curve        = curveParam->load();
    p.startSeconds = startTimeParam->load();
    p.tone         = toneParam->load();
    p.fade         = fadeParam->load();

    engine.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(), engineEngaged, p);
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
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TocadiscosStopProcessor();
}

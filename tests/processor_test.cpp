// Prueba del plugin completo con un "host" simulado.
//
// El frenado tiene que oírse entero pase lo que pase en el host: posición del
// cabezal informada a saltos, reinicios del procesado (prepareToPlay/reset)
// justo al pulsar "Parar", o el estado de reproducción parpadeando.

#include "PluginProcessor.h"

#include <cstdio>

namespace
{
struct FakePlayHead : juce::AudioPlayHead
{
    int64_t reportedSamples = 0;
    bool    playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setTimeInSamples (reportedSamples);
        return info;
    }
};

struct Result { double energyAfterEngage = 0.0; double energyWhenStopped = 0.0; };

// Simula `seconds` de reproducción. `positionStep` = cada cuántas muestras
// actualiza el host la posición que informa (1 = exacta).
struct HostQuirks { int positionStep = 1; bool resetOnEngage = false; bool flickerPlaying = false; };

Result run (HostQuirks q, double engageAt, double seconds)
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    TocadiscosStopProcessor proc;
    FakePlayHead head;
    proc.setPlayHead (&head);
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);

    auto* engage = proc.apvts.getParameter (ParamIDs::engage);
    auto* stopT  = proc.apvts.getParameter (ParamIDs::stopTime);
    stopT->setValueNotifyingHost (stopT->convertTo0to1 (1.5f));

    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    Result r;
    int64_t pos = 0;
    int64_t timeline = 0;
    const int64_t total = (int64_t) (seconds * sr);

    for (; pos < total; pos += block)
    {
        head.reportedSamples = (timeline / q.positionStep) * q.positionStep;
        head.playing = ! (q.flickerPlaying && (pos / block) % 7 == 3);

        const double t = (double) pos / sr;
        const bool engageNow = t >= engageAt;
        const bool justEngaged = engageNow && engage->getValue() < 0.5f;
        engage->setValueNotifyingHost (engageNow ? 1.0f : 0.0f);

        if (justEngaged && q.resetOnEngage)
        {
            proc.releaseResources();
            proc.prepareToPlay (sr, block);
            proc.reset();
        }

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < block; ++i)
                buf.setSample (c, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (double) (pos + i) / sr));

        proc.processBlock (buf, midi);

        double e = 0.0;
        for (int i = 0; i < block; ++i)
            e += std::abs (buf.getSample (0, i));
        e /= block;

        if (t >= engageAt && t < engageAt + 1.0)  r.energyAfterEngage += e;
        if (t >= engageAt + 2.0)                  r.energyWhenStopped += e;

        timeline += block;
    }
    return r;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    bool ok = true;

    const auto normal = run ({}, 1.0, 4.0);
    std::printf ("%-40s tras PARAR: %6.2f  ya parado: %.4f\n", "Host exacto", normal.energyAfterEngage, normal.energyWhenStopped);
    ok &= normal.energyAfterEngage > 10.0 && normal.energyWhenStopped < 0.01;

    const struct { const char* name; HostQuirks q; } cases[] = {
        { "Posicion por fotograma (25 fps)",        { 1920, false, false } },
        { "Reinicio del procesado al pulsar",       { 1,    true,  false } },
        { "Estado de reproduccion parpadeando",     { 1,    false, true  } },
        { "Todo a la vez",                          { 1920, true,  true  } },
    };

    for (const auto& c : cases)
    {
        const auto r = run (c.q, 1.0, 4.0);
        const bool pass = r.energyAfterEngage > 0.8 * normal.energyAfterEngage && r.energyWhenStopped < 0.01;
        std::printf ("%-40s tras PARAR: %6.2f  ya parado: %.4f  %s\n", c.name, r.energyAfterEngage, r.energyWhenStopped, pass ? "ok" : "FALLO");
        ok &= pass;
    }

    std::printf ("%s\n", ok ? "OK" : "FALLO");
    return ok ? 0 : 1;
}

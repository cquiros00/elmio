// Prueba del plugin completo con un "host" simulado.
//
// Algunos hosts (DaVinci Resolve entre ellos) no informan de la posición del
// cabezal muestra a muestra: la posición avanza a saltos (por fotograma de
// vídeo, por ejemplo). El plugin no debe confundir eso con que el usuario ha
// saltado en la línea de tiempo, porque cortaría el frenado en seco.

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
Result run (int positionStep, double engageAt, double seconds, int64_t jumpAtSample = -1)
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
    int64_t pos = 0;       // posición real
    int64_t timeline = 0;  // posición en la línea de tiempo (cambia si hay salto)
    const int64_t total = (int64_t) (seconds * sr);

    for (; pos < total; pos += block)
    {
        if (jumpAtSample >= 0 && pos >= jumpAtSample && pos < jumpAtSample + block)
            timeline += (int64_t) (5.0 * sr); // el usuario salta 5 s adelante

        head.reportedSamples = (timeline / positionStep) * positionStep;

        const double t = (double) pos / sr;
        engage->setValueNotifyingHost (t >= engageAt ? 1.0f : 0.0f);

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

    // 1) Host exacto: el frenado se oye durante el primer segundo tras pulsar.
    const auto exact = run (1, 1.0, 4.0);
    // 2) Host que actualiza la posición por fotograma (25 fps = 1920 muestras).
    const auto framed = run (1920, 1.0, 4.0);

    std::printf ("Energía 1 s tras PARAR  exacto: %.2f  por fotograma: %.2f\n", exact.energyAfterEngage, framed.energyAfterEngage);
    std::printf ("Energía ya parado       exacto: %.4f  por fotograma: %.4f\n", exact.energyWhenStopped, framed.energyWhenStopped);

    ok &= exact.energyAfterEngage > 10.0;
    ok &= framed.energyAfterEngage > 0.8 * exact.energyAfterEngage;
    ok &= exact.energyWhenStopped < 0.01 && framed.energyWhenStopped < 0.01;

    // 3) Salto real en la línea de tiempo con PARAR activado: silencio inmediato.
    const auto jump = run (1, 0.5, 4.0, (int64_t) (0.7 * 48000));
    std::printf ("Tras salto con PARAR activo (energía primer segundo): %.2f\n", jump.energyAfterEngage);
    ok &= jump.energyAfterEngage < 0.5 * exact.energyAfterEngage;

    std::printf ("%s\n", ok ? "OK" : "FALLO");
    return ok ? 0 : 1;
}

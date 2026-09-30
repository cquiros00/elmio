// Prueba del plugin completo imitando a DaVinci Resolve, según lo que mostró el
// registro de diagnóstico:
//   - solo envía audio mientras reproduce, en bloques de 480 muestras;
//   - informa la posición exacta en la línea de tiempo (que empieza en
//     01:00:00:00, es decir, 172 800 000 muestras a 48 kHz);
//   - el primer bloque de cada reproducción es más corto;
//   - antes de cada reproducción llama a reset, releaseResources y prepareToPlay.
//
// El efecto tiene que sonar igual en todas las pasadas.

#include "PluginProcessor.h"
#include "Timecode.h"

#include <cstdio>

namespace
{
constexpr double sr    = 48000.0;
constexpr int    block = 480;
constexpr int64_t timelineStart = 172800000; // 01:00:00:00

struct FakePlayHead : juce::AudioPlayHead
{
    int64_t reportedSamples = 0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (true);
        info.setTimeInSamples (reportedSamples);
        info.setFrameRate (juce::AudioPlayHead::fps25);
        return info;
    }
};

float inputAt (int64_t pos)
{
    return 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (double) pos / sr);
}

// Reproduce desde `from` durante `seconds`. Devuelve el canal izquierdo.
std::vector<float> play (TocadiscosStopProcessor& proc, FakePlayHead& head, int64_t from, double seconds)
{
    proc.reset();
    proc.releaseResources();
    proc.prepareToPlay (sr, block);

    std::vector<float> out;
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;

    // Primer bloque corto, informado en la rejilla de 480 (como en el registro).
    const int64_t grid = from / block * block;
    int64_t pos = from;
    bool first = true;
    const int64_t end = from + (int64_t) (seconds * sr);

    while (pos < end)
    {
        const int len = first ? (int) (grid + block - pos) : block;
        head.reportedSamples = first ? grid : pos;
        first = false;

        buf.setSize (2, len, false, false, true);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < len; ++i)
                buf.setSample (c, i, inputAt (pos + i));

        proc.processBlock (buf, midi);
        for (int i = 0; i < len; ++i)
            out.push_back (buf.getSample (0, i));
        pos += len;
    }
    return out;
}

double energy (const std::vector<float>& v, int64_t from, int64_t to)
{
    double e = 0.0;
    from = std::max<int64_t> (0, from);
    to   = std::min<int64_t> ((int64_t) v.size(), to);
    for (int64_t i = from; i < to; ++i)
        e += std::abs (v[(size_t) i]);
    return to > from ? e / (double) (to - from) : 0.0;
}

bool check (bool condition, const char* name, double value)
{
    std::printf ("%-58s %8.4f  %s\n", name, value, condition ? "ok" : "FALLO");
    return condition;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    bool ok = true;

    // En el heap, como lo crea un host (el procesador ocupa bastante memoria).
    auto procPtr = std::make_unique<TocadiscosStopProcessor>();
    auto& proc = *procPtr;
    FakePlayHead head;
    proc.setPlayHead (&head);
    proc.setPlayConfigDetails (2, 2, sr, block);

    auto* stopT = proc.apvts.getParameter (ParamIDs::stopTime);
    stopT->setValueNotifyingHost (stopT->convertTo0to1 (1.5f));

    const int64_t start  = timelineStart + (int64_t) (347.1 * sr); // 01:05:47 aprox.
    const int64_t stopAt = start + (int64_t) (1.0 * sr);
    const int64_t s1     = (int64_t) sr;                            // 1 s en muestras
    const double  dry    = 0.5 * 2.0 / juce::MathConstants<double>::pi; // |seno| medio

    // Sin punto de parada: la música pasa sin cambios.
    {
        const auto out = play (proc, head, start, 1.0);
        double maxDiff = 0.0;
        for (size_t i = 0; i < out.size(); ++i)
            maxDiff = std::max (maxDiff, (double) std::abs (out[i] - inputAt (start + (int64_t) i)));
        ok &= check (maxDiff < 1.0e-6, "Sin punto de parada: salida = entrada (dif. max.)", maxDiff);
    }

    proc.setStopPosition (stopAt);

    // Tres pasadas desde el mismo punto: tienen que ser idénticas.
    const auto pass1 = play (proc, head, start, 3.5);
    const auto pass2 = play (proc, head, start, 3.5);
    const auto pass3 = play (proc, head, start, 3.5);

    double diff = 0.0;
    for (size_t i = 0; i < pass1.size(); ++i)
        diff = std::max ({ diff, (double) std::abs (pass1[i] - pass2[i]), (double) std::abs (pass1[i] - pass3[i]) });

    ok &= check (energy (pass1, 0, s1) > 0.95 * dry,            "Antes del punto: suena normal", energy (pass1, 0, s1));
    ok &= check (energy (pass1, s1, s1 + s1) > 0.3 * dry,       "Frenando (1 s tras el punto): se oye", energy (pass1, s1, s1 + s1));
    ok &= check (energy (pass1, s1 + (int64_t) (1.6 * sr), (int64_t) pass1.size()) < 1.0e-4,
                 "Tras el frenado: silencio", energy (pass1, s1 + (int64_t) (1.6 * sr), (int64_t) pass1.size()));
    ok &= check (diff < 1.0e-6, "Pasadas 1, 2 y 3 identicas (dif. max.)", diff);

    // Reproducir empezando en mitad del frenado: se oye el final del frenado.
    {
        const auto mid = play (proc, head, stopAt + (int64_t) (0.5 * sr), 2.0);
        ok &= check (energy (mid, 0, (int64_t) (0.5 * sr)) > 0.05 * dry, "Empezando a mitad del frenado: se oye", energy (mid, 0, (int64_t) (0.5 * sr)));
        ok &= check (energy (mid, (int64_t) (1.1 * sr), (int64_t) mid.size()) < 1.0e-4, "  ... y acaba en silencio", energy (mid, (int64_t) (1.1 * sr), (int64_t) mid.size()));
    }

    // Reproducir después del frenado: silencio.
    {
        const auto after = play (proc, head, stopAt + 3 * s1, 1.0);
        ok &= check (energy (after, 0, (int64_t) after.size()) < 1.0e-4, "Despues del frenado: silencio", energy (after, 0, (int64_t) after.size()));
    }

    // "PARAR AQUI" usa la última posición procesada.
    ok &= check (proc.getLastPosition() == stopAt + 4 * s1, "Ultima posicion procesada", (double) (proc.getLastPosition() - stopAt));

    // El punto de parada se guarda con el proyecto.
    {
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        auto restored = std::make_unique<TocadiscosStopProcessor>();
        restored->setStateInformation (state.getData(), (int) state.getSize());
        ok &= check (restored->getStopPosition() == stopAt, "Punto guardado y recuperado", (double) (restored->getStopPosition() - stopAt));
    }

    // Código de tiempo: 01:05:47:01 a 25 fps.
    {
        const auto tc = Timecode::format (189458560, sr, 25.0);
        ok &= check (tc == "01:05:47:01", ("Codigo de tiempo " + tc).c_str(), 0.0);
        ok &= check (Timecode::parse (tc, sr, 25.0) <= 189458560 && Timecode::format (Timecode::parse (tc, sr, 25.0), sr, 25.0) == tc,
                     "Codigo de tiempo ida y vuelta", 0.0);
    }

    std::printf ("%s\n", ok ? "OK" : "FALLO");
    return ok ? 0 : 1;
}

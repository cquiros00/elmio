#pragma once

// Registro de diagnóstico: anota lo que hace el host (llamadas de preparación,
// posición del cabezal, estado de reproducción, valor de "Parar"...) en un
// archivo de texto dentro de Documentos, para poder ver cómo trata DaVinci
// Resolve al plugin.
//
// El hilo de audio solo copia una estructura pequeña a una cola sin bloqueos;
// la escritura en disco se hace desde el hilo de mensajes con un temporizador.

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

class DiagnosticLog : private juce::Timer
{
public:
    enum class Type : int { Block, Prepare, Reset, Release, Snap };

    struct Entry
    {
        double  wallMs     = 0.0;
        Type    type       = Type::Block;
        int     numSamples = 0;
        int     playing    = -1;   // -1 = el host no lo informa
        int64_t samplePos  = -1;   // -1 = el host no lo informa
        float   engage     = 0.0f;
        int     state      = 0;
        float   rate       = 0.0f;
        double  extra      = 0.0;
    };

    DiagnosticLog()
    {
        startMs = juce::Time::getMillisecondCounterHiRes();

        const auto name = "TocadiscosStop-diagnostico-"
                        + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S")
                        + "-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt (0xffff))
                        + ".txt";
        file = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name);

        stream = std::make_unique<juce::FileOutputStream> (file);
        if (! stream->openedOk())
        {
            stream.reset();
            return;
        }

        *stream << "Tocadiscos Stop - registro de diagnostico\n"
                << "Host: " << juce::PluginHostType().getHostDescription()
                << "  Formato: " << juce::AudioProcessor::getWrapperTypeDescription (juce::PluginHostType::getPluginLoadedAs())
                << "\n"
                << "Columnas: ms tipo muestras reproduciendo posicion parar estado velocidad extra\n"
                << "Estados: 0=girando 1=frenando 2=parado 3=arrancando 4=volviendo\n\n";
        stream->flush();
        startTimer (250);
    }

    ~DiagnosticLog() override
    {
        stopTimer();
        timerCallback();
    }

    // Se puede llamar desde el hilo de audio.
    void push (Entry e) noexcept
    {
        e.wallMs = juce::Time::getMillisecondCounterHiRes() - startMs;
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0)       entries[(size_t) scope.startIndex1] = e;
        else if (scope.blockSize2 > 0)  entries[(size_t) scope.startIndex2] = e;
        else                            dropped.fetch_add (1, std::memory_order_relaxed);
    }

    juce::String getFileName() const { return stream != nullptr ? file.getFileName() : juce::String ("(no se pudo crear)"); }

private:
    void timerCallback() override
    {
        if (stream == nullptr)
            return;

        static const char* names[] = { "bloque", "PREPARAR", "RESET", "LIBERAR", "SNAP" };

        const auto scope = fifo.read (fifo.getNumReady());
        auto write = [this] (int start, int size)
        {
            for (int i = start; i < start + size; ++i)
            {
                if (linesWritten >= maxLines)
                    return;
                const auto& e = entries[(size_t) i];
                *stream << juce::String (e.wallMs, 1) << ' ' << names[(int) e.type] << ' ' << e.numSamples << ' '
                        << e.playing << ' ' << (juce::int64) e.samplePos << ' ' << juce::String (e.engage, 2) << ' '
                        << e.state << ' ' << juce::String (e.rate, 3) << ' ' << juce::String (e.extra, 1) << '\n';
                ++linesWritten;
            }
        };
        write (scope.startIndex1, scope.blockSize1);
        write (scope.startIndex2, scope.blockSize2);

        if (const auto d = dropped.exchange (0); d > 0)
            *stream << "(perdidas " << d << " entradas)\n";
        if (linesWritten >= maxLines && ! truncatedNoted)
        {
            *stream << "(registro truncado)\n";
            truncatedNoted = true;
        }
        stream->flush();
    }

    static constexpr int capacity = 16384;
    static constexpr int maxLines = 200000;

    juce::AbstractFifo fifo { capacity };
    std::array<Entry, capacity> entries {};
    std::atomic<int> dropped { 0 };

    double startMs = 0.0;
    juce::File file;
    std::unique_ptr<juce::FileOutputStream> stream;
    int  linesWritten   = 0;
    bool truncatedNoted = false;
};

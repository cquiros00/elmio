#pragma once

// Motor DSP del efecto "Tocadiscos Stop".
//
// No depende de JUCE: se puede usar desde el plugin y desde las pruebas
// offline (tests/render_test.cpp).
//
// El efecto depende solo de la posición en la línea de tiempo, no de lo que
// haya sonado antes: el disco frena siempre en el mismo punto (`stopPos`), en
// cada reproducción y en el render final. DaVinci Resolve reinicia el plugin
// cada vez que se pulsa reproducir y solo le envía audio mientras reproduce,
// así que un efecto que dependiera del historial se oiría una sola vez.
//
// Funcionamiento: el audio que entra se guarda en un buffer indexado por
// posición absoluta. Antes del punto de parada la salida es la entrada tal
// cual. Desde el punto de parada, un cabezal de lectura recorre ese buffer a
// una velocidad que cae de 1 a 0 en `stopSeconds`, así que tono y tempo bajan
// juntos hasta el silencio. La posición del cabezal se calcula con una fórmula
// cerrada a partir de la posición actual, por lo que es la misma en cada pasada.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

class TurntableEngine
{
public:
    struct Params
    {
        float stopSeconds = 2.5f;  // duración del frenado
        float curve       = 1.0f;  // forma del frenado: 1 = lineal (fricción real)
        float tone        = 0.4f;  // 0..1, cuánto se oscurece el sonido al frenar
        float fade        = 0.5f;  // 0..1, cuánto baja el volumen con la velocidad
    };

    static constexpr double maxStopSeconds = 10.0;

    void prepare (double newSampleRate, int newNumChannels)
    {
        sampleRate  = newSampleRate;
        numChannels = std::max (1, newNumChannels);

        bufferSize = (int64_t) std::ceil ((maxStopSeconds + 1.0) * sampleRate) + 16;
        buffers.assign ((size_t) numChannels, std::vector<float> ((size_t) bufferSize, 0.0f));
        lp1.assign ((size_t) numChannels, 0.0f);
        lp2.assign ((size_t) numChannels, 0.0f);

        gainSmoothCoef = (float) std::exp (-1.0 / (0.004 * sampleRate));
        expectedPos = -1;
    }

    // Procesa el audio en el sitio. `blockPos` es la posición (en muestras) de
    // la primera muestra del bloque en la línea de tiempo. `stopPos` es el
    // punto donde empieza a frenar, o -1 si no hay punto de parada.
    void process (float* const* channels, int numChans, int numSamples,
                  int64_t blockPos, int64_t stopPos, const Params& p)
    {
        numChans = std::min (numChans, numChannels);

        // Un bloque que no sigue al anterior es una reproducción nueva (o un
        // salto): el audio guardado de antes no sirve.
        if (blockPos != expectedPos)
        {
            validStart   = blockPos;
            offsetKnown  = false;
            freshSegment = true;
        }
        expectedPos = blockPos + numSamples;

        const double stopLen = std::clamp ((double) p.stopSeconds, 0.05, maxStopSeconds) * sampleRate;
        const double curve   = std::clamp ((double) p.curve, 0.1, 10.0);

        float lastRate = 1.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const int64_t pos = blockPos + i;

            // 1) Guardar la entrada.
            const int64_t w = wrap (pos);
            for (int c = 0; c < numChans; ++c)
                buffers[(size_t) c][(size_t) w] = channels[c][i];
            validStart = std::max (validStart, pos - (bufferSize - 8));

            // 2) Antes del punto de parada: sin cambios.
            if (stopPos < 0 || pos < stopPos)
            {
                for (int c = 0; c < numChans; ++c)
                {
                    lp1[(size_t) c] = lp2[(size_t) c] = channels[c][i];
                    channels[c][i] *= smoothGain (1.0f, c == 0);
                }
                lastRate = 1.0f;
                freshSegment = false;
                continue;
            }

            // 3) Frenando o parado.
            const double x = (double) (pos - stopPos) / stopLen;
            double rate = 0.0;
            double readPos = 0.0;
            if (x < 1.0)
            {
                rate = std::pow (1.0 - x, curve);
                // Integral de la velocidad: cuánto ha avanzado el disco desde stopPos.
                readPos = (double) stopPos + stopLen * (1.0 - std::pow (1.0 - x, curve + 1.0)) / (curve + 1.0);

                // Si la reproducción empezó después del punto de parada no
                // tenemos el audio de ese tramo: se desplaza el cabezal para
                // empezar a leer desde lo primero que sí tenemos.
                if (! offsetKnown)
                {
                    readOffset  = std::max (0.0, (double) validStart - readPos);
                    offsetKnown = true;
                }
                readPos = std::min (readPos + readOffset, (double) pos);
            }

            const float targetGain = computeGain (rate, p.fade);
            const float lpCoef     = computeFilterCoef (rate, p.tone);

            for (int c = 0; c < numChans; ++c)
            {
                float wet = x < 1.0 ? readInterpolated ((size_t) c, readPos, pos) : 0.0f;

                // Filtro paso bajo de 2 polos que se va cerrando con la velocidad.
                auto& s1 = lp1[(size_t) c];
                auto& s2 = lp2[(size_t) c];
                if (freshSegment) { s1 = s2 = wet; }
                s1 = wet + lpCoef * (s1 - wet);
                s2 = s1  + lpCoef * (s2 - s1);

                channels[c][i] = s2 * smoothGain (targetGain, c == 0);
            }
            lastRate = (float) rate;
            freshSegment = false;
        }

        if (numSamples > 0)
            currentRate.store (lastRate, std::memory_order_relaxed);
    }

    // Velocidad actual del plato (0..1). Se puede leer desde otro hilo (la interfaz).
    std::atomic<float> currentRate { 1.0f };

private:
    int64_t wrap (int64_t pos) const noexcept
    {
        const int64_t m = pos % bufferSize;
        return m < 0 ? m + bufferSize : m;
    }

    float smoothGain (float target, bool advance)
    {
        if (advance)
        {
            if (freshSegment)
                smoothedGain = target;
            smoothedGain = target + gainSmoothCoef * (smoothedGain - target);
        }
        return smoothedGain;
    }

    static float computeGain (double r, float fade)
    {
        // Pérdida de volumen proporcional a la velocidad + fundido final para evitar clics.
        const double body = 1.0 - (double) std::clamp (fade, 0.0f, 1.0f) * (1.0 - r);
        const double x    = std::clamp (r / 0.04, 0.0, 1.0);
        const double tail = x * x * (3.0 - 2.0 * x);
        return (float) (body * tail);
    }

    float computeFilterCoef (double r, float tone) const
    {
        const double t = std::clamp ((double) tone, 0.0, 1.0);
        if (t <= 0.0001)
            return 0.0f;
        const double maxHz = std::min (20000.0, sampleRate * 0.45);
        const double fc    = 120.0 + (maxHz - 120.0) * std::pow (r, 1.0 + 4.0 * t);
        return (float) std::exp (-2.0 * 3.14159265358979323846 * fc / sampleRate);
    }

    float sampleAt (size_t c, int64_t index, int64_t newest) const
    {
        index = std::clamp (index, validStart, newest);
        return buffers[c][(size_t) wrap (index)];
    }

    // Interpolación cúbica (Hermite) entre muestras del buffer.
    float readInterpolated (size_t c, double pos, int64_t newest) const
    {
        const int64_t i0 = (int64_t) std::floor (pos);
        const float   f  = (float) (pos - (double) i0);

        const float xm1 = sampleAt (c, i0 - 1, newest);
        const float x0  = sampleAt (c, i0,     newest);
        const float x1  = sampleAt (c, i0 + 1, newest);
        const float x2  = sampleAt (c, i0 + 2, newest);

        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    double sampleRate  = 48000.0;
    int    numChannels = 2;

    std::vector<std::vector<float>> buffers;
    std::vector<float> lp1, lp2;
    int64_t bufferSize  = 1;
    int64_t expectedPos = -1;
    int64_t validStart  = 0;

    bool   offsetKnown  = false;
    double readOffset   = 0.0;
    bool   freshSegment = true;

    float smoothedGain   = 1.0f;
    float gainSmoothCoef = 0.0f;
};

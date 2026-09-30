#pragma once

// Motor DSP del efecto "Tocadiscos Stop".
//
// No depende de JUCE: se puede usar desde el plugin y desde las pruebas
// offline (tests/render_test.cpp).
//
// Idea: todo el audio que entra se escribe en un buffer circular. Mientras el
// efecto está en reposo la salida es la entrada tal cual. Al activar "Parar",
// un cabezal de lectura empieza a recorrer el buffer a una velocidad que cae
// de 1.0 a 0.0 (igual que un plato que pierde velocidad), por lo que el tono y
// el tempo bajan juntos hasta el silencio. Al soltar "Parar" el plato vuelve a
// arrancar y, al llegar a velocidad normal, se hace un fundido corto de vuelta
// al audio en directo.

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
        float stopSeconds  = 1.5f;  // duración del frenado
        float curve        = 1.0f;  // forma del frenado: 1 = lineal (fricción real)
        float startSeconds = 0.6f;  // duración del arranque al soltar (0 = instantáneo)
        float tone         = 0.4f;  // 0..1, cuánto se oscurece el sonido al frenar
        float fade         = 0.3f;  // 0..1, cuánto baja el volumen con la velocidad
    };

    enum class State { Running, SpinningDown, Stopped, SpinningUp, CatchingUp };

    static constexpr double maxLagSeconds = 16.0;

    void prepare (double newSampleRate, int newNumChannels)
    {
        sampleRate  = newSampleRate;
        numChannels = std::max (1, newNumChannels);

        bufferSize = (int64_t) std::ceil (maxLagSeconds * sampleRate) + 16;
        buffers.assign ((size_t) numChannels, std::vector<float> ((size_t) bufferSize, 0.0f));
        lp1.assign ((size_t) numChannels, 0.0f);
        lp2.assign ((size_t) numChannels, 0.0f);

        catchUpLength = std::max (1, (int) (0.030 * sampleRate)); // fundido de 30 ms
        gainSmoothCoef = (float) std::exp (-1.0 / (0.004 * sampleRate));

        snapTo (false);
    }

    // Coloca el motor directamente en un estado estable, sin rampas. Se usa al
    // empezar la reproducción o al saltar en la línea de tiempo, para que el
    // resultado no dependa de lo que sonó antes.
    void snapTo (bool engaged)
    {
        for (auto& b : buffers)
            std::fill (b.begin(), b.end(), 0.0f);
        std::fill (lp1.begin(), lp1.end(), 0.0f);
        std::fill (lp2.begin(), lp2.end(), 0.0f);

        writeCount    = 0;
        readPos       = 0.0;
        progress      = 0.0;
        catchUpPos    = 0;
        state         = engaged ? State::Stopped : State::Running;
        rate          = engaged ? 0.0 : 1.0;
        smoothedGain  = engaged ? 0.0f : 1.0f;
        currentRate.store ((float) rate);
    }

    // Procesa el audio en el sitio. `channels` apunta a `numChans` canales de
    // `numSamples` muestras. `engaged` es el estado del botón "Parar".
    void process (float* const* channels, int numChans, int numSamples, bool engaged, const Params& p)
    {
        numChans = std::min (numChans, numChannels);
        const double stopInc  = 1.0 / std::max (1.0, (double) p.stopSeconds * sampleRate);
        const bool   instantStart = p.startSeconds < 0.001f;
        const double startInc = instantStart ? 1.0 : 1.0 / ((double) p.startSeconds * sampleRate);
        const double curve    = std::clamp ((double) p.curve, 0.1, 10.0);

        for (int i = 0; i < numSamples; ++i)
        {
            // 1) Guardar la entrada en el buffer circular.
            const int64_t w = writeCount % bufferSize;
            for (int c = 0; c < numChans; ++c)
                buffers[(size_t) c][(size_t) w] = channels[c][i];
            ++writeCount;
            const double live = (double) (writeCount - 1); // posición de la muestra recién escrita

            // 2) Transiciones según el botón.
            if (engaged)
            {
                if (state == State::Running)
                {
                    readPos  = live;
                    progress = 0.0;
                    state    = State::SpinningDown;
                }
                else if (state == State::SpinningUp || state == State::CatchingUp)
                {
                    progress = 1.0 - std::pow (rate, 1.0 / curve); // continuar desde la velocidad actual
                    state    = State::SpinningDown;
                }
            }
            else
            {
                if (state == State::Stopped)
                {
                    // El disco vuelve a girar desde el punto en directo.
                    readPos  = live;
                    progress = 0.0;
                    rate     = 0.0;
                    state    = State::SpinningUp;
                }
                else if (state == State::SpinningDown)
                {
                    progress = 1.0 - std::pow (1.0 - rate, 1.0 / spinUpShape);
                    state    = State::SpinningUp;
                }
            }

            // 3) Avanzar la curva de velocidad.
            if (state == State::SpinningDown)
            {
                progress += stopInc;
                if (progress >= 1.0) { progress = 1.0; rate = 0.0; state = State::Stopped; }
                else                  rate = std::pow (1.0 - progress, curve);
            }
            else if (state == State::SpinningUp)
            {
                progress += startInc;
                if (progress >= 1.0)
                {
                    progress   = 1.0;
                    rate       = 1.0;
                    state      = State::CatchingUp;
                    catchUpPos = 0;
                }
                else
                {
                    rate = 1.0 - std::pow (1.0 - progress, spinUpShape);
                }
            }

            // Nunca dejar que el cabezal se quede más atrás de lo que cabe en el buffer.
            const double maxLag = (double) (bufferSize - 8);
            if (live - readPos > maxLag)
                readPos = live - maxLag;

            // 4) Generar la salida.
            if (state == State::Running)
            {
                for (int c = 0; c < numChans; ++c)
                {
                    lp1[(size_t) c] = lp2[(size_t) c] = channels[c][i];
                    channels[c][i] *= smoothGain (1.0f, c == 0);
                }
                readPos = live;
                currentRate.store (1.0f, std::memory_order_relaxed);
                continue;
            }

            const float targetGain = computeGain (rate, p.fade);
            const float lpCoef     = computeFilterCoef (rate, p.tone);

            float xfade = 0.0f;
            if (state == State::CatchingUp)
                xfade = (float) catchUpPos / (float) catchUpLength;

            for (int c = 0; c < numChans; ++c)
            {
                const float dry = channels[c][i];
                float wet = state == State::Stopped ? 0.0f : readInterpolated ((size_t) c, readPos, live);

                // Filtro paso bajo de 2 polos que se va cerrando con la velocidad.
                auto& s1 = lp1[(size_t) c];
                auto& s2 = lp2[(size_t) c];
                s1 = wet + lpCoef * (s1 - wet);
                s2 = s1  + lpCoef * (s2 - s1);
                wet = s2;

                const float g = smoothGain (targetGain, c == 0);
                wet *= g;

                channels[c][i] = state == State::CatchingUp ? wet * (1.0f - xfade) + dry * xfade : wet;
            }

            if (state != State::Stopped)
                readPos += rate;

            if (state == State::CatchingUp && ++catchUpPos >= catchUpLength)
            {
                state   = State::Running;
                readPos = live;
            }

            currentRate.store ((float) rate, std::memory_order_relaxed);
        }
    }

    State getState() const noexcept { return state; }

    // Velocidad actual del plato (0..1). Se puede leer desde otro hilo (la interfaz).
    std::atomic<float> currentRate { 1.0f };

private:
    static constexpr double spinUpShape = 1.6; // el motor acelera rápido y se estabiliza al final

    float smoothGain (float target, bool advance)
    {
        if (advance)
            smoothedGain = target + gainSmoothCoef * (smoothedGain - target);
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
        index = std::clamp (index, std::max<int64_t> (0, newest - bufferSize + 1), newest);
        return buffers[c][(size_t) (index % bufferSize)];
    }

    // Interpolación cúbica (Hermite) entre muestras del buffer.
    float readInterpolated (size_t c, double pos, double live) const
    {
        const int64_t newest = (int64_t) live;
        const int64_t i0     = (int64_t) std::floor (pos);
        const float   f      = (float) (pos - (double) i0);

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
    int64_t bufferSize = 1;
    int64_t writeCount = 0;
    double  readPos    = 0.0;

    State  state    = State::Running;
    double rate     = 1.0;
    double progress = 0.0;

    int   catchUpPos     = 0;
    int   catchUpLength  = 1;
    float smoothedGain   = 1.0f;
    float gainSmoothCoef = 0.0f;
};

#pragma once

// Conversión entre posiciones en muestras y código de tiempo HH:MM:SS:FF,
// como lo muestra la línea de tiempo de DaVinci Resolve (que incluye el código
// de inicio de la línea de tiempo, normalmente 01:00:00:00).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace Timecode
{
    // Fotogramas por segundo "nominales" (24 para 23,976; 30 para 29,97...).
    inline int nominalFps (double fps) { return fps > 0.0 ? (int) std::lround (fps) : 0; }

    // Si se conoce la cadencia (fps > 0) devuelve "HH:MM:SS:FF"; si no, "HH:MM:SS.mmm".
    inline std::string format (int64_t samples, double sampleRate, double fps)
    {
        if (samples < 0 || sampleRate <= 0.0)
            return "--:--:--:--";

        char text[48];
        const double seconds = (double) samples / sampleRate;

        if (const int nom = nominalFps (fps); nom > 0)
        {
            const int64_t frames = (int64_t) std::floor (seconds * fps + 1.0e-6);
            const int64_t f  = frames % nom;
            const int64_t ts = frames / nom;
            std::snprintf (text, sizeof (text), "%02lld:%02lld:%02lld:%02lld",
                           (long long) (ts / 3600), (long long) (ts / 60 % 60), (long long) (ts % 60), (long long) f);
        }
        else
        {
            const int64_t ms = (int64_t) std::floor (seconds * 1000.0 + 1.0e-6);
            std::snprintf (text, sizeof (text), "%02lld:%02lld:%02lld.%03lld",
                           (long long) (ms / 3600000), (long long) (ms / 60000 % 60),
                           (long long) (ms / 1000 % 60), (long long) (ms % 1000));
        }
        return text;
    }

    // Acepta "HH:MM:SS:FF", "HH:MM:SS.mmm", "HH:MM:SS" o "MM:SS"
    // (también ';' como separador de fotogramas).
    // Devuelve -1 si el texto no es válido.
    inline int64_t parse (const std::string& input, double sampleRate, double fps)
    {
        if (sampleRate <= 0.0)
            return -1;

        long parts[4] = { 0, 0, 0, 0 };
        int n = 0;
        double fraction = -1.0; // parte decimal de los segundos, si la hay
        std::string digits;

        auto push = [&] () -> bool
        {
            if (digits.empty() || n >= 4) return false;
            parts[n++] = std::stol (digits);
            digits.clear();
            return true;
        };

        for (size_t i = 0; i < input.size(); ++i)
        {
            const char ch = input[i];
            if (ch >= '0' && ch <= '9')       digits += ch;
            else if (ch == ':' || ch == ';')  { if (! push()) return -1; }
            else if (ch == '.' || ch == ',')
            {
                // Parte decimal de los segundos: "HH:MM:SS.mmm".
                if (! push()) return -1;
                const std::string rest = input.substr (i + 1);
                if (rest.empty() || rest.find_first_not_of ("0123456789") != std::string::npos) return -1;
                fraction = std::stod ("0." + rest);
                break;
            }
            else if (ch != ' ') return -1;
        }
        if (! digits.empty() && ! push()) return -1;
        if (n < 2) return -1;

        long h = 0, m = 0, s = 0, f = 0;
        if (n == 4)      { h = parts[0]; m = parts[1]; s = parts[2]; f = parts[3]; }
        else if (n == 3) { h = parts[0]; m = parts[1]; s = parts[2]; }
        else             { m = parts[0]; s = parts[1]; }
        if (m > 59 || s > 59) return -1;

        const int nom = nominalFps (fps);
        if (n == 4 && nom <= 0) return -1;      // fotogramas sin saber los fps
        if (n == 4 && fraction >= 0.0) return -1;
        if (nom > 0 && f >= nom) return -1;

        double seconds;
        if (nom > 0)
        {
            const double frames = (double) ((h * 3600 + m * 60 + s) * nom + f);
            seconds = frames / fps;
            if (fraction > 0.0) seconds += fraction * (double) nom / fps;
        }
        else
        {
            seconds = (double) (h * 3600 + m * 60 + s) + std::max (0.0, fraction);
        }
        // Hacia arriba, para caer siempre dentro del fotograma indicado.
        return (int64_t) std::ceil (seconds * sampleRate - 1.0e-6);
    }

    // Duración de un fotograma en muestras (o 40 ms si no se conocen los fps).
    inline int64_t frameLength (double sampleRate, double fps)
    {
        return (int64_t) std::llround (sampleRate / (fps > 0.0 ? fps : 25.0));
    }
}

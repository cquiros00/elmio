// Prueba offline del motor: genera una música sintética, aplica el efecto y
// guarda el resultado en un WAV para escucharlo. También comprueba que no haya
// valores no válidos ni picos y que el tramo "parado" esté en silencio.
//
//   g++ -std=c++17 -O2 -I plugins/TocadiscosStop/Source tests/render_test.cpp -o render_test
//   ./render_test salida.wav [archivo_entrada.wav]

#include "TurntableEngine.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

static constexpr double kPi = 3.14159265358979323846;

static void writeWav (const std::string& path, const std::vector<std::vector<float>>& ch, int sr)
{
    const int nc = (int) ch.size();
    const int n  = (int) ch[0].size();
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    const uint32_t dataBytes = (uint32_t) (n * nc * 2);
    f.write ("RIFF", 4); u32 (36 + dataBytes); f.write ("WAVE", 4);
    f.write ("fmt ", 4); u32 (16); u16 (1); u16 ((uint16_t) nc); u32 ((uint32_t) sr);
    u32 ((uint32_t) (sr * nc * 2)); u16 ((uint16_t) (nc * 2)); u16 (16);
    f.write ("data", 4); u32 (dataBytes);
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < nc; ++c)
        {
            const float v = std::clamp (ch[(size_t) c][(size_t) i], -1.0f, 1.0f);
            u16 ((uint16_t) (int16_t) std::lrint (v * 32767.0f));
        }
}

// Lector WAV mínimo: PCM 16 bits o float 32 bits.
static bool readWav (const std::string& path, std::vector<std::vector<float>>& ch, int& sr)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) return false;
    char id[4]; uint32_t size = 0;
    f.read (id, 4); f.read ((char*) &size, 4); f.read (id, 4);
    if (std::strncmp (id, "WAVE", 4) != 0) return false;
    uint16_t fmt = 0, nc = 0, bits = 0;
    while (f.read (id, 4) && f.read ((char*) &size, 4))
    {
        if (std::strncmp (id, "fmt ", 4) == 0)
        {
            std::vector<char> b (size); f.read (b.data(), size);
            std::memcpy (&fmt, &b[0], 2); std::memcpy (&nc, &b[2], 2);
            uint32_t r; std::memcpy (&r, &b[4], 4); sr = (int) r;
            std::memcpy (&bits, &b[14], 2);
            if (fmt == 0xFFFE && size >= 26) std::memcpy (&fmt, &b[24], 2);
        }
        else if (std::strncmp (id, "data", 4) == 0)
        {
            const int bytes = bits / 8;
            const int n = (int) (size / (uint32_t) (bytes * nc));
            ch.assign (nc, std::vector<float> ((size_t) n));
            std::vector<char> b (size); f.read (b.data(), size);
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < nc; ++c)
                {
                    const char* p = &b[(size_t) ((i * nc + c) * bytes)];
                    float v = 0;
                    if (fmt == 3 && bits == 32) std::memcpy (&v, p, 4);
                    else if (fmt == 1 && bits == 16) { int16_t s; std::memcpy (&s, p, 2); v = s / 32768.0f; }
                    else if (fmt == 1 && bits == 24) { int32_t s = (uint8_t) p[0] | ((uint8_t) p[1] << 8) | ((int8_t) p[2] << 16); v = s / 8388608.0f; }
                    else return false;
                    ch[(size_t) c][(size_t) i] = v;
                }
            return true;
        }
        else f.seekg (size + (size & 1), std::ios::cur);
    }
    return false;
}

// Música sintética: acordes con armónicos + bombo y hi-hat a 120 BPM.
static std::vector<std::vector<float>> makeMusic (int sr, double seconds)
{
    const int n = (int) (seconds * sr);
    std::vector<std::vector<float>> ch (2, std::vector<float> ((size_t) n));
    const double chords[4][3] = { { 220.0, 277.18, 329.63 }, { 196.0, 246.94, 293.66 },
                                  { 174.61, 220.0, 261.63 }, { 196.0, 246.94, 311.13 } };
    uint32_t noise = 12345;
    for (int i = 0; i < n; ++i)
    {
        const double t    = (double) i / sr;
        const double beat = t * 2.0;
        const auto&  ch3  = chords[((int) (t / 2.0)) % 4];
        double s = 0.0;
        for (double f : ch3)
            for (int h = 1; h <= 4; ++h)
                s += std::sin (2.0 * kPi * f * h * t) / (h * 8.0);
        const double kt = (beat - std::floor (beat)) / 2.0;
        s += 0.6 * std::sin (2.0 * kPi * (50.0 + 90.0 * std::exp (-kt * 40.0)) * kt) * std::exp (-kt * 12.0);
        noise = noise * 1664525u + 1013904223u;
        const double ht = ((beat + 0.5) - std::floor (beat + 0.5)) / 2.0;
        const double hat = ((double) (noise >> 8) / 8388608.0 - 1.0) * std::exp (-ht * 60.0) * 0.15;
        ch[0][(size_t) i] = (float) (0.5 * s + hat);
        ch[1][(size_t) i] = (float) (0.5 * s - hat);
    }
    return ch;
}

int main (int argc, char** argv)
{
    const std::string outPath = argc > 1 ? argv[1] : "tocadiscos_stop_demo.wav";
    int sr = 48000;
    std::vector<std::vector<float>> audio;
    if (argc > 2)
    {
        if (! readWav (argv[2], audio, sr)) { std::fprintf (stderr, "No se pudo leer %s\n", argv[2]); return 1; }
    }
    else
    {
        audio = makeMusic (sr, 9.0);
    }

    const int n = (int) audio[0].size();
    const double total = (double) n / sr;
    const double engageAt  = total * 0.35;  // pulsar "Parar"
    const double releaseAt = total * 0.75;  // soltar "Parar"

    TurntableEngine engine;
    engine.prepare (sr, (int) audio.size());
    TurntableEngine::Params p;

    const int block = 512;
    std::vector<float*> ptrs (audio.size());
    double stoppedEnergy = 0.0; int stoppedCount = 0;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        for (size_t c = 0; c < audio.size(); ++c) ptrs[c] = audio[c].data() + start;
        const double t = (double) start / sr;
        const bool engaged = t >= engageAt && t < releaseAt;
        engine.process (ptrs.data(), (int) audio.size(), len, engaged, p);
        if (engine.getState() == TurntableEngine::State::Stopped)
            for (int i = 0; i < len; ++i) { stoppedEnergy += std::abs (audio[0][(size_t) (start + i)]); ++stoppedCount; }
    }

    float peak = 0.0f; bool finite = true;
    for (auto& c : audio)
        for (float v : c) { finite = finite && std::isfinite (v); peak = std::max (peak, std::abs (v)); }

    writeWav (outPath, audio, sr);
    std::printf ("Escrito %s (%.2f s). Parar en %.2f s, soltar en %.2f s\n", outPath.c_str(), total, engageAt, releaseAt);
    std::printf ("Pico: %.3f  Valores finitos: %s  Muestras en silencio (parado): %d, nivel medio %.6f\n",
                 peak, finite ? "sí" : "NO", stoppedCount, stoppedCount ? stoppedEnergy / stoppedCount : 0.0);

    const bool ok = finite && peak < 2.0f && stoppedCount > 0 && stoppedEnergy / stoppedCount < 1e-4;
    std::printf ("%s\n", ok ? "OK" : "FALLO");
    return ok ? 0 : 1;
}

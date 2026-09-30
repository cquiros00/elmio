# Elmio · Efectos para DaVinci Resolve Studio

Efectos de audio editables para usar en las pistas de audio de **DaVinci Resolve Studio**
(página Fairlight y página Edit). Se distribuyen como plugins **VST3** (y **AU** en macOS),
que es el formato de plugins de audio que admite Resolve Studio.

## 1. Tocadiscos Stop

Simula que pulsas el botón de parada de un tocadiscos: el plato pierde velocidad poco a poco,
la música baja de tono y de tempo a la vez y se apaga hasta el silencio.

Marcas **un punto de la línea de tiempo** y el disco frena ahí, siempre igual: en cada
reproducción, tantas veces como la revises, y en el render final.

### Controles

| Control | Qué hace | Rango | Por defecto |
|---|---|---|---|
| **PARAR AQUÍ** | Pulsa mientras reproduces: guarda ese punto de la línea de tiempo como punto de parada. | — | — |
| **Punto de parada** | Código de tiempo donde empieza a frenar. Se puede escribir (`HH:MM:SS:FF` y Intro), ajustar con **-1 / +1** fotograma o borrar con **Quitar**. Se guarda con el proyecto. | — | sin punto |
| **Frenado** | Cuánto tarda el disco en pararse del todo. | 0,1 – 10 s | 2,5 s |
| **Curva** | Forma del frenado. `1` = natural (fricción real, pérdida de velocidad constante). Menos de 1 = *plato pesado* (aguanta la velocidad y cae al final). Más de 1 = *freno* (cae de golpe al principio y se arrastra al final). | 0,25 – 4 | 1 |
| **Oscurecer** | Filtro que va quitando agudos a medida que baja la velocidad (sonido más "apagado"). | 0 – 100 % | 40 % |
| **Desvanecer** | Cuánto baja el volumen junto con la velocidad. | 0 – 100 % | 50 % |

Sin punto de parada el plugin no cambia nada. Una vez parado el disco, la pista queda en
silencio hasta el final: si quieres que la música vuelva más adelante, corta el clip y deja
el resto en otra pista sin el efecto.

El disco dibujado en la interfaz gira a la velocidad real del efecto, así que se ve cómo frena.

### Cómo funciona por dentro

El audio que entra se guarda en un buffer indexado por su posición en la línea de tiempo.
Antes del punto de parada la salida es la señal original, sin cambios ni latencia. Desde el
punto de parada, un cabezal de lectura recorre ese buffer cada vez más despacio (de 100 % a
0 %), con interpolación cúbica, que es justo lo que le pasa a una aguja sobre un disco que
frena.

La posición del cabezal se calcula a partir de la posición en la línea de tiempo, no de lo que
sonó antes. Es importante porque DaVinci Resolve reinicia el plugin cada vez que pulsas
reproducir y solo le envía audio mientras reproduce:
un efecto que dependiera del historial solo se oiría bien la primera vez.

Si empiezas a reproducir en mitad del frenado, se oye el frenado desde ese punto; después del
frenado, silencio.

---

## Instalación

### Descargar el plugin compilado

Cada vez que se sube código a GitHub, el flujo **Compilar plugins** (pestaña *Actions* del
repositorio) genera el plugin para Windows, macOS y Linux. Entra en la ejecución más reciente
y descarga el artefacto de tu sistema (`TocadiscosStop-Windows`, `TocadiscosStop-macOS` o
`TocadiscosStop-Linux`).

Copia `Tocadiscos Stop.vst3` en la carpeta de plugins VST3 de tu sistema:

| Sistema | Carpeta |
|---|---|
| Windows | `C:\Program Files\Common Files\VST3\` |
| macOS | `/Library/Audio/Plug-Ins/VST3/` (y `Tocadiscos Stop.component` en `/Library/Audio/Plug-Ins/Components/`) |
| Linux | `~/.vst3/` |

**Windows:** si la carpeta `VST3` no existe (es normal si nunca has instalado un plugin VST3),
créala tú. En Windows en español el Explorador muestra la ruta como *Archivos de programa →
Archivos comunes*, pero es la misma carpeta. También puedes crearla desde PowerShell abierto
como administrador:

```powershell
New-Item -ItemType Directory -Force "C:\Program Files\Common Files\VST3"
```

`Tocadiscos Stop.vst3` es una **carpeta**, no un archivo suelto: cópiala entera.

**macOS:** el plugin no está firmado con un certificado de Apple, así que es posible que el
sistema lo bloquee. Quita la marca de cuarentena desde el Terminal:

```bash
sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/Tocadiscos Stop.vst3"
```

### Activarlo en DaVinci Resolve Studio

1. **DaVinci Resolve → Preferencias → Sistema → Audio Plugins** (en Windows: *DaVinci Resolve
   → Preferences → System → Audio Plugins*).
2. Asegúrate de que **VST** está activado y de que la carpeta de VST3 anterior está en la lista.
3. Reinicia Resolve. El efecto aparece como **Tocadiscos Stop** en la biblioteca de efectos,
   dentro de **Audio FX → VST**.

---

## Uso en Resolve

1. En la página **Fairlight**, en el mezclador, pulsa **+** en la sección *Effects* de la pista
   de música y elige **Tocadiscos Stop** (también puedes arrastrarlo desde la biblioteca a la
   cabecera de la pista).
2. Reproduce unos segundos antes de donde quieres el efecto y pulsa **PARAR AQUÍ** en la
   ventana del plugin en el momento en que quieres que el disco empiece a frenar.
3. Vuelve atrás y reproduce: el disco frena en ese punto cada vez.
4. Ajusta el punto con **-1 / +1** (un fotograma) o escribe el código de tiempo exacto, y
   ajusta **Frenado**, **Curva**, **Oscurecer** y **Desvanecer** a tu gusto.

El punto de parada es una posición de la línea de tiempo: si mueves el clip de música, vuelve a
marcarlo.

### Consejos

- **Frenado natural de tocadiscos:** Frenado 1,5–2,5 s, Curva 1.
- **"Tape stop" rápido de DJ:** Frenado 0,4–0,8 s, Curva 1,5–2.
- **Disco pesado que se resiste:** Frenado 3–4 s, Curva 0,5.
- Pon **Oscurecer** y **Desvanecer** a 0 si quieres solo el cambio de velocidad puro.

---

## Compilar desde el código

Necesitas CMake 3.22+ y un compilador de C++17 (Visual Studio 2022 en Windows, Xcode en macOS,
GCC/Clang en Linux). JUCE se descarga automáticamente.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

El plugin queda en `build/plugins/TocadiscosStop/TocadiscosStop_artefacts/Release/`.

En Linux instala antes las dependencias de JUCE:

```bash
sudo apt install libasound2-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev
```

### Prueba offline

`tests/render_test.cpp` aplica el efecto a una música sintética (o a tu propio WAV) sin
necesidad de Resolve y guarda el resultado para escucharlo:

```bash
g++ -std=c++17 -O2 -I plugins/TocadiscosStop/Source tests/render_test.cpp -o render_test
./render_test resultado.wav              # música de prueba generada
./render_test resultado.wav mi_cancion.wav  # tu propio archivo (WAV 16/24 bits o float)
```

Pone el punto de parada al 35 % del audio.

## Estructura

```
plugins/TocadiscosStop/Source/
  TurntableEngine.h    motor DSP (sin dependencias, reutilizable)
  PluginProcessor.*    parámetros, punto de parada y posición en la línea de tiempo
  Timecode.h           conversión entre muestras y código de tiempo
  PluginEditor.*       interfaz con el disco giratorio
tests/render_test.cpp     prueba offline del motor (genera un WAV)
tests/processor_test.cpp  prueba del plugin imitando a DaVinci Resolve
```

## Licencia

Los plugins usan [JUCE](https://juce.com), que se distribuye bajo AGPLv3 o licencia comercial.
Para uso personal no hay problema; si vas a distribuir o vender los plugins, revisa las
condiciones de licencia de JUCE.

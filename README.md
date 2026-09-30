# Elmio · Efectos para DaVinci Resolve Studio

Efectos de audio editables para usar en las pistas de audio de **DaVinci Resolve Studio**
(página Fairlight y página Edit). Se distribuyen como plugins **VST3** (y **AU** en macOS),
que es el formato de plugins de audio que admite Resolve Studio.

## 1. Tocadiscos Stop

Simula que pulsas el botón de parada de un tocadiscos: el plato pierde velocidad poco a poco,
la música baja de tono y de tempo a la vez y se apaga hasta el silencio. Al soltar el botón el
disco vuelve a arrancar.

### Parámetros (todos editables y automatizables)

| Control | Qué hace | Rango | Por defecto |
|---|---|---|---|
| **PARAR** | Activado: el disco frena hasta pararse. Desactivado: el disco vuelve a girar. | apagado / encendido | apagado |
| **Frenado** | Cuánto tarda el disco en pararse del todo. | 0,1 – 10 s | 1,5 s |
| **Curva** | Forma del frenado. `1` = natural (fricción real, pérdida de velocidad constante). Menos de 1 = *plato pesado* (aguanta la velocidad y cae al final). Más de 1 = *freno* (cae de golpe al principio y se arrastra al final). | 0,25 – 4 | 1 |
| **Arranque** | Cuánto tarda en recuperar la velocidad al desactivar PARAR. `0` = vuelve al instante. | 0 – 5 s | 0,6 s |
| **Oscurecer** | Filtro que va quitando agudos a medida que baja la velocidad (sonido más "apagado"). | 0 – 100 % | 40 % |
| **Desvanecer** | Cuánto baja el volumen junto con la velocidad. | 0 – 100 % | 30 % |

El disco dibujado en la interfaz gira a la velocidad real del efecto, así que se ve cómo frena.

### Cómo funciona por dentro

El audio que entra se guarda en un buffer. Mientras PARAR está apagado la salida es la señal
original, sin ningún cambio ni latencia. Al activar PARAR, un cabezal de lectura recorre ese
buffer cada vez más despacio (de 100 % a 0 %), con interpolación cúbica, que es justo lo que le
pasa a una aguja sobre un disco que frena. Al volver a arrancar, cuando el disco alcanza la
velocidad normal, se hace un fundido de 30 ms de vuelta al audio en directo.

Si empiezas a reproducir (o saltas) a un punto de la línea de tiempo donde PARAR ya está
activado, el efecto empieza directamente en silencio, sin volver a hacer el frenado.

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

### En Fairlight, con automatización

1. En la página **Fairlight**, en el mezclador, pulsa **+** en la sección *Effects* de la pista
   de música y elige **Tocadiscos Stop** (también puedes arrastrarlo desde la biblioteca a la
   cabecera de la pista).
2. Ajusta **Frenado**, **Curva**, **Oscurecer** y **Desvanecer** a tu gusto.
3. Activa la barra de **Automatización** de Fairlight, habilita la automatización de plugins y
   pon la pista en modo **Touch** o **Latch**.
4. Reproduce y pulsa **PARAR** en la ventana del plugin justo en el momento en que quieres que
   el disco empiece a frenar. Resolve graba ese momento como automatización.
5. Si quieres que la música vuelva, pulsa otra vez **PARAR** para soltarlo.

Después puedes mover el punto de automatización en la línea de tiempo para ajustar el momento
exacto, o dibujarlo a mano en el carril de automatización del parámetro **Parar**.

### Solo al final de la canción

Basta con un único cambio de automatización: **Parar** apagado antes del punto elegido y
encendido desde ahí hasta el final. Deja **Arranque** como quieras, no se usará.

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

Activa PARAR al 35 % del audio y lo suelta al 75 %.

## Estructura

```
plugins/TocadiscosStop/Source/
  TurntableEngine.h    motor DSP (sin dependencias, reutilizable)
  PluginProcessor.*    parámetros, automatización y sincronización con el transporte
  PluginEditor.*       interfaz con el disco giratorio
tests/render_test.cpp  prueba offline
```

## Licencia

Los plugins usan [JUCE](https://juce.com), que se distribuye bajo AGPLv3 o licencia comercial.
Para uso personal no hay problema; si vas a distribuir o vender los plugins, revisa las
condiciones de licencia de JUCE.

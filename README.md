![Streamerdesk ASMR Suite](branding/banner.jpg)

# Streamerdesk ASMR Suite

**[Español](#español)** · **[English](#english)**

Filtros de audio **gratuitos y libres** para **OBS Studio 28 o posterior**, pensados para creadoras y creadores de ASMR. Cada efecto es un plugin independiente: los instalas todos o solo los que quieras.

Las descargas oficiales (instalador y zip, Windows, macOS y Linux) están en **[streamerdesk.com](https://streamerdesk.com)**. Este repositorio es el código fuente, para que cualquiera pueda verlo, auditarlo, compilarlo y, si lo desea, firmar los `.exe` con un certificado de código abierto (por ejemplo el programa gratuito de [SignPath Foundation](https://signpath.org/)).

> Software libre de **Streamerdesk**, bajo licencia **GPL-2.0-or-later** (necesaria por enlazar con libobs). Ver [LICENSE](LICENSE).
> *Free software by Streamerdesk, licensed GPL-2.0-or-later (required because the plugins link to libobs). See [LICENSE](LICENSE).*

---

## Español

### Qué puede hacer cada efecto

Todos funcionan sobre **cualquier fuente de audio** de OBS. Están pensados sobre todo para micros USB y de condensador (Blue Yeti, Rode y similares), que suelen ser mono: el filtro suma la entrada a mono y saca un **estéreo** pensado para **escuchar con auriculares**. Cada control tiene un iconito **(?)** que explica qué hace y para qué sirve. Traen presets de fábrica para que oigas el efecto al momento, y puedes guardar los tuyos con nombre: se quedan **solo en tu ordenador**, sin exportar ni importar.

- **Eco Ping-Pong** (v1.0.1) — Un eco estéreo que rebota de oreja a oreja, inspirado en el clásico TAL-Dub versión 1, con sincronía al tempo/BPM y modo binaural.
- **Reverb** (v1.0.1) — Una reverb suave y envolvente: de habitación íntima a catedral etérea, con ducking para que la sala se aparte mientras hablas.
- **8D / Órbita** (v1.0.0) — Tu voz gira despacio alrededor de la cabeza, con un modelo binaural de cabeza y un toque de sala.
- **Susurro de oído a oído** (v1.0.0) — La voz viaja suavemente de una oreja a la otra, con modo binaural.
- **Proximidad** (v1.0.0) — Acerca tu voz al oído (graves cálidos, mucho cuerpo) o aléjala al otro lado de la habitación.
- **Espacialidad binaural** (v1.0.0) — Coloca tu voz delante, detrás, encima o junto a un oído, y la deslizas de un sitio a otro sin clics.

Más detalle, presets y ayudas de cada efecto: `plugins/<efecto>/README.md`.

### Cómo se instala (paquetes oficiales)

No hace falta compilar nada. En [streamerdesk.com](https://streamerdesk.com) tienes:

- **Windows 10/11 (64 bits):** instalador `.exe` de la suite (elige qué efectos instalar; vienen todos marcados) y el `.zip` equivalente. También hay instaladores sueltos de cada efecto.
- **macOS 11+** (Apple Silicon e Intel): zip universal con `Instalar - Install.command`. La primera vez el Mac avisará de que no reconoce al desarrollador: clic derecho → Abrir. El LEEME del zip lo explica paso a paso.
- **Linux x86_64** (glibc 2.28+, OBS normal o Flatpak): se descomprime el tar.gz y se ejecuta `install.sh`. No pide contraseña de administrador.

Compatible con **OBS 28 y posteriores** (un solo binario por sistema). Tus presets se conservan al actualizar o desinstalar.

### Cómo compilar desde el código

Requisitos en Linux (también es así como se generan los paquetes oficiales, con Zig como compilador cruzado): `python3`, `pip` (`ziglang`, `pefile`, `macholib`, `pillow`), `git`, `curl`, `tar`, `ar`. `makensis` (NSIS) es opcional y se descarga solo si no está. Las cabeceras de libobs **28.0.0** y los stubs de enlace se obtienen automáticamente (se quedan en `.deps/`, que no forma parte del repositorio).

```bash
pip3 install --user ziglang pefile macholib pillow

# Un efecto, para Windows, macOS y Linux
bash scripts/build-all.sh plugins/pingpong-echo          # → dist/

# Los seis efectos para Windows + instaladores individuales + instalador de la suite 1.0.2
bash scripts/build-windows-suite.sh                      # → dist/

# Suite completa para macOS universal (arm64 + x86_64)
bash scripts/build-macos-suite.sh                        # → dist-macos/

# Suite completa para Linux x86_64
bash scripts/build-linux-suite.sh                        # → dist-linux/
```

Compilación nativa alternativa (MSVC / Xcode / gcc), si ya tienes libobs:

```bash
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release \
  -DOBS_INCLUDE_DIR=/ruta/a/obs-studio/libobs -DOBS_LIBRARY=/ruta/a/libobs
cmake --build build-native --config Release
```

### Estructura del repositorio

```
common/                    núcleo compartido (DSP, presets 1.0.1, plantillas de instalador)
plugins/pingpong-echo/     Eco Ping-Pong
plugins/reverb/            Reverb
plugins/8d-orbit/          8D / Órbita (reutiliza el motor de sala de la Reverb)
plugins/ear-to-ear/        Susurro de oído a oído  (+ common/ propio, ver docs/SOURCES.md)
plugins/proximity/         Proximidad              (+ common/ propio)
plugins/binaural/          Espacialidad binaural   (+ common/ propio)
branding/                  logo, banner e imágenes del instalador
scripts/build-all.sh       un efecto, tres sistemas
scripts/build-windows-suite.sh
linux/  macos/  suite-bundle/   paquetes de la suite por sistema
.github/workflows/         compilación de los instaladores de Windows en GitHub Actions
```

Tres efectos (Susurro, Proximidad y Espacialidad) se publicaron con una copia anterior de `common/`. Esa copia viaja con ellos, sin cambiar el sonido, los presets, la interfaz ni los textos, para que una recompilación coincida con lo que ya se descargó de streamerdesk.com. El detalle está en [docs/SOURCES.md](docs/SOURCES.md).

---

## English

### What each effect can do

Every effect works on **any OBS audio source**. They are made with USB and condenser mics in mind (Blue Yeti, Rode and similar), which are often mono: the filter sums the input to mono and outputs **stereo** meant for **headphones**. Every control has a **(?)** tip that explains what it does and what it is for. Built-in presets let you hear the effect straight away, and you can save your own named presets: they stay **on your computer only**, with no export or import.

- **Ping-Pong Echo** (v1.0.1) — A stereo echo that bounces from ear to ear, inspired by classic TAL-Dub version 1, with tempo/BPM sync and a binaural mode.
- **Reverb** (v1.0.1) — A soft, enveloping reverb, from an intimate room to an ethereal cathedral, with ducking so the room steps aside while you speak.
- **8D / Orbit** (v1.0.0) — Your voice slowly circles the listener's head, with a binaural head model and a touch of room.
- **Ear-to-ear whisper** (v1.0.0) — Your voice glides gently from one ear to the other, with a binaural mode.
- **Proximity** (v1.0.0) — Bring your voice right up to the ear (warm bass, lots of body) or move it across the room.
- **Binaural spatializer** (v1.0.0) — Place your voice in front, behind, above or at one ear, and glide it from one spot to another without clicks.

More detail, presets and help for each effect: `plugins/<effect>/README.md`.

### How to install (official packages)

You do not need to build anything. Official downloads are at **[streamerdesk.com](https://streamerdesk.com)**:

- **Windows 10/11 (x64):** suite `.exe` installer (pick which effects to install; all are checked by default) and the matching `.zip`. Standalone installers for each effect are also available.
- **macOS 11+** (Apple Silicon and Intel): universal zip with `Instalar - Install.command`. The first time, macOS will warn that the developer is unidentified: right-click → Open. The README inside the zip walks you through it.
- **Linux x86_64** (glibc 2.28+, regular or Flatpak OBS): unpack the tar.gz and run `install.sh`. No admin password required.

Compatible with **OBS 28 and later** (one binary per platform). Your presets are kept when you update or uninstall.

### How to build from source

On Linux (this is also how the official packages are made, using Zig as a cross-compiler) you need `python3`, `pip` (`ziglang`, `pefile`, `macholib`, `pillow`), `git`, `curl`, `tar`, `ar`. `makensis` (NSIS) is optional and will be fetched if missing. libobs **28.0.0** headers and link stubs are downloaded automatically into `.deps/` (not part of the repository).

```bash
pip3 install --user ziglang pefile macholib pillow

# One effect, for Windows, macOS and Linux
bash scripts/build-all.sh plugins/pingpong-echo          # → dist/

# All six effects for Windows + individual installers + the 1.0.2 suite installer
bash scripts/build-windows-suite.sh                      # → dist/

# Full macOS universal suite (arm64 + x86_64)
bash scripts/build-macos-suite.sh                        # → dist-macos/

# Full Linux x86_64 suite
bash scripts/build-linux-suite.sh                        # → dist-linux/
```

Alternative native build (MSVC / Xcode / gcc) if you already have libobs:

```bash
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release \
  -DOBS_INCLUDE_DIR=/path/to/obs-studio/libobs -DOBS_LIBRARY=/path/to/libobs
cmake --build build-native --config Release
```

### Repository layout

See the Spanish section above for the directory tree. Ear-to-ear, Proximity and the Binaural spatializer ship with their own copy of `common/` (the one they were originally built with), so a rebuild can match the binaries already published on streamerdesk.com. Details are in [docs/SOURCES.md](docs/SOURCES.md).

## Política de firma de código / Code signing policy

**Free code signing provided by [SignPath.io](https://signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).**

Estamos tramitando la firma gratuita de los instaladores oficiales de Windows con SignPath Foundation. Hasta que se publique la primera versión firmada, esos `.exe` no llevan firma digital. El detalle, los roles del equipo y la política de privacidad están en [CODE_SIGNING_POLICY.md](CODE_SIGNING_POLICY.md).

We are applying for free Windows code signing through SignPath Foundation. Until the first signed release is published, official Windows installers are unsigned. Roles, process and the privacy statement are in [CODE_SIGNING_POLICY.md](CODE_SIGNING_POLICY.md).

## Licence

**GPL-2.0-or-later**. Free software by Streamerdesk. See [LICENSE](LICENSE).

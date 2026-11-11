![Streamerdesk ASMR Suite](../../branding/banner.jpg)

# Streamerdesk ASMR – Susurro de oído a oído · v1.0.0

**[Español](#español)** · **[English](#english)**

Filtro de audio para **OBS Studio 28 o posterior** (Windows, macOS, Linux) · Audio filter for **OBS Studio 28 or newer**.
Web: **https://streamerdesk.com**

> **Software libre** de **Streamerdesk**, gratuito, bajo licencia **GPL-2.0-or-later** (ver `LICENSE.txt`).
> **Free software** by **Streamerdesk**, free of charge, licensed **GPL-2.0-or-later** (see `LICENSE.txt`).

---

## Español

### ¿Qué hace?
Hace que tu voz **viaje despacito de una oreja a la otra**, como si te susurraran pasando de un lado a otro de la cabeza. Está pensado para escuchar con **auriculares** y para **micrófonos USB/condensador** (Blue Yeti, Rode NT-USB, Rode PodMic USB…), que suelen grabar en mono o en “mono duplicado”: el filtro suma la entrada a mono y saca un **estéreo de verdad**.

Además del panorama clásico, tiene un modo **binaural** que imita lo que hace tu cabeza: la oreja lejana oye la voz un poquito más tarde (hasta 0,7 ms), algo más baja y con menos agudos. Con auriculares se siente mucho más real y cercano. Todo se mueve de forma suave, sin clics ni saltos.

### Compatibilidad
| Sistema | Archivo | OBS |
|---|---|---|
| Windows 10/11 x64 | `StreamerdeskASMR-SusurroOidoAOido-1.0.0-windows-installer.exe` (instalador) o `…-windows.zip` (manual) | 28.0 → última |
| macOS 11+ (Apple Silicon e Intel, universal) | `StreamerdeskASMR-SusurroOidoAOido-1.0.0-macos-universal.zip` | 28.0 → última |
| Linux x86_64 (Ubuntu 20.04+, Debian, Fedora, Arch, Flatpak) | `StreamerdeskASMR-SusurroOidoAOido-1.0.0-linux-x86_64.tar.gz` | 28.0 → última |

**Un único archivo por sistema sirve para todas las versiones desde OBS 28.0 hasta la última**: se compila con las cabeceras de libobs 28.0 y solo usa funciones que ya existían en OBS 28 (comprobado contra el `obs.dll` de OBS 28.0 y de OBS 33.0.1). No hace falta una versión distinta para OBS 28, 29, 30, 31, 32 o 33.

### Instalación

**Windows – instalador (recomendado)**
1. Cierra OBS Studio.
2. Ejecuta `StreamerdeskASMR-SusurroOidoAOido-1.0.0-windows-installer.exe`. Encuentra solo la carpeta de OBS (normalmente `C:\Program Files\obs-studio`). El instalador sale en español o en inglés según el idioma de tu Windows.
3. Si Windows SmartScreen avisa (“Windows protegió su PC”), pulsa **Más información → Ejecutar de todas formas** (el instalador no está firmado con un certificado de pago).
4. Para desinstalar: *Configuración → Aplicaciones → “Streamerdesk ASMR - Susurro de oído a oído (OBS Studio)” → Desinstalar*.

**Windows – manual (.zip)**
1. Cierra OBS. 2. Descomprime el zip. 3. Copia las carpetas `obs-plugins` y `data` dentro de `C:\Program Files\obs-studio\` (acepta combinar carpetas).
Quedará: `obs-plugins\64bit\streamerdesk-asmr-ear-to-ear.dll` y `data\obs-plugins\streamerdesk-asmr-ear-to-ear\locale\*.ini`.

**macOS**
1. Cierra OBS. 2. Descomprime el zip. 3. Haz doble clic en **`Instalar - Install.command`** (si macOS lo bloquea: clic derecho → Abrir → Abrir).
   Copia `streamerdesk-asmr-ear-to-ear.plugin` a `~/Library/Application Support/obs-studio/plugins/` y quita la cuarentena de Gatekeeper.
   *Manual:* copia el `.plugin` a esa carpeta y ejecuta en Terminal: `xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/streamerdesk-asmr-ear-to-ear.plugin`

**Linux**
```bash
tar xzf StreamerdeskASMR-SusurroOidoAOido-1.0.0-linux-x86_64.tar.gz
cd StreamerdeskASMR-SusurroOidoAOido-1.0.0-linux && ./install.sh
```
Instala en `~/.config/obs-studio/plugins/streamerdesk-asmr-ear-to-ear/` (`bin/64bit/*.so` + `data/`) y, si usas el Flatpak de OBS, también en `~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/`.

### Uso
1. En el **Mezclador de audio** de OBS, clic en ⋮ (o clic derecho) sobre tu micrófono → **Filtros**.
2. Botón **+** → **Streamerdesk ASMR - Susurro de oído a oído**.
3. Elige un preset y ajústalo a tu gusto. Cada control tiene un icono **(?)** con una explicación.

**Importante para micros mono (Yeti, Rode…):**
- En *Propiedades de audio avanzadas* del micrófono, **no** actives **“Mezclar a mono” (Downmix to Mono)**: OBS volvería a juntar los dos lados y se perdería el movimiento.
- En *Ajustes → Audio*, los **Canales** deben estar en **Estéreo** (lo normal).
- Escúchalo con **auriculares** (*Propiedades de audio avanzadas → Monitorización de audio → “Monitorizar y emitir”*).
- Si combinas filtros de la suite, pon este **antes** del Eco Ping-Pong o de la reverb para que el eco siga a la voz.

### Controles

**Presets**
| Control | Qué hace |
|---|---|
| Preset | ★ incluidos y 👤 tuyos. Se aplica al elegirlo. |
| Cargar preset seleccionado | Vuelve a cargarlo (deshace tus cambios). |
| Nombre para mi preset / Guardar preset | Guarda los ajustes actuales con ese nombre (si ya existe, lo actualiza). |
| Borrar preset | Borra el preset de usuario elegido (los ★ no se pueden borrar). |
| Restablecer valores por defecto | Vuelve a los valores iniciales. |

Presets incluidos: *Susurro suave (por defecto)*, *Lento de oreja a oreja*, *Cosquillas rápidas*, *Paseo orgánico*, *Pegado a la oreja izquierda*, *Pegado a la oreja derecha*, *Al ritmo (70 BPM)*, *Amplio y muy cercano*.

Tus presets se guardan en tu ordenador, se conservan al reiniciar OBS y están en todas tus escenas y fuentes:
Windows `%APPDATA%\obs-studio\plugin_config\streamerdesk-asmr-ear-to-ear\presets.json` · macOS `~/Library/Application Support/obs-studio/plugin_config/streamerdesk-asmr-ear-to-ear/presets.json` · Linux `~/.config/obs-studio/plugin_config/streamerdesk-asmr-ear-to-ear/presets.json`.

**Movimiento**
| Control | Rango | Defecto |
|---|---|---|
| Modo de movimiento | Manual / Automático (ida y vuelta) / Aleatorio-orgánico / Sincronizado al tempo | Automático |
| Posición *(modo manual)* | −100 (izquierda) … 0 (centro) … +100 (derecha) | 0 |
| Velocidad en *(auto/aleatorio)* | Hercios / Segundos por recorrido | Segundos |
| Velocidad *(Hz)* | 0,01 – 4 Hz (ciclos completos ida y vuelta por segundo) | 0,125 Hz |
| Duración de cada recorrido *(segundos)* | 0,25 – 60 s (de una oreja a la otra) | 4 s |
| Tempo *(modo tempo)* | 30 – 300 BPM | 70 BPM |
| Duración del recorrido *(modo tempo)* | 1 negra, 1 blanca, blanca con puntillo, 1, 2, 4 u 8 compases | 1 compás |
| Anchura del recorrido | 0 – 100 % | 70 % |
| Forma del movimiento *(auto/tempo)* | Senoidal / Pausa suave en cada oreja | Pausa suave |
| Tiempo de pausa en cada oreja | 0 – 90 % del ciclo | 30 % |
| Empieza por *(auto/tempo)* | Oreja izquierda / derecha | Izquierda |

**Espacio y cercanía**
| Control | Rango | Defecto |
|---|---|---|
| Tipo de panorama | Simple (equal-power) / Binaural (auriculares) | Binaural |
| Intensidad binaural | 0 – 100 % (ITD hasta 0,7 ms · ILD de 4 a 20 dB · sombra de agudos hasta 1,5 kHz) | 60 % |
| Cercanía a la oreja | 0 – 100 % (hasta +4,5 dB de calidez a ~220 Hz en la oreja cercana) | 30 % |
| Mantener en el centro | 0 – 100 % | 0 % |
| Suavizar el paso por el centro | 0 – 6 dB | 0 dB |

**Salida**
| Control | Rango | Defecto |
|---|---|---|
| Ganancia de salida | −24 … +12 dB | 0 dB |

En el centro la voz suena exactamente igual que sin filtro; hacia los lados se mantiene la misma potencia total (la oreja cercana puede subir hasta +3 dB). Todos los cambios se suavizan, sin clics. Con fuentes 5.1/7.1 se procesan los canales frontales y el resto pasa sin cambios.

### Notas
- Instalador y plugin sin firma de pago: Windows SmartScreen o macOS Gatekeeper pueden avisar la primera vez (ver pasos arriba).
- Desinstalar en macOS/Linux: borra la carpeta/bundle `streamerdesk-asmr-ear-to-ear` de la carpeta de plugins.

---

## English

### What it does
Your voice **glides gently from one ear to the other**, like a whisper moving around your head. Made for **headphones** and **USB/condenser mics** (Blue Yeti, Rode…), which are usually mono or dual-mono: the input is summed to mono and the output is **true stereo**. Besides classic panning there is a **binaural** mode that mimics your head (the far ear hears the voice up to 0.7 ms later, a bit quieter and darker). Everything moves smoothly, no clicks.

### Compatibility
One binary per OS works on **OBS Studio 28.0 up to the latest**: Windows 10/11 x64 (`…-windows-installer.exe` or `…-windows.zip`), macOS 11+ universal (Apple Silicon + Intel, `…-macos-universal.zip`), Linux x86_64 (`…-linux-x86_64.tar.gz`, also Flatpak). Built against libobs 28.0 headers and only uses API available since OBS 28 (checked against the `obs.dll` of OBS 28.0 and OBS 33.0.1). No separate builds per OBS version are needed.

### Install
- **Windows (installer):** close OBS, run the `.exe` (auto-detects the OBS folder, Spanish/English UI by system language, uninstall from *Settings → Apps*). If SmartScreen warns: *More info → Run anyway*.
- **Windows (manual zip):** close OBS and copy the `obs-plugins` and `data` folders into `C:\Program Files\obs-studio\`.
- **macOS:** unzip and double-click **`Instalar - Install.command`** (if blocked: right-click → Open). It copies the `.plugin` to `~/Library/Application Support/obs-studio/plugins/` and clears the Gatekeeper quarantine.
- **Linux:** `tar xzf …-linux-x86_64.tar.gz && cd …-linux && ./install.sh` (installs to `~/.config/obs-studio/plugins/` and the Flatpak config if present).

### Use
Audio Mixer → ⋮ on your mic → **Filters** → **+** → **Streamerdesk ASMR - Ear to Ear Whisper** (the name follows OBS's UI language). Don't enable *Downmix to Mono* on the mic, keep OBS audio channels on *Stereo*, listen on headphones.

### Controls (range · default)
- **Presets:** preset list (★ built-in, 👤 yours), Load, Name + Save, Delete, Reset to defaults. User presets are stored in `plugin_config/streamerdesk-asmr-ear-to-ear/presets.json` and survive restarts.
- **Movement:** Mode (Manual / Automatic / Random-organic / Tempo · Automatic), Position (−100…+100 · 0), Speed unit (Hz / seconds · seconds), Speed (0.01–4 Hz · 0.125), Sweep duration (0.25–60 s · 4 s), Tempo (30–300 BPM · 70), Sweep length (¼ note … 8 bars · 1 bar), Sweep width (0–100 % · 70 %), Shape (Sine / Smooth hold · hold), Hold time (0–90 % · 30 %), Start at (Left / Right · Left).
- **Space:** Panning (Equal-power / Binaural · Binaural), Binaural intensity (0–100 % · 60 %; ITD ≤ 0.7 ms, ILD 4–20 dB, head shadow down to 1.5 kHz), Close to the ear (0–100 % · 30 %), Keep in center (0–100 % · 0), Soften center crossing (0–6 dB · 0).
- **Output:** Output gain (−24…+12 dB · 0).

---

## Para desarrolladores / For developers
- Código: `plugins/ear-to-ear/src/ear_dsp.c` (DSP sin dependencias de OBS), `plugin-main.c` (filtro OBS), `common/` (DSP y presets comunes de la suite).
- Pruebas offline del DSP: `cc -O2 -Icommon/dsp plugins/ear-to-ear/test/test_ear.c plugins/ear-to-ear/src/ear_dsp.c -lm && ./a.out`
- Compilar y empaquetar los 3 sistemas desde Linux: `bash scripts/build-all.sh plugins/ear-to-ear` (Zig como compilador cruzado, NSIS para el instalador).
- Alternativa nativa: `CMakeLists.txt`. CI opcional: `.github/workflows/build.yml`.

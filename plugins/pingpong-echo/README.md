![Streamerdesk ASMR Suite](../../branding/banner.jpg)

# Streamerdesk ASMR – Eco Ping-Pong · v1.0.1

**[Español](#español)** · **[English](#english)**

Filtro de audio para **OBS Studio 28 o posterior** (Windows, macOS, Linux) · Audio filter for **OBS Studio 28 or newer**.
Web: **https://streamerdesk.com**

> **Software libre** de **Streamerdesk**, gratuito, bajo licencia **GPL-2.0-or-later** (ver `LICENSE.txt`).
> **Free software** by **Streamerdesk**, free of charge, licensed **GPL-2.0-or-later** (see `LICENSE.txt`).

---

## Español

### ¿Qué hace?
Crea un **eco que rebota de oreja a oreja** (izquierda → derecha → izquierda…) sobre cualquier fuente de audio de OBS. Pensado sobre todo para **micrófonos USB/condensador** (Blue Yeti, Rode NT-USB, Rode PodMic USB…), que suelen ser mono o “mono duplicado”: el filtro suma la entrada a mono y genera una salida **estéreo** con el rebote, perfecta para escuchar con auriculares.

El carácter y los controles están inspirados en el clásico gratuito **TAL-Dub (versión 1, 2007)** de TAL Software: filtro resonante en la realimentación, corte de graves, limitador cálido, tiempos y realimentación por canal con LINK, SWAP L/R, FILTER FB, IN y DRY/WET.

### Compatibilidad
| Sistema | Archivo | OBS |
|---|---|---|
| Windows 10/11 x64 | `StreamerdeskASMR-EcoPingPong-1.0.1-windows-installer.exe` (instalador) o `…-windows.zip` (manual) | 28.0 → última |
| macOS 11+ (Apple Silicon e Intel, universal) | `StreamerdeskASMR-EcoPingPong-1.0.0-macos-universal.zip` (versión 1.0.0; la 1.0.1 de momento solo para Windows) | 28.0 → última |
| Linux x86_64 (Ubuntu 18.04+, Debian, Fedora, Arch, Flatpak) | `StreamerdeskASMR-EcoPingPong-1.0.0-linux-x86_64.tar.gz` (versión 1.0.0; la 1.0.1 de momento solo para Windows) | 28.0 → última |

Un único binario por sistema sirve para todas las versiones desde OBS 28.0: se compila con las cabeceras de libobs 28.0 y solo usa funciones que ya existían en OBS 28 (comprobado contra el `obs.dll` de OBS 28.0 y de la última versión).

### Instalación

**Windows – instalador (recomendado)**
1. Cierra OBS Studio.
2. Ejecuta `StreamerdeskASMR-EcoPingPong-1.0.1-windows-installer.exe`. Detecta solo la carpeta de OBS (normalmente `C:\Program Files\obs-studio`). El instalador está en español o inglés según el idioma de Windows.
3. Si Windows SmartScreen avisa (“Windows protegió su PC”), pulsa **Más información → Ejecutar de todas formas** (el instalador no está firmado con certificado de pago).
4. Para desinstalar: *Configuración → Aplicaciones → “Streamerdesk ASMR - Eco Ping-Pong (OBS Studio)” → Desinstalar*.

**Windows – manual (.zip)**
1. Cierra OBS. 2. Descomprime el zip. 3. Copia las carpetas `obs-plugins` y `data` dentro de `C:\Program Files\obs-studio\` (acepta combinar carpetas).
Quedará: `obs-plugins\64bit\streamerdesk-asmr-pingpong-echo.dll` y `data\obs-plugins\streamerdesk-asmr-pingpong-echo\locale\*.ini`.

**macOS**
1. Cierra OBS. 2. Descomprime el zip. 3. Haz doble clic en **`Instalar - Install.command`** (si macOS lo bloquea: clic derecho → Abrir → Abrir).
   Esto copia `streamerdesk-asmr-pingpong-echo.plugin` a `~/Library/Application Support/obs-studio/plugins/` y quita la cuarentena de Gatekeeper.
   *Manual:* copia el `.plugin` a esa carpeta y ejecuta en Terminal: `xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/streamerdesk-asmr-pingpong-echo.plugin`

**Linux**
```bash
tar xzf StreamerdeskASMR-EcoPingPong-1.0.0-linux-x86_64.tar.gz
cd StreamerdeskASMR-EcoPingPong-1.0.0-linux && ./install.sh
```
Instala en `~/.config/obs-studio/plugins/streamerdesk-asmr-pingpong-echo/` (`bin/64bit/*.so` + `data/`) y, si usas el Flatpak de OBS, también en `~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/`.

### Uso
1. En el **Mezclador de audio** de OBS, clic en ⋮ (o clic derecho) sobre tu micrófono → **Filtros**.
2. Botón **+** → **Streamerdesk ASMR - Eco Ping-Pong**.
3. Elige un preset y ajusta a tu gusto. Cada control tiene un icono **(?)** con una explicación.

**Importante para micros mono (Yeti, Rode…):**
- Deja **Entrada = “Suma mono”** (por defecto).
- En *Propiedades de audio avanzadas* del micrófono, **no** actives **“Mezclar a mono” (Downmix to Mono)**: si lo activas, OBS convierte la fuente a mono y se pierde el rebote izquierda/derecha.
- En *Ajustes → Audio*, los **Canales** deben ser **Estéreo** (es lo normal). Si OBS está en Mono, el filtro funciona pero no puede haber rebote.
- Escúchalo con **auriculares** (en *Propiedades de audio avanzadas → Monitorización de audio*, “Monitorizar y emitir”), y recuerda que tu directo/grabación sí lleva el estéreo.

### Controles
Agrupados en la ventana del filtro. Los marcados con **TAL** vienen del panel de TAL-Dub 1.

**Presets**
| Control | Qué hace |
|---|---|
| Preset | Lista de presets: ★ incluidos (ASMR suave, Dub clásico, Rebote amplio, Binaural envolvente) y 👤 tuyos. Se aplica al elegirlo. |
| Cargar preset seleccionado | Vuelve a cargarlo (deshace tus cambios). |
| Nombre para mi preset / Guardar preset | Guarda los ajustes actuales con ese nombre (si existe, lo actualiza). |
| Borrar preset | Borra el preset de usuario seleccionado. |
| Restablecer valores por defecto | Vuelve a los valores iniciales (pensados para micros USB/condensador). |

Tus presets se guardan solo en tu ordenador (no se comparten ni exportan) y se conservan al reiniciar OBS:
Windows `%APPDATA%\obs-studio\plugin_config\streamerdesk-asmr-pingpong-echo\presets.json` · macOS `~/Library/Application Support/obs-studio/plugin_config/streamerdesk-asmr-pingpong-echo/presets.json` · Linux `~/.config/obs-studio/plugin_config/streamerdesk-asmr-pingpong-echo/presets.json`.

**Eco**
| Control | Rango | Defecto | Origen |
|---|---|---|---|
| Modo de tiempo | Manual (ms) / Tempo (BPM + figura) | Manual | TAL (selector de figura) |
| Enlazar tiempos izquierda/derecha (LINK) | sí/no | sí | TAL |
| Tiempo del eco (izquierda) | 1 – 4000 ms | 375 ms | TAL (DELAY L, máx. 4 s) |
| Tiempo del eco (derecha) *(visible sin LINK)* | 1 – 4000 ms | 375 ms | TAL (DELAY R) |
| Tempo *(modo Tempo)* | 30 – 300 BPM | 120 | — |
| Figura musical izq./der. *(modo Tempo)* | 1/32 … 2/1 (con puntillo y tresillo) | 1/8. | TAL |
| Enlazar realimentación (LINK) | sí/no | sí | TAL |
| Repeticiones / realimentación izq. (der. sin LINK) | 0 – 120 % | 45 % | TAL (FB L/R) |
| Limitador cálido en la realimentación | sí/no | sí | TAL (limitador/distorsión) |

**Estéreo / Binaural**
| Control | Rango | Defecto | Origen |
|---|---|---|---|
| Cantidad de ping-pong | 0 – 100 % (0 = eco estéreo normal, 100 = rebote total) | 100 % | nuevo |
| La primera repetición suena por | Izquierda / Derecha | Izquierda | nuevo |
| Entrada | Suma mono / Mantener estéreo | Suma mono | nuevo |
| Intercambiar izquierda/derecha (SWAP L/R) *(solo entrada estéreo)* | sí/no | no | TAL |

**Tono (filtro)**
| Control | Rango | Defecto | Origen |
|---|---|---|---|
| Brillo del eco / corte de agudos (DAMP) | 200 – 20000 Hz (20000 = apagado) | 4500 Hz | TAL (DAMP, filtro resonante 12 dB) |
| Resonancia (RESONANCE) | 0 – 100 % | 15 % | TAL |
| Corte de graves (LOW CUT) | 10 – 1000 Hz (10 = apagado) | 100 Hz | TAL |
| Filtro dentro de la realimentación (FILTER FB) | sí (en el bucle) / no (post FX) | sí | TAL |
| Filtrar también la voz original (IN) | sí/no | no | TAL |

**Mezcla**
| Control | Rango | Defecto | Origen |
|---|---|---|---|
| Mezcla seco / eco (DRY / WET) | 0 – 100 % (50 % = ambos al máximo) | 30 % | TAL (DRY/WET) |
| Balance izquierda / derecha | −100 … +100 % | 0 | nuevo |
| Volumen de salida | −24 … +12 dB | 0 dB | nuevo |

Los cambios de parámetros se suavizan (sin clics): el tiempo de eco se desliza como en una cinta.
Con fuentes 5.1/7.1 se procesan los canales frontales izquierdo/derecho y el resto pasa sin cambios.

### Notas
- Instalador y plugin sin firma de pago: Windows SmartScreen o macOS Gatekeeper pueden avisar la primera vez (ver pasos arriba).
- Desinstalar en macOS/Linux: borra la carpeta/bundle `streamerdesk-asmr-pingpong-echo` de la carpeta de plugins.

---

## English

### What it does
A **ping-pong echo that bounces ear to ear** (left → right → left…) on any OBS audio source. Tuned for **USB/condenser microphones** (Blue Yeti, Rode…), which are usually mono or dual-mono: the input is summed to mono and the output is a **stereo** bounce, made for headphones. Character and controls are inspired by the classic free **TAL-Dub (version 1, 2007)**.

### Compatibility
One binary per OS works on **OBS Studio 28.0 up to the latest**: Windows 10/11 x64 (`…-windows-installer.exe` or `…-windows.zip`), macOS 11+ universal (Apple Silicon + Intel, `…-macos-universal.zip`), Linux x86_64 (`…-linux-x86_64.tar.gz`, also Flatpak). Built against libobs 28.0 headers and only uses API available in OBS 28 (checked against OBS 28.0 and latest `obs.dll`).

### Install
- **Windows (installer):** close OBS, run the `.exe` (auto-detects the OBS folder, Spanish/English UI by system language, uninstall from *Settings → Apps*). If SmartScreen warns: *More info → Run anyway*.
- **Windows (manual zip):** close OBS and copy the `obs-plugins` and `data` folders into `C:\Program Files\obs-studio\`.
- **macOS:** unzip and double-click **`Instalar - Install.command`** (if blocked: right-click → Open). It copies the `.plugin` to `~/Library/Application Support/obs-studio/plugins/` and clears the Gatekeeper quarantine.
- **Linux:** `tar xzf …-linux-x86_64.tar.gz && cd …-linux && ./install.sh` (installs to `~/.config/obs-studio/plugins/` and the Flatpak config if present).

### Use
Audio Mixer → ⋮ on your mic → **Filters** → **+** → **Streamerdesk ASMR - Ping-Pong Echo**. Pick a preset and tweak; every control has a **(?)** help tooltip.
For mono mics keep **Input = Mono sum**, do **not** enable **Downmix to Mono** in Advanced Audio Properties, and keep OBS audio channels on **Stereo**.

### Controls
Same as the Spanish tables above (labels appear in English when OBS is in English):
Presets (select / load / name / save / delete / reset) · Echo: time mode, LINK, echo time L/R (1–4000 ms, 375), tempo 30–300 BPM + note value, feedback LINK, feedback L/R (0–120 %, 45 %), warm limiter · Stereo/Binaural: ping-pong amount (0–100 %, 100), first repeat side, input mode (mono sum / keep stereo), SWAP L/R · Tone: DAMP 200–20000 Hz (4500), RESONANCE 0–100 % (15), LOW CUT 10–1000 Hz (100), FILTER FB, IN · Mix: DRY/WET 0–100 % (30), balance −100…+100 %, output −24…+12 dB.

User presets are stored locally only (`plugin_config/streamerdesk-asmr-pingpong-echo/presets.json`) and survive restarts; no export/import.

---
© 2026 Streamerdesk · https://streamerdesk.com · GPL-2.0-or-later. TAL-Dub es un producto de TAL Software / TAL-Dub is a product of TAL Software. Este plugin es una obra independiente inspirada en sus controles y no contiene código de TAL / this plugin is an independent work inspired by its controls and contains no TAL code.

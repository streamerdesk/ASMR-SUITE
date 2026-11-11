![Streamerdesk ASMR Suite](../branding/banner.jpg)

# Streamerdesk ASMR Suite · v1.0.2 (Windows x64)

**[Español](#español)** · **[English](#english)**

Seis filtros de audio para **OBS Studio 28 o posterior** · Six audio filters for **OBS Studio 28 or newer**.
Web: **https://streamerdesk.com**

> **Software libre** de **Streamerdesk**, gratuito, bajo licencia **GPL-2.0-or-later** (ver `LICENSE.txt`).
> **Free software** by **Streamerdesk**, free of charge, licensed **GPL-2.0-or-later** (see `LICENSE.txt`).

---

## Español

### Los 6 efectos
| Efecto | Versión | Qué hace |
|---|---|---|
| **Eco Ping-Pong** | 1.0.1 | Un eco estéreo que rebota de oreja a oreja, inspirado en TAL-Dub. |
| **Reverb** | 1.0.1 | Una reverb suave y envolvente: de habitación íntima a catedral etérea, con ducking. |
| **8D / Órbita** | 1.0.0 | Tu voz gira despacio alrededor de la cabeza (efecto 8D binaural). |
| **Susurro de oído a oído** | 1.0.0 | La voz viaja suavemente de una oreja a la otra, con modo binaural. |
| **Proximidad** | 1.0.0 | Acerca tu voz al oído o aléjala al otro lado de la habitación. |
| **Espacialidad binaural** | 1.0.0 | Coloca tu voz delante, detrás, encima o junto a un oído, con deslizamientos suaves. |

Todos tienen presets incluidos, tus propios presets guardados en tu ordenador y una ayuda **(?)** en cada control, en español e inglés.

### Instalación con el instalador (recomendado)
1. Cierra OBS Studio.
2. Ejecuta `StreamerdeskASMR-Suite-1.0.2-windows-installer.exe`. Se muestra en español o inglés según tu Windows y encuentra solo la carpeta de OBS (normalmente `C:\Program Files\obs-studio`).
3. En la página **Componentes** elige los efectos que quieras (vienen todos marcados).
4. Abre OBS → en el **Mezclador de audio**, ⋮ (o clic derecho) sobre tu micrófono → **Filtros** → **+** → elige un efecto «Streamerdesk ASMR».

- **¿Añadir o quitar efectos más adelante?** Vuelve a ejecutar el instalador: aparecerán marcados los que ya tienes; marca los nuevos y desmarca los que no quieras. Tus presets se conservan.
- **¿Tenías algún efecto instalado por separado?** El instalador de la suite lo actualiza y lo integra; en *Aplicaciones* quedará una sola entrada: «Streamerdesk ASMR Suite (OBS Studio)».
- **Desinstalar:** *Configuración → Aplicaciones → Streamerdesk ASMR Suite (OBS Studio) → Desinstalar*. Solo se quita lo que instaló la suite; tus presets (`%APPDATA%\obs-studio\plugin_config\…`) se conservan.

**Aviso de SmartScreen:** el instalador no lleva una firma de pago, así que Windows puede mostrar «Windows protegió su PC». Pulsa **Más información → Ejecutar de todas formas**. Descárgalo siempre desde https://streamerdesk.com y, si quieres, compara su huella con el archivo `SHA256SUMS`.

### Instalación manual (.zip)
Cierra OBS, descomprime `StreamerdeskASMR-Suite-1.0.2-windows.zip` y copia las carpetas `obs-plugins` y `data` dentro de `C:\Program Files\obs-studio\` (acepta combinar carpetas). Si solo quieres algunos efectos, copia solo sus archivos: `obs-plugins\64bit\<efecto>.dll` y `data\obs-plugins\<efecto>\`.

### Consejos para micrófonos mono (Blue Yeti, Rode…)
- Deja la **Entrada** de cada efecto en **«Suma mono»**: tu micro mono saldrá en estéreo de verdad.
- En OBS, *Propiedades de audio avanzadas*: **no** actives **«Mezclar a mono»** en el micrófono (se perdería todo el efecto).
- *Ajustes → Audio → Canales* = **Estéreo**.
- Escucha y comprueba con **auriculares**: los efectos binaurales y 8D están pensados para ellos.
- Orden recomendado de filtros: reducción de ruido / compresor → Proximidad → Susurro / Espacialidad / 8D → Eco → Reverb. Si combinas varios efectos con sala, baja un poco la sala de cada uno.

---

## English

### The 6 effects
| Effect | Version | What it does |
|---|---|---|
| **Ping-Pong Echo** | 1.0.1 | A stereo echo that bounces from ear to ear, inspired by TAL-Dub. |
| **Reverb** | 1.0.1 | A soft, enveloping reverb, from intimate room to ethereal cathedral, with ducking. |
| **8D / Orbit** | 1.0.0 | Your voice slowly circles the listener's head (binaural 8D effect). |
| **Ear-to-ear whisper** | 1.0.0 | Your voice glides gently from one ear to the other, with a binaural mode. |
| **Proximity** | 1.0.0 | Bring your voice right up to the ear, or move it across the room. |
| **Binaural spatializer** | 1.0.0 | Place your voice in front, behind, above or at one ear, with smooth glides. |

Every effect has built-in presets, your own local presets and a **(?)** help tooltip on each control, in Spanish and English.

### Install with the installer (recommended)
1. Close OBS Studio.
2. Run `StreamerdeskASMR-Suite-1.0.2-windows-installer.exe` (Spanish or English depending on Windows; finds your OBS folder automatically).
3. On the **Components** page choose the effects you want (all are checked by default).
4. Open OBS → Audio Mixer → ⋮ on your microphone → **Filters** → **+** → pick a “Streamerdesk ASMR” effect.

- **Add or remove effects later:** run the installer again; your installed effects are pre-checked. Check new ones, uncheck the ones you don't want. Presets are kept.
- **Already had an effect installed on its own?** The suite installer updates it and takes it over; *Apps* will show a single “Streamerdesk ASMR Suite (OBS Studio)” entry.
- **Uninstall:** *Settings → Apps → Streamerdesk ASMR Suite (OBS Studio) → Uninstall*. Only what the suite installed is removed; your presets are kept.

**SmartScreen note:** the installer is not code-signed, so Windows may say “Windows protected your PC”. Click **More info → Run anyway**. Always download from https://streamerdesk.com and optionally compare with `SHA256SUMS`.

### Manual install (.zip)
Close OBS, unzip and copy the `obs-plugins` and `data` folders into `C:\Program Files\obs-studio\` (merge folders). For only some effects, copy just their `obs-plugins\64bit\<effect>.dll` and `data\obs-plugins\<effect>\`.

### Tips for mono mics (Blue Yeti, Rode…)
Keep each effect's **Input** on **Mono sum**; do **not** enable **Downmix to Mono** in Advanced Audio Properties; set *Settings → Audio → Channels* to **Stereo**; check the result on **headphones**.

---
© 2026 Streamerdesk · https://streamerdesk.com · GPL-2.0-or-later.

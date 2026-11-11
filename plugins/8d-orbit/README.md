![Streamerdesk ASMR Suite](../../branding/banner.jpg)

# Streamerdesk ASMR – 8D / Órbita · v1.0.0

**[Español](#español)** · **[English](#english)**

Filtro de audio para **OBS Studio 28 o posterior** en **Windows 10/11 (64 bits)** · Audio filter for **OBS Studio 28+** on **Windows 10/11 x64**.
Web: **https://streamerdesk.com**

> **Software libre** de **Streamerdesk**, gratuito, bajo licencia **GPL-2.0-or-later** (ver `LICENSE.txt`).
> **Free software** by **Streamerdesk**, free of charge, licensed **GPL-2.0-or-later** (see `LICENSE.txt`).

---

## Español

### ¿Qué hace?
Tu voz **gira lentamente alrededor de la cabeza** de quien te escucha con auriculares: el famoso efecto **«8D»**, hecho con delicadeza para ASMR. No es un simple paneo izquierda-derecha: usa un **modelo binaural de cabeza** —diferencia de tiempo entre oídos (hasta 0,66 ms), diferencia de volumen, «sombra» de la cabeza que oscurece el oído lejano, pistas delante/detrás tipo pabellón auditivo y una ligera ondulación de altura— para que la voz se sienta de verdad **alrededor** de ti. Con un toque de **sala** (el motor de la Reverb de Streamerdesk) el sonido sale de dentro de los auriculares.

Pensado para **micrófonos USB/condensador** (Blue Yeti, Rode NT-USB, Rode PodMic USB…), que suelen ser mono: la entrada se suma a mono y sale en **estéreo binaural**. **Se escucha con auriculares**; en altavoces se oye como un paneo suave.

### Compatibilidad
| Sistema | Archivo | OBS |
|---|---|---|
| Windows 10/11 x64 | `StreamerdeskASMR-8D-Orbita-1.0.0-windows-installer.exe` (instalador) o `StreamerdeskASMR-8D-Orbita-1.0.0-windows.zip` (manual) | 28.0 → última |
| macOS / Linux | próximamente | — |

Un único DLL sirve para todas las versiones desde OBS 28.0: solo usa funciones que ya existían en OBS 28 (comprobado contra el `obs.dll` de OBS 28.0 y de la 33.0.1). Solo depende de OBS y de Windows.

### Instalación
**Instalador (recomendado)**
1. Cierra OBS Studio.
2. Ejecuta `StreamerdeskASMR-8D-Orbita-1.0.0-windows-installer.exe`. Encuentra solo la carpeta de OBS (normalmente `C:\Program Files\obs-studio`). Se muestra en español o inglés según tu Windows.
3. Si Windows SmartScreen avisa, pulsa **Más información → Ejecutar de todas formas** (el instalador no lleva firma de pago).
4. Para desinstalar: *Configuración → Aplicaciones → “Streamerdesk ASMR - 8D / Órbita (OBS Studio)” → Desinstalar*.

**Manual (.zip)**
Cierra OBS, descomprime y copia las carpetas `obs-plugins` y `data` dentro de `C:\Program Files\obs-studio\` (acepta combinar carpetas). Quedará `obs-plugins\64bit\streamerdesk-asmr-8d-orbit.dll` y `data\obs-plugins\streamerdesk-asmr-8d-orbit\locale\*.ini`.

### Uso
1. En el **Mezclador de audio** de OBS, ⋮ (o clic derecho) sobre tu micrófono → **Filtros**.
2. **+** → **Streamerdesk ASMR - 8D / Órbita**.
3. Elige un preset y ajústalo con auriculares puestos. Cada control tiene un **(?)** con su explicación; los que no se usan en el modo elegido se ocultan.

**Consejos:** deja **Entrada = “Suma mono”** para micros mono; en *Propiedades de audio avanzadas* **no** actives **“Mezclar a mono”** (se perdería todo el efecto); en *Ajustes → Audio*, Canales = **Estéreo**. Coloca este filtro **al final** de la cadena (después de ruido/compresor/eco/reverb). Si ya usas la Reverb de Streamerdesk, baja aquí la **Sala** a 0–10 %.

### Controles
**Presets:** desplegable (★ incluidos, 👤 tuyos), Cargar, Nombre, Guardar, Borrar, Restablecer. Tus presets se guardan solo en tu ordenador (`%APPDATA%\obs-studio\plugin_config\streamerdesk-asmr-8d-orbit\presets.json`), sin exportar ni compartir, y se conservan al reiniciar OBS.

| Grupo | Control | Rango | Defecto | Visible |
|---|---|---|---|---|
| Movimiento | Tipo de movimiento | Órbita continua / Vaivén en arco / Deriva aleatoria / Órbita al ritmo (BPM) | Órbita continua | siempre |
| Movimiento | Velocidad en | Segundos por vuelta / Hz | Segundos | no en «al ritmo» |
| Movimiento | Duración de una vuelta | 2 – 120 s | 12 s | unidad = segundos, no en «al ritmo» |
| Movimiento | Velocidad | 0,008 – 0,5 Hz | 0,08 Hz | unidad = Hz, no en «al ritmo» |
| Movimiento | Tempo | 40 – 240 BPM | 90 BPM | solo «al ritmo» |
| Movimiento | Compases por vuelta (4/4) | ½, 1, 2, 4, 8, 16 | 4 | solo «al ritmo» |
| Movimiento | Sentido de giro | Horario / Antihorario | Horario | siempre |
| Movimiento | Ángulo inicial (0 = delante, 90 = oído der., −90 = oído izq.) | −180 – 180° | −90° | no en «deriva» |
| Movimiento | Anchura del arco | 10 – 360° | 180° | solo «vaivén» |
| Movimiento | Quedarse cerca de los oídos | 0 – 100 % | 30 % | siempre |
| Movimiento | Movimiento de altura (hasta ±35°) | 0 – 100 % | 20 % | siempre |
| Cabeza y oídos | Intensidad binaural | 0 – 100 % | 85 % | siempre |
| Cabeza y oídos | Pistas delante / detrás | 0 – 100 % | 60 % | siempre |
| Distancia y sala | Distancia (radio de la órbita) | 0,25 – 4 m | 0,6 m | siempre |
| Distancia y sala | Sala | 0 – 100 % | 20 % | siempre |
| Entrada y mezcla | Entrada | Suma mono / Mantener estéreo | Suma mono | siempre |
| Entrada y mezcla | Mezcla original / 8D (50 % = ambos al máximo) | 0 – 100 % | 100 % | siempre |
| Entrada y mezcla | Volumen de salida | −24 … +12 dB | 0 dB | siempre |

**Presets incluidos**
| Preset | Carácter |
|---|---|
| ★ Órbita 8D clásica | Vueltas completas de 10 s a 1 m, pistas delante/detrás marcadas, un poco de sala: el 8D de toda la vida, pero suave. |
| ★ Susurro de oído a oído | Vaivén de 180° de oído izquierdo a derecho pasando por delante, cada 8 s, a 30 cm y con sesgo a los oídos: íntimo y cosquilleante. |
| ★ Mariposa errante | Deriva aleatoria lenta (≈14 s por vuelta) a 50 cm con ondulación de altura: nunca sabes por dónde aparecerá. |
| ★ Pulso hipnótico (al ritmo) | Órbita antihoraria sincronizada: 80 BPM × 2 compases = una vuelta cada 6 s, a 1,2 m y con más sala. |

### Notas
- Con «Mezcla» por debajo de 100 % la voz original (centrada) se suma a la voz que gira; puede sonar algo «hueca» por la pequeña diferencia de tiempo entre ambas. Para 8D puro, 100 %.
- El modo «al ritmo» sigue el tempo, pero no se engancha al inicio de la canción (OBS no tiene reloj de compases).
- Con fuentes 5.1/7.1 se procesan los canales frontales izquierdo/derecho; el resto pasa sin cambios.
- Uso de CPU muy bajo (≈0,8 % de un núcleo a 48 kHz en las pruebas, sala incluida).

---

## English

### What it does
Your voice **slowly circles around the listener's head** on headphones — the famous **“8D”** effect, done tastefully for ASMR. It's not a plain left-right pan: a **binaural head model** (interaural time difference up to 0.66 ms, level difference, head shadow darkening the far ear, pinna-like front/back cues and a gentle height wave) makes the voice feel truly **around** you, and a touch of **room** (the Streamerdesk Reverb engine) takes it out of the headphones. Built for mono **USB/condenser mics** (Blue Yeti, Rode…). **Listen on headphones**; on speakers it sounds like a soft pan.

### Compatibility & install
One DLL for **OBS Studio 28.0 up to the latest** on **Windows 10/11 x64** (checked against OBS 28.0 and 33.0.1 `obs.dll`). macOS/Linux coming later.
- **Installer:** close OBS, run `StreamerdeskASMR-8D-Orbita-1.0.0-windows-installer.exe` (auto-detects OBS, Spanish/English by system language, uninstall from *Settings → Apps*). SmartScreen: *More info → Run anyway*.
- **Manual zip:** close OBS and copy `obs-plugins` and `data` into `C:\Program Files\obs-studio\`.

### Use
Audio Mixer → ⋮ on your mic → **Filters** → **+** → **Streamerdesk ASMR - 8D / Orbit**. Pick a preset and tweak with headphones on; every control has a **(?)** tooltip and controls that don't apply to the chosen motion are hidden. Keep **Input = Mono sum** for mono mics, do **not** enable **Downmix to Mono**, keep OBS audio on **Stereo**, and put this filter **last** in the chain.

### Controls (range, default)
Motion: type continuous orbit / back-and-forth arc / random wander / tempo-synced (orbit) · speed unit seconds/Hz · lap duration 2–120 s (12) · speed 0.008–0.5 Hz (0.08) · tempo 40–240 BPM (90) · bars per lap ½/1/2/4/8/16 (4) · direction clockwise/counter-clockwise (clockwise) · start angle −180…180° (−90 = left ear) · arc width 10–360° (180) · stay near the ears 0–100 % (30) · height movement 0–100 % (20).
Head & ears: binaural intensity 0–100 % (85) · front/back cues 0–100 % (60).
Distance & room: distance 0.25–4 m (0.6) · room 0–100 % (20).
Input & mix: input mono sum / keep stereo (mono sum) · original/8D mix 0–100 % (100; 50 % = both full) · output −24…+12 dB (0).
Built-in presets: Classic 8D orbit, Ear-to-ear whisper, Wandering butterfly, Hypnotic pulse (tempo). User presets are stored locally only and survive restarts; no export/import.

---
© 2026 Streamerdesk · https://streamerdesk.com · GPL-2.0-or-later.

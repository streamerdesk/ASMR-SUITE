![Streamerdesk ASMR Suite](../../branding/banner.jpg)

# Streamerdesk ASMR – Reverb · v1.0.1

**[Español](#español)** · **[English](#english)**

Filtro de audio para **OBS Studio 28 o posterior** en **Windows 10/11 (64 bits)** · Audio filter for **OBS Studio 28+** on **Windows 10/11 x64**.
Web: **https://streamerdesk.com**

> **Software libre** de **Streamerdesk**, gratuito, bajo licencia **GPL-2.0-or-later** (ver `LICENSE.txt`).
> **Free software** by **Streamerdesk**, free of charge, licensed **GPL-2.0-or-later** (see `LICENSE.txt`).

---

## Español

### ¿Qué hace?
Envuelve tu voz en un **espacio suave y realista**: desde una habitación íntima hasta una catedral etérea. Está pensada para **susurros y sonidos ASMR con micrófonos USB/condensador** (Blue Yeti, Rode NT-USB, Rode PodMic USB…), que suelen ser mono: la entrada se suma a mono y la reverb sale en **estéreo**, abierta y envolvente con auriculares.

Por dentro: pre-retardo → difusión de entrada → reflexiones tempranas + cola tardía con una red de 8 líneas de retardo realimentadas (FDN), amortiguación de agudos, modulación suave para evitar el sonido metálico, opción de congelar y **ducking** para que la reverb se aparte mientras hablas. Todos los cambios se suavizan: puedes mover los controles en directo sin clics.

### Compatibilidad
| Sistema | Archivo | OBS |
|---|---|---|
| Windows 10/11 x64 | `StreamerdeskASMR-Reverb-1.0.1-windows-installer.exe` (instalador) o `StreamerdeskASMR-Reverb-1.0.1-windows.zip` (manual) | 28.0 → última |
| macOS / Linux | próximamente | — |

Un único DLL sirve para todas las versiones desde OBS 28.0: se compila con las cabeceras de libobs 28.0 y solo usa funciones que ya existían en OBS 28 (comprobado contra el `obs.dll` de OBS 28.0 y de la 33.0.1). Solo depende de OBS y de Windows.

### Instalación
**Instalador (recomendado)**
1. Cierra OBS Studio.
2. Ejecuta `StreamerdeskASMR-Reverb-1.0.1-windows-installer.exe`. Encuentra solo la carpeta de OBS (normalmente `C:\Program Files\obs-studio`). Se muestra en español o inglés según tu Windows.
3. Si Windows SmartScreen avisa, pulsa **Más información → Ejecutar de todas formas** (el instalador no lleva firma de pago).
4. Para desinstalar: *Configuración → Aplicaciones → “Streamerdesk ASMR - Reverb (OBS Studio)” → Desinstalar*.

**Manual (.zip)**
Cierra OBS, descomprime y copia las carpetas `obs-plugins` y `data` dentro de `C:\Program Files\obs-studio\` (acepta combinar carpetas). Quedará `obs-plugins\64bit\streamerdesk-asmr-reverb.dll` y `data\obs-plugins\streamerdesk-asmr-reverb\locale\*.ini`.

### Uso
1. En el **Mezclador de audio** de OBS, ⋮ (o clic derecho) sobre tu micrófono → **Filtros**.
2. **+** → **Streamerdesk ASMR - Reverb**.
3. Elige un preset y ajústalo. Cada control tiene un **(?)** con su explicación.

**Consejos para micros mono (Yeti, Rode…):** deja **Entrada = “Suma mono”**; en *Propiedades de audio avanzadas* **no** actives **“Mezclar a mono”** (perderías el estéreo de la reverb); en *Ajustes → Audio*, Canales = **Estéreo**. Si usas varios filtros, pon la reverb **al final** de la lista (después de ruido/compresor/eco).

### Controles
**Presets:** desplegable (★ incluidos, 👤 tuyos), Cargar, Nombre, Guardar, Borrar, Restablecer. Tus presets se guardan solo en tu ordenador (`%APPDATA%\obs-studio\plugin_config\streamerdesk-asmr-reverb\presets.json`), sin exportar ni compartir, y se conservan al reiniciar OBS.

| Grupo | Control | Rango | Defecto |
|---|---|---|---|
| Sala | Tamaño de la sala | 0 – 100 % | 45 % |
| Sala | Duración de la cola (RT60) *(oculto si está congelada)* | 0,2 – 10 s | 1,6 s |
| Sala | Pre-retardo | 0 – 200 ms | 20 ms |
| Sala | Difusión | 0 – 100 % | 70 % |
| Sala | Congelar la cola (FREEZE) | sí / no | no |
| Tono | Amortiguación de agudos *(oculto si está congelada)* | 0 – 100 % | 50 % |
| Tono | Corte de graves en la reverb | 20 – 1000 Hz (20 = apagado) | 180 Hz |
| Tono | Corte de agudos en la reverb | 1000 – 20000 Hz (20000 = apagado) | 9000 Hz |
| Espacio / Estéreo | Reflexiones tempranas / cola | 0 – 100 % (0 = solo tempranas, 50 = ambas al máximo, 100 = solo cola) | 55 % |
| Espacio / Estéreo | Anchura estéreo | 0 – 150 % | 100 % |
| Espacio / Estéreo | Modulación suave | 0 – 100 % | 35 % |
| Espacio / Estéreo | Entrada | Suma mono / Mantener estéreo | Suma mono |
| Ducking | Ducking (bajar la reverb mientras hablas) | 0 – 100 % | 30 % |
| Ducking | Recuperación del ducking *(oculto si ducking = 0)* | 20 – 2000 ms | 400 ms |
| Mezcla | Mezcla seco / reverb (DRY / WET) | 0 – 100 % (50 % = ambos al máximo) | 25 % |
| Mezcla | Volumen de salida | −24 … +12 dB | 0 dB |

**Presets incluidos**
| Preset | Carácter |
|---|---|
| ★ Habitación íntima | Sala pequeña (0,6 s), reflexiones cercanas, muy natural para susurros al oído. |
| ★ Sala suave | Sala acogedora (1,9 s) con pre-retardo de 24 ms: los susurros siguen nítidos. |
| ★ Catedral susurro | Espacio enorme (6,5 s), pre-retardo de 65 ms, ancho 130 % y ducking fuerte: el ambiente florece en las pausas. |
| ★ Placa cálida | Cola densa estilo “placa” (2,4 s), muy difusa y oscura (corte en 6 kHz). |

### Notas
- Con fuentes 5.1/7.1 se procesan los canales frontales izquierdo/derecho; el resto pasa sin cambios.
- Uso de CPU muy bajo (≈0,7 % de un núcleo a 48 kHz en las pruebas).

---

## English

### What it does
Wraps your voice in a **soft, realistic space**, from an intimate room to an ethereal cathedral. Built for **whispers and ASMR sounds on USB/condenser mics** (Blue Yeti, Rode…): the input is summed to mono and the reverb comes out in wide **stereo**. Inside: pre-delay → input diffusion → early reflections + an 8-line feedback delay network tail, HF damping, gentle modulation against metallic ringing, freeze, and **ducking** so the reverb steps aside while you talk. Every change is smoothed — no clicks when you move controls live.

### Compatibility & install
One DLL for **OBS Studio 28.0 up to the latest** on **Windows 10/11 x64** (built against libobs 28.0, checked against OBS 28.0 and 33.0.1 `obs.dll`). macOS/Linux coming later.
- **Installer:** close OBS, run `StreamerdeskASMR-Reverb-1.0.1-windows-installer.exe` (auto-detects OBS, Spanish/English by system language, uninstall from *Settings → Apps*). SmartScreen: *More info → Run anyway*.
- **Manual zip:** close OBS and copy `obs-plugins` and `data` into `C:\Program Files\obs-studio\`.

### Use
Audio Mixer → ⋮ on your mic → **Filters** → **+** → **Streamerdesk ASMR - Reverb**. Pick a preset and tweak; every control has a **(?)** tooltip. For mono mics keep **Input = Mono sum**, do **not** enable **Downmix to Mono**, keep OBS audio on **Stereo**, and put the reverb **last** in the filter chain.

### Controls
Room: size 0–100 % (45), decay 0.2–10 s (1.6), pre-delay 0–200 ms (20), diffusion 0–100 % (70), freeze · Tone: HF damping 0–100 % (50), reverb low cut 20–1000 Hz (180), reverb high cut 1000–20000 Hz (9000) · Space/Stereo: early/late 0–100 % (55), width 0–150 % (100), modulation 0–100 % (35), input mono sum / keep stereo · Ducking: amount 0–100 % (30), release 20–2000 ms (400) · Mix: dry/wet 0–100 % (25; 50 % = both full), output −24…+12 dB (0).
Built-in presets: Intimate room, Soft hall, Whisper cathedral, Warm plate. User presets are stored locally only and survive restarts; no export/import.

---
© 2026 Streamerdesk · https://streamerdesk.com · GPL-2.0-or-later.

# Changelog — Streamerdesk ASMR Suite

Las descargas oficiales siguen saliendo de [streamerdesk.com](https://streamerdesk.com).
Official downloads remain at [streamerdesk.com](https://streamerdesk.com).

## 1.0.2 — 2026-11-11 (instalador de Windows / Windows installer)

Solo cambia el instalador combinado de Windows; los seis efectos son exactamente los mismos que en la 1.0.1.
Installer-only update; the six effects are byte-for-byte the same as in 1.0.1.

- **Corregido:** el instalador ahora reconoce los efectos que ya tenías instalados, también los que pusiste uno a uno con sus instaladores sueltos o copiando los archivos a mano, y los marca con «(instalado vX.Y.Z)». / **Fixed:** the installer now recognises effects you already have, including ones installed one by one with their standalone installers or copied by hand, and labels them "(installed vX.Y.Z)".
- La primera vez siguen saliendo todos marcados; si vuelves a ejecutarlo, se marcan los que ya gestiona la suite. / On the first run every effect is still checked; when you run it again, the ones the suite manages are checked.
- Al pasar a la suite, los efectos instalados por separado dejan su entrada en Aplicaciones y su desinstalador: te queda una sola entrada, «Streamerdesk ASMR Suite». Si desmarcas uno que tenías suelto, la suite no lo toca. / Standalone copies taken over by the suite lose their own Apps entry and uninstaller, so only "Streamerdesk ASMR Suite" remains. If you uncheck one you had installed separately, the suite leaves it alone.
- Si OBS no está en el registro (por ejemplo, una instalación portable), el instalador propone la carpeta donde ya tenías los efectos. / If OBS is not in the registry (e.g. a portable install), the installer suggests the folder where your effects already are.

En esta misma fecha se publica el código fuente unificado de los seis efectos (Windows, macOS y Linux) en este repositorio. No hay cambios de sonido, presets, interfaz ni textos respecto a los paquetes ya publicados. / The unified source of all six effects (Windows, macOS and Linux) is also published in this repository on this date. There are no changes to the sound, presets, UI or locale strings relative to the packages already shipped.

## 1.0.1 — 2026-11-11

Primer paquete combinado de la suite (Windows, y también las suites completas de macOS y Linux).
First combined suite bundle (Windows, plus the full macOS and Linux suites).

| Efecto / Effect | Versión / Version | Cambios / Changes |
|---|---|---|
| Eco Ping-Pong / Ping-Pong Echo | 1.0.1 | **Corregido:** al cargar un preset ahora se muestran los controles correctos. / **Fixed:** loading a preset now shows the correct controls. |
| Reverb | 1.0.1 | **Corregido:** al cargar un preset ahora se muestran los controles correctos. / **Fixed:** loading a preset now shows the correct controls. |
| 8D / Órbita (8D / Orbit) | 1.0.0 | Primera versión. / First release. |
| Susurro de oído a oído / Ear-to-ear whisper | 1.0.0 | Primera versión. / First release. |
| Proximidad / Proximity | 1.0.0 | Primera versión. / First release. |
| Espacialidad binaural / Binaural spatializer | 1.0.0 | Primera versión. / First release. |

- Un solo instalador bilingüe con página de componentes: elige qué efectos instalar; al volver a ejecutarlo puedes añadir o quitar efectos. / One bilingual installer with a components page: pick which effects to install; run it again to add or remove effects.
- Actualiza e integra las copias instaladas por separado; una sola entrada en Aplicaciones y un único desinstalador que conserva tus presets. / Upgrades and takes over individually installed copies; one Apps entry and a single uninstaller that keeps your presets.
- macOS: zip universal (Apple Silicon e Intel) con instalador de doble clic y, cuando es posible, un `.pkg`. / macOS: universal zip (Apple Silicon and Intel) with a double-click installer and, when possible, a `.pkg`.
- Linux x86_64: `install.sh` que no pide contraseña, compatible con OBS normal y Flatpak. / Linux x86_64: `install.sh` that needs no admin password, for regular and Flatpak OBS.

Los cambios de cada efecto están también en `plugins/<efecto>/CHANGELOG.md`. El historial del instalador combinado de Windows está en `suite-bundle/CHANGELOG.md`.
Per-effect changes are also in `plugins/<effect>/CHANGELOG.md`. The Windows suite-bundle history is in `suite-bundle/CHANGELOG.md`.

## 1.0.0 — 2026-11-11

Primera versión pública de Eco Ping-Pong (después actualizado a 1.0.1), y primeras versiones de los demás efectos según se fueron publicando.
First public release of Ping-Pong Echo (later updated to 1.0.1), and first releases of the other effects as they were published.

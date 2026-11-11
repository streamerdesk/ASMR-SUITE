# Origen de las fuentes / Source layout

Este monorepo reúne los seis efectos de la Streamerdesk ASMR Suite **sin cambiar el DSP, los presets, la interfaz ni los textos de idioma**. Tres de ellos nacieron como proyectos sueltos y se compilaban con su propia copia de `common/`. Esa copia se conserva aquí, vendorizada junto al efecto, para que una recompilación pueda coincidir con los binarios ya publicados en streamerdesk.com.

This monorepo gathers all six effects **without changing the DSP, presets, UI or locale content**. Three effects started as standalone projects and were built against their own copy of `common/`. That copy is vendored next to the effect so a rebuild can match the binaries already published on streamerdesk.com.

## Resumen / Summary

| Efecto / Effect | Origen / Origin | `common/` que usa / `common/` used | Notas / Notes |
|---|---|---|---|
| Eco Ping-Pong | `plugins/pingpong-echo/` (suite) | `common/` compartido (1.0.1) | El arreglo de presets de la 1.0.1 vive aquí. / The 1.0.1 preset-visibility fix lives here. |
| Reverb | `plugins/reverb/` (suite) | `common/` compartido (1.0.1) | Igual que el eco. / Same as the echo. |
| 8D / Órbita | `plugins/8d-orbit/` (suite) | `common/` compartido (1.0.1) | Reutiliza `plugins/reverb/src/reverb_dsp.c`. / Reuses the Reverb FDN core. |
| Susurro de oído a oído | `plugins/ear-to-ear/` | **`plugins/ear-to-ear/common/`** (anterior) | Las fuentes del plugin coinciden con las de la suite; solo se añade `test/obs_load_test.c`. / Plugin sources match the suite tree; only `test/obs_load_test.c` was added. |
| Proximidad | `plugins/proximity/` | **`plugins/proximity/common/`** (anterior) | Proyecto suelto original. / Original standalone project. |
| Espacialidad binaural | `plugins/binaural/` | **`plugins/binaural/common/`** (mixto) | Proyecto suelto original. / Original standalone project. |

Los scripts (`scripts/build-all.sh`, `linux/build_linux_suite.py`, `macos/build_macos_suite.py`, CMake) eligen el `common/` del plugin cuando existe `plugins/<efecto>/common/obs/`, y si no, el compartido.

Build scripts pick the plugin's own `common/` when `plugins/<effect>/common/obs/` is present, otherwise the shared one.

## Diferencias concretas / Concrete diffs

Comparado con el `common/` compartido (el de Eco 1.0.1 / Reverb 1.0.1 / 8D):

Compared with the shared `common/` (Echo 1.0.1 / Reverb 1.0.1 / 8D):

1. **`common/obs/sd_presets.c`** — El compartido, al cargar un preset o restablecer, vuelve a aplicar los callbacks «modified» para que la visibilidad de los controles coincida con los valores recién cargados (`obs_properties_apply_settings`). Es el arreglo de la 1.0.1 del eco y la reverb. Las copias vendorizadas de Susurro, Proximidad y Espacialidad **no** incluyen esa llamada: así se mantiene el comportamiento con el que se publicaron en 1.0.0.
   The shared copy, after loading or resetting a preset, re-runs property "modified" callbacks so dependent visibility matches the newly loaded values. That is the 1.0.1 Echo/Reverb fix. The vendored copies used by Ear-to-ear, Proximity and Binaural **do not** include that call, so they keep the 1.0.0 behaviour they shipped with.

2. **`common/dsp/sd_dsp.h`** — El compartido y el de Espacialidad añaden `sd_delay_read_hermite()` (interpolación de 4 puntos para líneas en bucle, usada por la reverb). El de Susurro y Proximidad no tiene esa función. Es `static inline` y no cambia el binario de un plugin que no la llama; aun así se respeta cada copia.
   The shared header and the Binaural copy add `sd_delay_read_hermite()` (4-point interpolation for feedback delay lines, used by the reverb). Ear-to-ear and Proximity do not have that helper. It is `static inline` and does not change the binary of a plugin that never calls it; each copy is still kept as-is.

3. **`common/windows/version.rc.in`** — El compartido añade `#pragma code_page(65001)` para nombres con acentos (p.ej. «Órbita»). Las copias vendorizadas no lo tienen. Solo afecta al recurso de versión de Windows, no al audio.
   The shared template adds `#pragma code_page(65001)` for accented names (e.g. "Órbita"). The vendored copies do not. This only affects the Windows version resource, not the audio.

4. **`sd_presets.h`, `sd_atomic.h`, `installer.nsi.in`** y las plantillas de macOS/Linux son idénticos en todas las copias.
   Those files are identical in every copy.

5. **Idiomas / locales** — Eco, Reverb, 8D y Susurro incluyen `es-ES`, `es-419` y `en-US`. Proximidad y Espacialidad, tal como se publicaron, solo incluyen `es-ES` y `en-US` (falta `es-419`). No se ha añadido aquí para no alterar el contenido publicado.
   Echo, Reverb, 8D and Ear-to-ear ship `es-ES`, `es-419` and `en-US`. Proximity and Binaural, as published, only ship `es-ES` and `en-US` (no `es-419`). That file has not been added here, so published locale content stays unchanged.

6. **Flags de compilación** — Proximidad y Espacialidad se compilaban con flags extra (`-Wno-unused-parameter`, `-fno-strict-aliasing`, y en Windows `-DUNICODE` y `-Wl,--subsystem,windows`). Esas flags están en su `plugin.env` (`PLUGIN_CFLAGS`, `PLUGIN_CFLAGS_WINDOWS`, `PLUGIN_LDFLAGS_WINDOWS`) para poder reproducir el mismo binario.
   Proximity and Binaural were built with a few extra flags. Those are recorded in each plugin's `plugin.env` so a rebuild can match the shipped binary.

## Qué se ha excluido del repositorio / What was left out

Carpetas `build/`, `dist*/`, binarios (`.dll`, `.so`, `.exe`, `.zip`, `.tar.gz`, `.pkg`), cabeceras y librerías descargadas de OBS (`.deps/`, `deps/obs28/`), prefijos de Wine, registros, archivos temporales y cualquier ruta absoluta de la máquina de compilación. Los scripts vuelven a obtener las cabeceras de OBS 28.0.0 y los stubs de enlace cuando hace falta.

`build/`, `dist*/`, binaries (`.dll`, `.so`, `.exe`, `.zip`, `.tar.gz`, `.pkg`), downloaded OBS headers and libs (`.deps/`, `deps/obs28/`), Wine prefixes, logs, temp files and any absolute paths from the original build machine were left out. The scripts fetch OBS 28.0.0 headers and link stubs again when needed.

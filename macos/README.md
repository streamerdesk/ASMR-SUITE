# macOS — compilación de la suite / suite build

`bash scripts/build-macos-suite.sh` (desde Linux) compila los 6 efectos como bundles `.plugin` universales
(arm64 + x86_64, macOS 11+, cabeceras OBS 28.0.0), firma ad-hoc con `rcodesign`, verifica contra el libobs
real de OBS 28.0.0 y 33.0.1 para macOS, ejecuta las pruebas DSP y deja todo en `dist-macos/`.

- `plugins.json`: lista de efectos (cada uno se compila desde `plugins/<efecto>/` y su `common/` (compartido o vendorizado), sin cambios de DSP).
- `macpkg.py`: crea el `.pkg` con selector de componentes sin macOS (cpio odc + mkbom + xar en Python).
- `check_pkg.py`, `check_bom.py`, `verify_codesign.py`: verificaciones independientes.
- Plantillas: `Instalar-Suite.command.in`, `Desinstalar-Suite.command.in`, `pkg-postinstall.in`, `pkg-res/`, READMEs.

Dependencias (en `$DEPS`, por defecto `.deps/` en la raiz del repositorio):
`macobs/libs/libobs-{28.0.0,33.0.1}-{arm64,x86_64}` (extraídos de los DMG oficiales),
`mactools/rcodesign` (apple-codesign 0.29), `mactools/mkbom` (bomutils compilado con `zig c++`).

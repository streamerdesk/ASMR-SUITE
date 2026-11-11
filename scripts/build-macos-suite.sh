#!/bin/bash
# Compila, firma (ad-hoc), verifica y empaqueta la suite completa para macOS universal -> dist-macos/
# Requiere: python3 + ziglang + macholib, libobs macOS 28.0.0/33.0.1 en $DEPS/macobs/libs,
# rcodesign y mkbom (bomutils) en $DEPS/mactools. No toca dist/ (Windows).
set -euo pipefail
exec python3 "$(cd "$(dirname "$0")/.." && pwd)/macos/build_macos_suite.py" "$@"

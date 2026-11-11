#!/usr/bin/env bash
# Crea el instalador .pkg de macOS (debe ejecutarse en macOS: usa pkgbuild/productbuild).
# Instala el .plugin en ~/Library/Application Support/obs-studio/plugins del usuario.
#   scripts/macos-pkg.sh <dir_con_.plugin> <salida>
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/suite.env"; source "$ROOT/plugins/pingpong-echo/plugin.env"
PAYLOAD="$1"; OUTDIR="$2"; mkdir -p "$OUTDIR"
BUNDLE="$PAYLOAD/$PLUGIN_ID.plugin"
chmod 755 "$BUNDLE/Contents/MacOS/$PLUGIN_ID"
if [ -n "${APPLE_DEV_ID_APP:-}" ]; then
	codesign --force --timestamp --options runtime --sign "$APPLE_DEV_ID_APP" "$BUNDLE"
else
	codesign --force --sign - "$BUNDLE"   # firma ad-hoc
fi
SCRIPTS="$(mktemp -d)"
cat >"$SCRIPTS/postinstall" <<POST
#!/bin/bash
U=\$(stat -f%Su /dev/console)
H=\$(dscl . -read /Users/\$U NFSHomeDirectory | awk '{print \$2}')
D="\$H/Library/Application Support/obs-studio/plugins"
mkdir -p "\$D"; rm -rf "\$D/$PLUGIN_ID.plugin"
cp -R "/Library/Application Support/Streamerdesk/$PLUGIN_ID.plugin" "\$D/"
chown -R "\$U" "\$H/Library/Application Support/obs-studio"
exit 0
POST
chmod 755 "$SCRIPTS/postinstall"
pkgbuild --root "$PAYLOAD" --install-location "/Library/Application Support/Streamerdesk" \
	--identifier "$PLUGIN_BUNDLE_ID.pkg" --version "$PLUGIN_VERSION" --scripts "$SCRIPTS" "$OUTDIR/component.pkg"
OUT="$OUTDIR/$PLUGIN_PRODUCT-$PLUGIN_VERSION-macos.pkg"
if [ -n "${APPLE_DEV_ID_INSTALLER:-}" ]; then
	productbuild --package "$OUTDIR/component.pkg" --sign "$APPLE_DEV_ID_INSTALLER" "$OUT"
else
	productbuild --package "$OUTDIR/component.pkg" "$OUT"
fi
rm -f "$OUTDIR/component.pkg"
echo "$OUT"

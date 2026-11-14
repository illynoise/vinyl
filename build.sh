#!/usr/bin/env bash
# Vinyl Studio - Base Jase Plugins: configure, build Release, test, package.
#   ./build.sh                 build everything and write dist/BaseJasePlugins-VinylStudio-<version>/
#   NO_INSTALL=1 ./build.sh    do not copy the plugins into the user plugin folders
#   SKIP_TESTS=1 ./build.sh    skip vs_tests / auval / pluginval
# macOS signing (optional):
#   DEVELOPER_ID_APP="Developer ID Application: Name (TEAMID)"
#   DEVELOPER_ID_INSTALLER="Developer ID Installer: Name (TEAMID)"
#   NOTARY_PROFILE=<notarytool keychain profile>     (xcrun notarytool store-credentials)
set -euo pipefail
cd "$(dirname "$0")"
VERSION=$(sed -n 's/^project(VinylStudio VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
NAME="BaseJasePlugins-VinylStudio-$VERSION"
DIST="dist/$NAME"
COPY=ON; [ -n "${NO_INSTALL:-}" ] && COPY=OFF
GEN=(); command -v ninja >/dev/null && GEN=(-G Ninja)

echo "== Configure + build (Release) =="
cmake -S . -B build "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release -DVS_COPY_AFTER_BUILD=$COPY
cmake --build build --config Release --parallel

rm -rf "$DIST"; mkdir -p "$DIST"
VARIANTS=(VinylStudio VinylStudioMono VinylStudioLite VinylStudioLiteMono)
art() { find "build/$1_artefacts" -maxdepth 3 -name "$2" | head -1; }

if [ -z "${SKIP_TESTS:-}" ]; then
  echo "== Offline tests =="
  T=$(find build -type f -name vs_tests | head -1)
  if [ "$(uname)" = Linux ] && command -v xvfb-run >/dev/null; then xvfb-run -a "$T" test-output | tee "$DIST/test-report.txt"
  else "$T" test-output | tee "$DIST/test-report.txt"; fi
fi

if [ "$(uname)" = Darwin ]; then
  ROOT=build/pkgroot; rm -rf "$ROOT"
  mkdir -p "$ROOT/Library/Audio/Plug-Ins/VST3" "$ROOT/Library/Audio/Plug-Ins/Components"
  for v in "${VARIANTS[@]}"; do
    cp -R "$(art "$v" '*.vst3')" "$ROOT/Library/Audio/Plug-Ins/VST3/"
    cp -R "$(art "$v" '*.component')" "$ROOT/Library/Audio/Plug-Ins/Components/"
  done
  echo "== Code signing =="
  SIGN="${DEVELOPER_ID_APP:--}"            # "-" = ad-hoc signature
  OPTS=(--force --deep --timestamp=none); [ "$SIGN" != "-" ] && OPTS=(--force --deep --timestamp --options runtime)
  find "$ROOT" \( -name '*.vst3' -o -name '*.component' \) -maxdepth 6 -print0 | xargs -0 -n1 codesign "${OPTS[@]}" -s "$SIGN"

  if [ -z "${SKIP_TESTS:-}" ]; then
    echo "== auval (Audio Unit validation) =="
    # auval only sees installed components; COPY_PLUGIN_AFTER_BUILD put them in ~/Library/Audio/Plug-Ins/Components
    killall -9 AudioComponentRegistrar 2>/dev/null || true
    for code in Bjv1 Bjv2 Bjv3 Bjv4; do
      if auval -v aufx $code Bjsp >> "$DIST/auval-report.txt" 2>&1; then echo "auval PASS $code"; else echo "auval FAIL $code (see auval-report.txt)"; AUVAL_FAILED=1; fi
    done
    if command -v pluginval >/dev/null || [ -x /Applications/pluginval.app/Contents/MacOS/pluginval ]; then
      PV=$(command -v pluginval || echo /Applications/pluginval.app/Contents/MacOS/pluginval)
      echo "== pluginval (strictness 10) =="
      find "$ROOT" \( -name '*.vst3' -o -name '*.component' \) -maxdepth 6 | while read -r p; do
        "$PV" --strictness-level 10 --validate-in-process "$p" >> "$DIST/pluginval-report.txt" 2>&1 && echo "PASS $p" || { echo "FAIL $p"; exit 1; }
      done
    else echo "pluginval not found - install it from https://github.com/Tracktion/pluginval to run this step"; fi
  fi

  echo "== Installer package =="
  pkgbuild --root "$ROOT" --identifier com.basejaseplugins.vinylstudio --version "$VERSION" --install-location / build/VinylStudio-component.pkg
  PB=(--distribution installer/macos/distribution.xml --resources installer/macos/resources --package-path build)
  [ -n "${DEVELOPER_ID_INSTALLER:-}" ] && PB+=(--sign "$DEVELOPER_ID_INSTALLER")
  productbuild "${PB[@]}" "$DIST/$NAME.pkg"
  if [ -n "${NOTARY_PROFILE:-}" ] && [ -n "${DEVELOPER_ID_INSTALLER:-}" ]; then
    echo "== Notarization =="
    xcrun notarytool submit "$DIST/$NAME.pkg" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$DIST/$NAME.pkg"
  else echo "(ad-hoc signed; set DEVELOPER_ID_APP, DEVELOPER_ID_INSTALLER and NOTARY_PROFILE for a notarized installer)"; fi
  for v in "${VARIANTS[@]}"; do cp -R "$(art "$v" '*.app')" "$DIST/" 2>/dev/null || true; done
else
  mkdir -p "$DIST/VST3" "$DIST/Standalone"
  for v in "${VARIANTS[@]}"; do
    cp -R "$(art "$v" '*.vst3')" "$DIST/VST3/"
    find "build/${v}_artefacts" -path '*Standalone*' -type f -perm -u+x -exec cp {} "$DIST/Standalone/" \;
  done
fi
cp README.md "$DIST/"; cp docs/UserGuide.md "$DIST/"; cp -R docs/images "$DIST/images"
(cd dist && rm -f "$NAME.zip" && zip -q -r "$NAME.zip" "$NAME")
echo "== Done: $DIST  and  dist/$NAME.zip =="
if [ -n "${AUVAL_FAILED:-}" ]; then echo "auval reported failures"; exit 1; fi

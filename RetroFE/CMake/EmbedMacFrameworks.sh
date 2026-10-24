#!/bin/sh
# Copies frameworks and libraries into RetroFE.app/Contents/Frameworks and signs the
# bundle ad hoc, as the Xcode project's Embed Frameworks phase did. Run by CMake after
# linking RetroFE.app (RETROFE_MACOS_BUNDLE).
#
# Usage: EmbedMacFrameworks.sh RetroFE.app FROM_DIR ENTITLEMENTS ITEM...
#   ITEM is a framework or dylib in FROM_DIR, e.g. SDL2.framework or libminizip.dylib
set -e

bundle="$1"
from="$2"
entitlements="$3"
shift 3

frameworks="$bundle/Contents/Frameworks"
mkdir -p "$frameworks"

for item in "$@"; do
    rm -rf "$frameworks/$item"
    # ditto keeps the frameworks' symlinks (Versions/Current and the rest)
    ditto "$from/$item" "$frameworks/$item"
    case "$item" in
        *.framework)
            # Headers are only for building against it
            rm -rf "$frameworks/$item/Headers" "$frameworks/$item"/Versions/*/Headers
            ;;
    esac
done

# GStreamer's static libraries and libtool files are only for building against it too
gstreamer_lib="$frameworks/GStreamer.framework/Versions/1.0/lib"
if [ -d "$gstreamer_lib" ]; then
    find "$gstreamer_lib" -type f \( -name '*.a' -o -name '*.la' \) -delete
fi

# Ad hoc signatures (what Xcode's "Sign to Run Locally" gives): each embedded item, then
# the app with its entitlements. Anything that changes the bundle afterwards (the
# GStreamer slimming in build-macos.yml) signs it again.
for item in "$@"; do
    codesign --force --sign - "$frameworks/$item"
done
codesign --force --sign - --entitlements "$entitlements" "$bundle"

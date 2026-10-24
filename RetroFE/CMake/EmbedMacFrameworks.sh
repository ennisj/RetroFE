#!/bin/sh
# Copies frameworks and libraries into RetroFE.app/Contents/Frameworks and signs the
# bundle ad hoc, as the Xcode project's Embed Frameworks phase did. Run by CMake after
# linking RetroFE.app (RETROFE_MACOS_BUNDLE).
#
# Usage: EmbedMacFrameworks.sh RetroFE.app FROM_DIR ENTITLEMENTS ITEM...
#   ITEM is a framework or dylib in FROM_DIR, e.g. SDL2.framework or libminizip.dylib
#
# An item already in the bundle is copied again only when something in FROM_DIR/ITEM
# changed since it was (GStreamer.framework alone is over a gigabyte). Stamps beside
# the bundle, in .RetroFE.app-embedded, record when each was copied.
set -e

bundle="$1"
from="$2"
entitlements="$3"
shift 3

frameworks="$bundle/Contents/Frameworks"
stamps="$(dirname "$bundle")/.$(basename "$bundle")-embedded"
mkdir -p "$frameworks" "$stamps"

for item in "$@"; do
    stamp="$stamps/$item.stamp"
    if [ -e "$frameworks/$item" ] && [ -e "$stamp" ] &&
       [ -z "$(find "$from/$item" -newer "$stamp" -print -quit)" ]; then
        continue
    fi
    rm -f "$stamp"
    rm -rf "$frameworks/$item"
    # ditto keeps the frameworks' symlinks (Versions/Current and the rest)
    ditto "$from/$item" "$frameworks/$item"
    case "$item" in
        *.framework)
            # Headers are only for building against it
            rm -rf "$frameworks/$item/Headers" "$frameworks/$item"/Versions/*/Headers
            ;;
    esac
    if [ "$item" = "GStreamer.framework" ]; then
        # So are GStreamer's static libraries and libtool files
        find "$frameworks/$item/Versions/1.0/lib" -type f \( -name '*.a' -o -name '*.la' \) -delete
    fi
    # Ad hoc signatures (what Xcode's "Sign to Run Locally" gives)
    codesign --force --sign - "$frameworks/$item"
    touch "$stamp"
done

# Then the app with its entitlements, which changes with every link. Anything that
# changes the bundle afterwards (the GStreamer slimming in build-macos.yml) signs it again.
codesign --force --sign - --entitlements "$entitlements" "$bundle"

# LZMA SDK 26.04 (7-Zip decoder)

The C files RetroFE needs to read `.7z` archives (and 7-Zip self-extracting `.exe` files),
from the LZMA SDK by Igor Pavlov, which is in the public domain (`lzma-sdk.txt`).

- From `https://www.7-zip.org/a/lzma2604.7z` (2026-10-05), SHA-256
  `bd98058d0e12c5da970dd4e5e1d8fa24f2d704914d96e69c28f92ed45421a439`.
- Kept: the `C/` files that `C/Util/7z` (the SDK's `7zdec`) builds from, and the headers
  they include. Line endings converted to LF; nothing else changed.
- Built with `Z7_PPMD_SUPPORT` and `Z7_EXTRACT_ONLY`, as `7zdec` is.

Used by the Catalog to unpack emulator downloads (docs/design/emulator-downloads.md).
To update: take the same file list from a newer SDK.

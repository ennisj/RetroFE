# Aeon Nox generators

The fork's Aeon Nox (`Package/Environment/Common/layouts/Aeon Nox`) is made from
upstream RetroFE's theme by these two scripts. Both reproduce the committed files
byte for byte, so change the scripts rather than the generated files, and run them
again.

| Script | Writes |
|--------|--------|
| `gen_layouts.py <upstream folder> <output folder>` | `layout.xml`, `collections/_menu/`, `collections/Home/`, `collections/Catalog/` pages and texts, `forms/` pages and `splash.xml`, each page also as `layout 4x3.xml` |
| `gen_images.py <output folder>` | `images/background.png`, `images/form_focus.png`, the Home logos and icons and the Sources icon, drawn with Pillow from rectangles, ellipses, arcs and polygons (the logos use `fonts/BebasNeue.ttf` from the output folder) |

They need Python 3 and Pillow (the committed images were made with Pillow 11.3;
another version can compress the PNGs differently). The upstream folder is
`Package/Environment/Common/layouts/Aeon Nox` from
[phulshof/RetroFE](https://github.com/phulshof/RetroFE) at commit
`9f9230d09cbd4dae074b9465ca090567fd0623e7`, for example:

```sh
git clone --filter=blob:none --sparse https://github.com/phulshof/RetroFE upstream
git -C upstream sparse-checkout set "Package/Environment/Common/layouts/Aeon Nox"
git -C upstream checkout 9f9230d09cbd4dae074b9465ca090567fd0623e7
python Scripts/aeon-nox/gen_layouts.py "upstream/Package/Environment/Common/layouts/Aeon Nox" "Package/Environment/Common/layouts/Aeon Nox"
python Scripts/aeon-nox/gen_images.py "Package/Environment/Common/layouts/Aeon Nox"
```

Everything else in the folder (fonts and their licenses, upstream's images, sounds
and video, `credits.txt`, `readme.txt`, `NOTICE.txt`, and the hand-written
`collections/_videos/` pages) is kept as it is; NOTICE.txt says where each file
comes from.

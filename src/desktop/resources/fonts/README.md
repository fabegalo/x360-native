# Launcher fonts

One family on Windows, Linux and macOS: **Noto Sans CJK JP**, Regular and
Medium, Sans2.004. Both complete OTF files are embedded unchanged. There is no
system-font lookup, font merging, generated subset, synthetic weight or FreeType.
The medium face supplies headings and emphasis; the regular face supplies all
other text, including technical reports.

Source: https://github.com/notofonts/noto-cjk/tree/523d033d6cb47f4a80c58a35753646f5c3608a78/Sans/OTF/Japanese

| File | SHA-256 |
|---|---|
| NotoSansCJKjp-Regular.otf | 68a3fc98800b2a27b371f2fb79991daf3633bd89309d4ffaa6946fd587f375b5 |
| NotoSansCJKjp-Medium.otf | dd523e580e3413c480b2d701bf64e534c20f8419e3cfb6a44c2bdcd8d2a6c052 |

Copyright 2014–2021 Adobe. OFL-1.1: `licenses/launcher-fonts.txt`.
The distribution inventory records the license hash and both generated units
for all six platform/architecture combinations.

The complete files total about 32 MiB, traded for a single unmodified family
and no per-label font-generation pipeline. ImGui bakes glyphs on demand;
it does not rasterize the whole character set at startup. Coverage tests check
all 16 native language labels and the 42 displayed countries in both PT and EN.
This does not claim universal Unicode coverage or text shaping for every script.

The rasterizer remains stb_truetype. Sizes are 20 logical pixels for body text,
18 for captions, and 26/36 for headings. Direct drawing propagates framebuffer
density to every face so display changes do not reuse lower-resolution glyphs.
Single-line labels center on capital height; glyph advances remain pixel-aligned.

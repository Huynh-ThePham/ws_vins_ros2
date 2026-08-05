# IEEE Access LaTeX template assets

Official IEEE Access manuscript class expects `ieeeaccess.cls` plus:

- `Logo.png`, `notaglineLogo.png` (running header)
- `bullet.png` (end-of-document `\EOD` mark)
- bundled `IEEEtran.cls` (loaded by `ieeeaccess.cls`)

## Provenance

Files mirrored from the IEEE Access LaTeX package (community mirror:
https://github.com/hasantahir/Preparation-of-Papers-for-IEEE-ACCESS ).

Prefer re-downloading from the [IEEE Template Selector](https://template-selector.ieee.org/)
before camera-ready if a newer zip is available.

## Local patches (Tectonic / XeTeX)

Documented in `paper/sem_geodf/README.md`: CMYK blue fallback, caption/`xfigwd` fix,
biography counter, Access page stock `203.2×276.2 mm`, simpler headers.

Commercial Formata / Giovanni Std font maps in the original class are often
unavailable locally; TeX falls back to Times/Helvetica while keeping Access
layout macros (`\history`, `\doi`, `\address`, `\EOD`, …).

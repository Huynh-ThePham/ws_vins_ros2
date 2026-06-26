#!/usr/bin/env bash
# Build a .docx of the AECE manuscript to copy into the official AECE template.
# Pipeline: Markdown -> styled HTML (tables + embedded figure) -> LibreOffice -> .docx
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MD="$ROOT/docs/MANUSCRIPT_GeoDF-VINS-AECE.md"
HTML="$ROOT/docs/MANUSCRIPT_GeoDF-VINS-AECE.html"
OUTDIR="$ROOT/docs"
PROFILE="file:///tmp/lo_aece_profile"

python3 - "$MD" "$HTML" <<'PY'
import sys, os, re, base64, mimetypes
import markdown

md_path, html_path = os.path.abspath(sys.argv[1]), sys.argv[2]
text = open(md_path, encoding="utf-8").read()

# Inline figures as base64 data URIs so LibreOffice EMBEDS (not links) them.
def inline_img(m):
    alt, rel = m.group(1), m.group(2)
    ap = os.path.abspath(os.path.join(os.path.dirname(md_path), rel))
    mime = mimetypes.guess_type(ap)[0] or "image/png"
    with open(ap, "rb") as f:
        b64 = base64.b64encode(f.read()).decode("ascii")
    return f"![{alt}](data:{mime};base64,{b64})"

text = re.sub(r"!\[([^\]]*)\]\(([^)]+)\)", inline_img, text)

body = markdown.markdown(
    text,
    extensions=["tables", "fenced_code", "sane_lists", "attr_list"],
)

css = """
<style>
@page { size: A4; margin: 2cm; }
body { font-family: 'Times New Roman', serif; font-size: 11pt; line-height: 1.3; }
h1 { font-size: 17pt; } h2 { font-size: 13pt; } h3 { font-size: 11.5pt; }
table { border-collapse: collapse; width: 100%; font-size: 10pt; }
th, td { border: 1px solid #000; padding: 3px 6px; }
th { background: #e8e8e8; }
code, pre { font-family: 'Courier New', monospace; font-size: 10pt; }
pre { background: #f4f4f4; padding: 6px; border: 1px solid #ccc; }
img { max-width: 100%; }
</style>
"""

html = (f"<!DOCTYPE html><html><head><meta charset='utf-8'>{css}</head>"
        f"<body>{body}</body></html>")
open(html_path, "w", encoding="utf-8").write(html)
print("HTML written:", html_path, os.path.getsize(html_path), "bytes")
PY

echo "Converting to .docx via LibreOffice (headless)..."
soffice --headless -env:UserInstallation="$PROFILE" \
  --convert-to 'docx:MS Word 2007 XML' --outdir "$OUTDIR" "$HTML" 2>&1 \
  | grep -v 'javaldx' || true

rm -f "$HTML"
DOCX="$OUTDIR/MANUSCRIPT_GeoDF-VINS-AECE.docx"
if [ -f "$DOCX" ]; then
  echo "DOCX written: $DOCX"
  echo -n "Embedded media: "
  unzip -l "$DOCX" 2>/dev/null | grep -c 'word/media/' || echo 0
else
  echo "ERROR: docx not produced" >&2
  exit 1
fi

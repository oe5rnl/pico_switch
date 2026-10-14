#!/usr/bin/env bash
# Erzeugt die LVGL-Fonts mit deutschen Umlauten (Latin-1) fuer das CYD-Display.
# Die eingebauten lv_font_montserrat_* enthalten nur ASCII (keine Ä/Ö/Ü/ß).
# Quelle: Montserrat-Medium.ttf aus dem LVGL-Paket (gleicher Look wie die Built-ins).
# Aufruf: ./fonts/gen_fonts.sh   (benoetigt node/npx; einmalig nach Font-Aenderungen)
set -euo pipefail
cd "$(dirname "$0")/.."
TTF=".pio/libdeps/cyd/lvgl/scripts/built_in_font/Montserrat-Medium.ttf"
[ -f "$TTF" ] || { echo "Font fehlt: $TTF (erst 'pio run -e cyd' ausfuehren)"; exit 1; }
# ASCII + Latin-1-Ergaenzung (Ä Ö Ü ä ö ü ß ° ...) + Bullet + Euro
RANGE="0x20-0x7E,0xA0-0xFF,0x2022,0x20AC"
for size in 18 24; do
  npx --yes lv_font_conv --no-compress --no-prefilter --bpp 4 --size "$size" \
    --font "$TTF" -r "$RANGE" --format lvgl --force-fast-kern-format \
    --lv-font-name "lv_font_de_${size}" -o "src/fonts/lv_font_de_${size}.c"
done
echo "OK: src/fonts/lv_font_de_18.c, src/fonts/lv_font_de_24.c"

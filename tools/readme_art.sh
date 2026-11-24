#!/usr/bin/env bash
# readme_art.sh: renders the GitHub art from the site's own fonts and bust (headless Chromium + ImageMagick):
#   docs/images/readme/banner-dark.png, banner-light.png  README hero (1600x520, rounded, transparent corners)
#   website/img/github-social-preview.png                 repo social preview (1280x640, Settings > Social preview)
#   website/img/discussion-announcement.png, discussion-showcase.png
#                                                          header cards for Discussions (.github/DISCUSSION_TEMPLATE)
# The loops (docs/images/readme/loop-*.gif) are cut from website/media/*.mp4; the commands are in the README's
# HTML comment next to them. Run from the repository root; check the PNGs by eye, then tools/check_marks_media.py.
set -euo pipefail
root=$(pwd)
[ -f website/img/bust-512.png ] || { echo "run from the repository root" >&2; exit 2; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/readme-art.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

# render NAME W H THEME KIND OUT
render() {
    local name=$1 w=$2 h=$3 theme=$4 kind=$5 out=$6 bg fg muted accent tile radius
    if [ "$theme" = dark ]; then
        bg="radial-gradient(120% 140% at 18% 50%, #3a2112 0%, #1a130f 38%, #0f0f12 75%)"; fg="#ececf1"; muted="#a3a3ad"; accent="#f28c3c"
    else
        bg="radial-gradient(120% 140% at 18% 50%, #ffd9b8 0%, #fbeee2 40%, #f7f3ee 75%)"; fg="#17171c"; muted="#55555f"; accent="#c4581a"
    fi
    radius=24; [ "$kind" = social ] && radius=0
    local title="SculptForge" line="The free, open-source sculpting app." sub="Linux · macOS · Windows &nbsp;·&nbsp; free built-in AI" eyebrow="" size=21 bust=74 linesz=64
    case $kind in
        announce) eyebrow="Announcement"; sub="News from the SculptForge team";;
        showcase) eyebrow="Show and tell"; title="Made with SculptForge"; line="Share your work."; sub="Renders, turntables, works in progress"; size=13;;
        social) size=15; bust=62; linesz=50;;
    esac
    cat > "$tmp/$name.html" <<HTML
<!doctype html><html><head><meta charset="utf-8"><style>
@font-face { font-family: Inter; src: url("file://$root/website/fonts/Inter-Regular.ttf"); font-weight: 400; }
@font-face { font-family: Inter; src: url("file://$root/website/fonts/Inter-SemiBold.ttf"); font-weight: 600 700; }
html, body { margin: 0; width: ${w}px; height: ${h}px; background: transparent; overflow: hidden; }
.card { box-sizing: border-box; width: ${w}px; height: ${h}px; border-radius: ${radius}px; background: $bg; display: flex;
  align-items: center; gap: $((w / 22))px; padding: 0 $((w / 14))px; font-family: Inter, sans-serif; position: relative; overflow: hidden; }
.card::after { content: ""; position: absolute; inset: 0; border-radius: ${radius}px; box-shadow: inset 0 0 0 1px rgba(242,140,60,.18); }
.bust { height: $((h * bust / 100))px; flex: none; filter: drop-shadow(0 0 $((h / 12))px rgba(242,140,60,.35)); }
.eyebrow { color: $accent; font-weight: 600; letter-spacing: .14em; text-transform: uppercase; font-size: $((h / 26))px; margin: 0 0 $((h / 40))px; }
h1 { color: $fg; font-weight: 600; font-size: $((h * size / 100))px; letter-spacing: -.02em; line-height: 1; margin: 0; }
.line { color: $accent; font-size: $((h * linesz / 1000))px; white-space: nowrap; margin: $((h / 22))px 0 0; }
.sub { color: $muted; font-size: $((h * 44 / 1000))px; margin: $((h / 36))px 0 0; }
</style></head><body><div class="card"><img class="bust" src="file://$root/website/img/bust-512.png" alt="">
<div>$( [ -n "$eyebrow" ] && echo "<p class=\"eyebrow\">$eyebrow</p>")<h1>$title</h1><p class="line">$line</p><p class="sub">$sub</p></div></div></body></html>
HTML
    chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
        --default-background-color=00000000 --window-size="$w,$h" --screenshot="$tmp/$name.png" "file://$tmp/$name.html" >/dev/null 2>&1
    if [ "$kind" = social ]; then
        magick "$tmp/$name.png" -alpha off -strip -define png:compression-level=9 "$out"
    else
        magick "$tmp/$name.png" -strip -define png:compression-level=9 "$out"
    fi
    echo "$out $(stat -c %s "$out") bytes"
}

render banner-dark 1600 520 dark banner docs/images/readme/banner-dark.png
render banner-light 1600 520 light banner docs/images/readme/banner-light.png
render social 1280 640 dark social website/img/github-social-preview.png
render announce 1600 520 dark announce website/img/discussion-announcement.png
render showcase 1600 520 dark showcase website/img/discussion-showcase.png

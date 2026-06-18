#!/usr/bin/env bash
set -euo pipefail

HL=/home/rayb/Projects/hyprland
OUT=/home/rayb/Projects/hyprtasking/.hl055
ROOT=$OUT/include/hyprland
PROTO_OUT=$ROOT/protocols

WP=$(pkg-config --variable=pkgdatadir wayland-protocols)
HP=$(pkg-config --variable=pkgdatadir hyprland-protocols)
WS=$(pkg-config --variable=pkgdatadir wayland-scanner)

rm -rf "$ROOT"
mkdir -p "$ROOT" "$PROTO_OUT"

echo ">> generating shader includes in sibling"
( cd "$HL" && sh scripts/generateShaderIncludes.sh >/dev/null )

echo ">> copying v0.55 src headers"
# copy the whole src tree (headers + generated shaders + version template area)
rsync -a --include='*/' \
      --include='*.hpp' --include='*.h' --include='*.inc' --include='*.in' \
      --exclude='*' "$HL/src/" "$ROOT/src/"

echo ">> generating version.h"
GIT_HASH=$(cd "$HL" && git rev-parse HEAD)
GIT_TAG=$(cd "$HL" && git describe --tags 2>/dev/null || echo v0.55.0)
sed -e "s/@GIT_COMMIT_HASH@/$GIT_HASH/g" \
    -e "s/@GIT_BRANCH@/main/g" \
    -e "s/@GIT_COMMIT_MESSAGE@/build/g" \
    -e "s/@GIT_COMMIT_DATE@/2026/g" \
    -e "s/@GIT_DIRTY@//g" \
    -e "s/@GIT_TAG@/$GIT_TAG/g" \
    -e "s/@GIT_COMMITS@/0/g" \
    -e "s/@AQUAMARINE_VERSION@/0.10.0/g" \
    -e "s/@AQUAMARINE_VERSION_MAJOR@/0/g" \
    -e "s/@AQUAMARINE_VERSION_MINOR@/10/g" \
    -e "s/@AQUAMARINE_VERSION_PATCH@/0/g" \
    -e "s/@HYPRLANG_VERSION@/0.6.8/g" \
    -e "s/@HYPRUTILS_VERSION@/0.11.1/g" \
    -e "s/@HYPRCURSOR_VERSION@/0.1.13/g" \
    -e "s/@HYPRGRAPHICS_VERSION@/0.2.6/g" \
    "$HL/src/version.h.in" > "$ROOT/src/version.h"

echo ">> parsing protocol list from CMakeLists"
# collapse the protocol-call region into single-line calls and extract args
CALLS=$(tr '\n' ' ' < "$HL/CMakeLists.txt" \
        | grep -oE 'protocolnew\([^)]*\)' || true)

gen_one() {
    local xml="$1"
    if [[ ! -f "$xml" ]]; then
        echo "!! MISSING XML: $xml" >&2
        return 1
    fi
    hyprwayland-scanner "$xml" "$PROTO_OUT/" >/dev/null 2>&1
}

echo "$CALLS" | while read -r call; do
    [[ -z "$call" ]] && continue
    # args are quoted strings + a bool
    args=$(echo "$call" | grep -oE '"[^"]*"' | tr -d '"')
    path=$(echo "$args" | sed -n '1p')
    name=$(echo "$args" | sed -n '2p')
    ext=$(echo "$call" | grep -oE '(true|false)' | tail -1)
    [[ -z "$name" ]] && continue
    if [[ "$ext" == "false" ]]; then
        xml="$WP/$path/$name.xml"
    elif [[ "$path" == "protocols" ]]; then
        xml="$HL/protocols/$name.xml"
    else
        # ${HYPRLAND_PROTOCOLS}/protocols
        xml="$HP/protocols/$name.xml"
    fi
    gen_one "$xml" && echo "   ok: $name"
done

echo ">> generating wayland.hpp (core enums)"
hyprwayland-scanner --wayland-enums "$WS/wayland.xml" "$PROTO_OUT/" >/dev/null 2>&1 && echo "   ok: wayland"

echo ">> writing hyprland.pc"
cat > "$OUT/hyprland.pc" <<EOF
prefix=$OUT
Name: hyprland
Description: Hyprland headers (v0.55 vendored for plugin build)
Version: 0.55.0
Cflags: -I\${prefix}/include -I\${prefix}/include/hyprland/protocols -I\${prefix}/include/hyprland -I\${prefix}/include/hyprland/src -DWLR_USE_UNSTABLE
EOF

echo ">> done. protocol headers: $(ls "$PROTO_OUT" | wc -l)"

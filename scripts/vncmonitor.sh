#!/usr/bin/bash

set -euo pipefail  # safe mode
export LC_ALL=C    # speed

# Add this to your hyprland config:
# monitor=VIRTUAL,1920x1080@60,auto-center-up,1
# === CONFIG ===
IP=0.0.0.0
PORT=5900
FPS=60
VIRTUAL_MONITOR=VIRTUAL
rows=$(hyprctl getoption plugin:hyprtasking:grid:rows -j | jq -r '.int')
cols=$(hyprctl getoption plugin:hyprtasking:grid:cols -j | jq -r '.int')
layers=$(hyprctl getoption plugin:hyprtasking:grid:layers -j | jq -r '.int')
active_workspace=$(hyprctl activeworkspace -j | jq -r .id)
VIRTUAL_WORKSPACE=$((active_workspace + (rows*cols*layers)))
REAL_MONITOR=$(hyprctl monitors -j | jq -r '.[] | select(.focused == true) .name')

# === SCRIPT NAME ===
S=$(basename "$0")

# === CLEANUP FUNCTION ===
cleanup() {
  set +e
  echo -e "\n[$S] Cleaning up..."
  hyprctl dispatch moveworkspacetomonitor "$VIRTUAL_WORKSPACE" "$REAL_MONITOR"
  hyprctl dispatch focusmonitor "$REAL_MONITOR"
  pkill wayvnc
  hyprctl output remove "$VIRTUAL_MONITOR"
  echo "[$S] Done."
  exit 0
}

# === Trap Exit for Cleanup ===
trap cleanup INT TERM

# === Check if virtual monitor is already active, create it if not ===
if ! hyprctl monitors | grep -q "$VIRTUAL_MONITOR"; then
  echo "[$S] Creating $VIRTUAL_MONITOR dynamically..."
  hyprctl output create headless "$VIRTUAL_MONITOR"
  sleep 0.5
fi

# === Assign workspace and activate it ===
# echo "[$S] Moving workspace $VIRTUAL_WORKSPACE to $VIRTUAL_MONITOR..."
# hyprctl dispatch moveworkspacetomonitor $VIRTUAL_WORKSPACE $VIRTUAL_MONITOR
# sleep 0.2
# hyprctl dispatch workspace $VIRTUAL_WORKSPACE
# sleep 0.2

# === Return focus to your real monitor so you don't get stuck on the headless one ===
hyprctl dispatch focusmonitor "$REAL_MONITOR"
sleep 0.2
hyprctl dispatch workspace "$active_workspace"

# === Start WayVNC ===
echo "[$S] Starting WayVNC on $VIRTUAL_MONITOR..."
wayvnc "$IP" "$PORT" --gpu --max-fps="$FPS" --output="$VIRTUAL_MONITOR" "$@"

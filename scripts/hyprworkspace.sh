#!/usr/bin/bash

set -euo pipefail  # safe mode
export LC_ALL=C    # speed

reload_vars() {
    rows=$(hyprctl getoption plugin:hyprtasking:grid:rows | head -n1 | cut -d' ' -f2)
    cols=$(hyprctl getoption plugin:hyprtasking:grid:cols | head -n1 | cut -d' ' -f2)
    # layers=$(hyprctl getoption plugin:hyprtasking:grid:layers | head -n1 | cut -d' ' -f2)
    wsm=$((rows*cols))
}
reload_vars

ws=$(hyprctl activeworkspace | head -n1 | cut -d' ' -f3)
ws=$((ws%wsm - 1))
dump() {
    json='{"text":"'
    for ((i=0; i<rows; i++)); do
        for ((j=0; j<cols; j++)); do
            c_w=$((i*rows+j))
            if [ $ws == $c_w ]; then
                json+='   '
            else
                json+='███'
            fi
        done
        json+='\n'
    done
    json+='"}'
    echo "$json"
}
dump

handle() {
  case $1 in
    workspacev2*)
        ws=${1:13}
        ws=${ws%,*}
        ws=$(((ws-1)%wsm))
        dump
        ;;
    configreloaded*)
        reload_vars
        ;;
  esac
}

socat -U - "UNIX-CONNECT:$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket2.sock" | while read -r line; do handle "$line"; done

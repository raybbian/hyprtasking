## Here are some scripts that you might find useful

### [vncmonitor.sh](./vncmonitor.sh)

Creates a headless (virtual) monitor and starts wayvnc there, so you can have a setup with multiple monitors without having to actually buy multiple monitors.

This script is based on [Ghost Monitor Wayland](https://github.com/Zellington3/Ghost-Monitor-Wayvnc-Hyprland) and includes some hyprtasking-specific modifications to work with layers.

Usage:

```
vncmonitor.sh
```

### [hyprworkspace.sh](./hyprworkspace.sh)

This script recreates waybar workspace indicator from [hyprkool](https://github.com/thrombe/hyprkool). It's useful for understanding on which workspace in a grid you are currently on without having to open the overview every time.

Usage:

#### `~/.config/waybar/config.jsonc`:

```jsonc
// Add it to modules
"modules-right": [/* other modules, ... */ "custom/hyprtasking"],

// ...
"custom/hyprtasking": {
    "exec": "hyprworkspace.sh",
    "return-type": "json",
    "tooltip": false,
},
```

#### `~/.config/waybar/style.css`:

```css
#custom-hyprtasking {
    padding: 0px 10px 0px 10px;
    font-size: 0.177em;
    font-family: 'JetBrains Mono Nerd Font';
    letter-spacing: -1px;
}
```


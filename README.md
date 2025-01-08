# Hyprtasking

A plugin for Hyprland that allows for complete control over your windows and workspaces.

- Supports Hyprland release `v0.46.2`.
- Support for Hyprland-git **HAS BEEN DROPPED** until the rendering rewrite is more mature!! If you'd still like to use it, build from the `git-archive` branch.

https://github.com/user-attachments/assets/8d6cdfd2-2b17-4240-a117-1dbd2231ed4e

> This plugin's current workflow is very similar to that of [hyprexpo](https://github.com/hyprwm/hyprland-plugins/tree/main/hyprexpo). Check it out if you'd like something more polished and performant.

### [Jump To Installation](#Installation)

### [See Configuration](#Configuration)

## Plugin Compatibility

- No clue, open an issue if something goes wrong

## Roadmap

- [x] Mouse controls
    - [x] Exit into workspace (hover, click)
    - [x] Drag and drop windows
- [ ] Keyboard controls
    - [x] Switch workspaces with direction
    - [ ] Switch workspaces with absolute number
- [x] Multi-monitor support (tested)
- [x] Monitor scaling support (tested)
- [x] Animation support
- [ ] Configurability
    - [x] Overview exit behavior
    - [x] Number of visible workspaces
    - [ ] Custom workspace layouts
    - [x] Toggle behavior
    - [x] Toggle keybind
- [ ] Touch and gesture support
    
## Installation

### Hyprpm

```
hyprpm add https://github.com/raybbian/hyprtasking
hyprpm enable hyprtasking
```

### Nix

Add hyprtasking to your flake inputs
```nix
# flake.nix
{
  inputs = {
    hyprland.url = "github:hyprwm/Hyprland/v0.46.2";

    hyprtasking = {
      url = "github:raybbian/hyprtasking";
      inputs.hyprland.follows = "hyprland";
    };
  };
  # ...
}

```

Include the plugin in the hyprland home manager options

```nix
# home.nix
{ inputs, ... }:
{
  wayland.windowManager.hyprland = {
    plugins = [
      inputs.hyprtasking.packages.${pkgs.system}.hyprtasking
    ];
  }
}
```

### Manual

To build, have hyprland headers installed on the system and then:

```
meson setup build
cd build && meson compile
```

Then use `hyprctl plugin load` to load the absolute path to the `.so` file.

## Usage

### Opening Overview

- Bind `hyprtasking:toggle, all` to a keybind to open/close the overlay on all monitors. 
- See [below](#Configuration) for configuration options.

### Interaction

- Workspace Transitioning:
    - Open the overlay, then use **right click** to switch to a workspace
    - Use the directional dispatchers `hyprtasking:move` to switch to a workspace
- Window management:
    - **Left click** to drag and drop windows around

## Configuration

Example below:

```
bind = $mainMod, tab, hyprtasking:toggle, all
bind = $mainMod, space, hyprtasking:toggle, cursor
bind = $mainMod, x, hyprtasking:killhovered

bind = $mainMod, h, hyprtasking:move, left
bind = $mainMod, j, hyprtasking:move, down
bind = $mainMod, k, hyprtasking:move, up
bind = $mainMod, l, hyprtasking:move, right

plugin {
    hyprtasking {
        rows = 3
        exit_behavior = original hovered interacted
        gap_size = 8
        bg_color = $love
        border_size = 4
    }
}

```

### Dispatchers

- `hyprtasking:toggle, ARG` takes in 1 argument that is either `cursor` or `all`
    - if the argument is `all`, then
        - if all overviews are hidden, then all overviews will be shown
        - otherwise all overviews will be hidden
    - if the argument is `cursor`, then
        - if current monitor's overview is hidden, then it will be shown
        - otherwise all overviews will be hidden

- `hyprtasking:move, ARG` takes in 1 argument that is one of `up`, `down`, `left`, `right`
    - when dispatched, hyprtasking will switch workspaces with a nice animation

- `hyprtasking:killhovered` behaves similarly to the standard `killactive` dispatcher with focus on hover
    - when dispatched, hyprtasking will the currently hovered window, useful when the overview is active.
    - this dispatcher is designed to **replace** killactive, it will work even when the overview is **not active**.

### Config Options

- `plugin:hyprtasking:rows` (int): The number of rows (and columns) of workspaces to show

- `plugin:hyprtasking:gap_size` (int): The width (logical pixels) of the vertical gaps between workspaces

- `plugin:hyprtasking:bg_color` (color): The background color that does not include 

- `plugin:hyprtasking:border_size` (int): The width (logical pixels) of the borders around workspaces

- `plugin:hyprtasking:exit_behavior` (str): A space-separated list of `{'hovered', 'interacted', 'original'}`
    - When an overview is about to hide, hyprtasking will evaluate these strings in order
        - If the string is `'hovered'`, hyprtasking will attempt to switch to the hovered workspace
        - If the string is `'interacted'`, hyprtasking will attempt to switch to the last interacted workspace (window drag/drop, overview show/hide)
        - If the string is `'original'`, hyprtasking will attempt to switch to the workspace in which the overview was shown initially
    - If hyprtasking fails to do any of the above, it will move to the next string in the list

<div align="center">
  <h1>Hyprtasking</h1>
  <p>Powerful workspace management plugin, packed with features.</p>
</div>

> [!Important]
> - Supports Hyprland release `v0.46.2-v0.49.0`.
> - The `main` branch will compile for `v0.49.0` but is missing the `movewindow` dispatcher.
> - The `hyprland-v0.48.1` branch includes the `movewindow` dispatcher but deprecates the `exit_behavior` configuration.

https://github.com/user-attachments/assets/8d6cdfd2-2b17-4240-a117-1dbd2231ed4e

#### [Jump To Installation](#Installation)

#### [See Configuration](#Configuration)

## Roadmap

- [ ] Modular Layouts
    - [x] Grid layout
    - [x] Linear layout
    - [ ] Minimap layout
- [x] Mouse controls
    - [x] Exit into workspace (hover, click)
    - [x] Drag and drop windows
- [ ] Keyboard controls
    - [x] Switch workspaces with direction
    - [ ] Switch workspaces with absolute number
- [x] Multi-monitor support (tested)
- [x] Monitor scaling support (tested)
- [x] Animation support
- [x] Configurability
    - [x] Overview exit behavior
    - [x] Number of visible workspaces
    - [x] Custom workspace layouts
    - [x] Toggle behavior
    - [x] Toggle keybind
- [ ] Touch and gesture support
- [ ] Overview layers
    
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
- Bind `hyprtasking:toggle, cursor` to a keybind to open the overlay on one monitor and close on all monitors.
- Swipe up/down on a touchpad device to open/close the overlay on one monitor.
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
bind = SUPER, tab, hyprtasking:toggle, cursor
bind = SUPER, space, hyprtasking:toggle, all

bind = SUPER, X, hyprtasking:killhovered

bind = SUPER, H, hyprtasking:move, left
bind = SUPER, J, hyprtasking:move, down
bind = SUPER, K, hyprtasking:move, up
bind = SUPER, L, hyprtasking:move, right

plugin {
    hyprtasking {
        layout = grid

        gap_size = 20
        bg_color = 0xff26233a
        border_size = 4

        gestures {
            enabled = true
            open_fingers = 3
            open_distance = 300
            open_positive = true
        }

        grid {
            rows = 3
            cols = 3
        }

        linear {
            height = 400
            scroll_speed = 1.1
            blur = 0
        }
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

- `hyprtasking:movewindow, ARG` takes in 1 argument that is one of `up`, `down`, `left`, `right`
    - when dispatched, hyprtasking will 1. move the hovered window to the workspace in the given direction relative to the window, and 2. switch to that workspace.

- `hyprtasking:killhovered` behaves similarly to the standard `killactive` dispatcher with focus on hover
    - when dispatched, hyprtasking will the currently hovered window, useful when the overview is active.
    - this dispatcher is designed to **replace** killactive, it will work even when the overview is **not active**.

### Config Options

All options should are prefixed with `plugin:hyprtasking:`.

| Option | Type | Description | Default |
| --- | --- | --- | --- |
| `layout` | `Hyprlang::STRING` | The layout to use, either `grid` or `linear` | `grid` |
| `bg_color` | `Hyprlang::INT` | The color of the background of the overlay | `0x000000FF` |
| `gap_size` | `Hyprlang::FLOAT` | The width in logical pixels of the gaps between workspaces | `8.f` |
| `border_size` | `Hyprlang::FLOAT` | The width in logical pixels of the borders around workspaces | `4.f` |
| `exit_behavior` (deprecated) | `Hyprlang::STRING` | [Determines which workspace to exit to](#exit-behavior-deprecated) when closed by keybind | `active hovered interacted original` |
| `gestures:enabled` | `Hyprlang::INT` | Whether or not to enable gestures | `1` |
| `gestures:open_fingers` | `Hyprlang::INT` | The number of fingers to use for the "open" gesture | `3` |
| `gestures:open_distance` | `Hyprlang::FLOAT` | How large of a swipe on the touchpad is needed for the "open" gesture | `300.f` |
| `gestures:open_positive` | `Hyprlang::INT` | `1` if swiping up should open the overlay, `0` otherwise | `1` |
| `grid:rows` | `Hyprlang::INT` | The number of rows to display on the grid overlay | `3` |
| `grid:cols` | `Hyprlang::INT` | The number of columns to display on the grid overlay | `3` |
| `linear:blur` | `Hyprlang::INT` | Whether or not to blur the dimmed area | `0` |
| `linear:height` | `Hyprlang::FLOAT` | The height of the linear overlay in logical pixels | `300.f` |
| `linear:scroll_speed` | `Hyprlang::FLOAT` | Scroll speed modifier. Set negative to flip direction | `1.f` |

#### Exit Behavior (Deprecated)

- When an overview is about to hide, hyprtasking will evaluate these strings in order
    - If the string is `'hovered'`, hyprtasking will attempt to switch to the hovered workspace
    - If the string is `'interacted'`, hyprtasking will attempt to switch to the last interacted workspace (window drag/drop)
    - If the string is `'original'`, hyprtasking will attempt to switch to the workspace in which the overview was shown initially
    - (Fallback) If the string is `'active'`, hyprtasking will switch to the monitor's active workspace.


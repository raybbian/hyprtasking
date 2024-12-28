# Hyprtasking

A plugin for Hyprland that allows for complete control over your windows and workspaces.

- Supports Hyprland release `v0.46.2`. 

https://github.com/user-attachments/assets/44cfd319-c321-4721-a34f-c428d147b811

> This plugin's current workflow is very similar to that of [hyprexpo](https://github.com/hyprwm/hyprland-plugins/tree/main/hyprexpo). Check it out if you'd like something more polished and performant.

### [Jump To Installation](#Installation)

## Plugin Compatibility

- No clue, open an issue if something goes wrong

## Roadmap

- [x] Mouse controls
    - [x] Exit into workspace
    - [x] Drag and drop windows
- [x] Multi-monitor support (tested)
- [x] Monitor scaling support (tested)
- [x] Animation support
- [ ] Configurability
    - [x] Number of visible workspaces
    - [ ] Custom workspace layouts
    - [x] Toggle keybind
- [ ] Touch and gesture support
    
## Installation

### Hyprpm

```
hyprpm add https://github.com/raybbian/
hyprpm enable hyprtasking
```

### Manual

To build, have hyprland headers installed on the system and then:

```
make all
```

Then use `hyprctl plugin load` to load the absolute path to the `.so` file.

## Usage

### Opening Overview

- Bind `hyprtasking:toggle` to a keybind to open/close the overlay

### Interaction

- Window management:
    - **Left click** to drag and drop windows around
- Exiting:
    - Trigger `hyprtasking:toggle` again
    - Press **ESC** to close the overlay
    - This will change workspaces to the workspace under your cursor

## Configuration

See below:

```
bind = $mainMod, tab, hyprtasking:toggle

plugin {
    hyprtasking {
        rows = 2
    }
}
```

#include "overview.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/cursor/CursorShapeOverrideController.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Box.hpp>

#include "config.hpp"
#include "globals.hpp"
#include "layout/grid.hpp"
#include "layout/linear.hpp"
#include "src/desktop/state/FocusState.hpp"

HTView::HTView(MONITORID in_monitor_id) {
    monitor_id = in_monitor_id;
    active = false;
    closing = false;
    navigating = false;

    change_layout(HTConfig::value<Hyprlang::STRING>("layout"));
}

void HTView::change_layout(const std::string& layout_name) {
    if (layout != nullptr && layout->layout_name() == layout_name) {
        layout->init_position();
        return;
    }

    if (layout_name == "grid") {
        layout = makeShared<HTLayoutGrid>(monitor_id);
    } else if (layout_name == "linear") {
        layout = makeShared<HTLayoutLinear>(monitor_id);
    } else {
        fail_exit(
            "Bad overview layout name {}, supported ones are 'grid' and 'linear'",
            layout_name
        );
    }
}

void HTView::do_exit_behavior(bool exit_on_mouse) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    auto try_get_hover_id = [this, &monitor]() {
        const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
        if (cursor_monitor != monitor)
            return WORKSPACE_INVALID;

        const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
        return layout->get_ws_id_from_global(mouse_coords);
    };

    const int EXIT_ON_HOVERED = HTConfig::value<Hyprlang::INT>("exit_on_hovered");

    const bool use_hovered = exit_on_mouse || EXIT_ON_HOVERED;
    WORKSPACEID ws_id = use_hovered ? try_get_hover_id() : monitor->m_activeWorkspace->m_id;
    PHLWORKSPACE workspace = use_hovered ? layout->get_workspace_from_layout(ws_id)
                                         : monitor->m_activeWorkspace;

    if (use_hovered && ws_id == WORKSPACE_INVALID && layout->layout_name() == "grid") {
        const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
        if (cursor_monitor == monitor) {
            const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
            const SP<HTLayoutGrid> grid_layout = Hyprutils::Memory::dynamicPointerCast<HTLayoutGrid, HTLayoutBase>(layout);
            if (grid_layout != nullptr) {
                const auto [cell_x, cell_y, cell_layer] = grid_layout->get_grid_cell_from_global(mouse_coords);
                if (cell_x >= 0 && cell_y >= 0 && cell_layer >= 0) {
                    grid_layout->layer = cell_layer;
                    const int ROWS = HTConfig::value<Hyprlang::INT>("grid:rows");
                    const int COLS = HTConfig::value<Hyprlang::INT>("grid:cols");
                    const int ws_per_layer = std::max(1, ROWS * COLS);
                    const int target_slot = cell_layer * ws_per_layer + cell_y * COLS + cell_x;

                    WORKSPACEID next_id = 1;
                    for (PHLWORKSPACE ws : g_pCompositor->getWorkspacesCopy()) {
                        if (ws != nullptr && !ws->m_isSpecialWorkspace && ws->m_id >= next_id) {
                            next_id = ws->m_id + 1;
                        }
                    }

                    workspace = g_pCompositor->createNewWorkspace(next_id, monitor->m_id, "", false);
                    if (workspace != nullptr) {
                        grid_layout->pin_workspace_to_slot(next_id, target_slot);
                        ws_id = next_id;
                        Log::logger->log(
                            LOG,
                            "[Hyprtasking] Created workspace {} at slot ({}, {}) on right-click exit",
                            next_id,
                            cell_x,
                            cell_y
                        );
                    }
                }
            }
        }
    }

    if (workspace == nullptr)
        return;

    monitor->changeWorkspace(workspace);
}

void HTView::show(bool recalculate) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;
    const PHLWORKSPACE active_workspace = monitor->m_activeWorkspace;
    if (active_workspace == nullptr)
        return;

    active = true;
    closing = false;
    navigating = false;

    if (recalculate) {
        layout->init_position();
    }
    layout->on_show();

    Cursor::overrideController->setOverride("left_ptr", Cursor::CURSOR_OVERRIDE_UNKNOWN);

    g_pHyprRenderer->damageMonitor(monitor);
    g_pCompositor->scheduleFrameForMonitor(monitor);
}

void HTView::hide(bool exit_on_mouse) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;
    const PHLWORKSPACE active_workspace = monitor->m_activeWorkspace;
    if (active_workspace == nullptr)
        return;

    do_exit_behavior(exit_on_mouse);

    active = true;
    closing = true;
    navigating = false;

    layout->on_hide([this](auto self) {
        active = false;
        closing = false;
    });

    Cursor::overrideController->unsetOverride(Cursor::CURSOR_OVERRIDE_UNKNOWN);

    g_pHyprRenderer->damageMonitor(monitor);
    g_pCompositor->scheduleFrameForMonitor(monitor);
}

void HTView::warp_window(Hyprlang::INT warp, PHLWINDOW window) {
    // taken from Hyprland:
    // https://github.com/hyprwm/Hyprland/blob/ea42041f936d5810c5cfa45d6bece12dde2fd9b6/src/managers/KeybindManager.cpp#L1319
    if (warp > 0) {
        auto HLSurface = Desktop::View::CWLSurface::fromResource(g_pSeatManager->m_state.pointerFocus.lock());

        if (window && (!HLSurface || HLSurface->view()))
            window->warpCursor(warp == 2);
    }
}

void HTView::move_id(WORKSPACEID ws_id, bool move_window) {
    navigating = false;
    if (closing)
        return;
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;
    const PHLWORKSPACE active_workspace = monitor->m_activeWorkspace;
    if (active_workspace == nullptr)
        return;

    // FIXME: weird hovered window duplicate code
    PHLWINDOW hovered_window = ht_manager->get_window_from_cursor();
    bool should_move = true;
    if (hovered_window == nullptr && move_window)
        should_move = false;

    layout->build_overview_layout(HT_VIEW_CLOSED);
    PHLWORKSPACE other_workspace = layout->get_workspace_from_layout(ws_id);
    if (other_workspace == nullptr)
        return;

    if (move_window && should_move) {
        g_pCompositor->moveWindowToWorkspaceSafe(hovered_window, other_workspace);
    }

    Hyprlang::INT warp;

    monitor->changeWorkspace(other_workspace);
    if (move_window) {
        Desktop::focusState()->fullWindowFocus(hovered_window, Desktop::FOCUS_REASON_CLICK);
        warp = *CConfigValue<Hyprlang::INT>("plugin:hyprtasking:warp_on_move_window");
    } else {
        warp = *CConfigValue<Hyprlang::INT>("cursor:warp_on_change_workspace");
    }
    warp_window(warp, hovered_window);

    navigating = true;
    layout->on_move(active_workspace->m_id, other_workspace->m_id, [this](auto self) {
        navigating = false;
    });
}

void HTView::move(std::string arg, bool move_window) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;
    const PHLWORKSPACE active_workspace = monitor->m_activeWorkspace;
    if (active_workspace == nullptr)
        return;
    PHLWINDOW hovered_window = ht_manager->get_window_from_cursor();
    if (hovered_window == nullptr && move_window)
        return;

    if (!active && !navigating)
        layout->init_position();

    // if moving a window, the up/down/left/right should be relative to the window (and cursor) and not necessarily the active workspace
    const WORKSPACEID source_ws_id =
        move_window ? hovered_window->workspaceID() : active_workspace->m_id;
    layout->build_overview_layout(HT_VIEW_CLOSED);
    const auto ws_layout_it = layout->overview_layout.find(source_ws_id);
    if (ws_layout_it == layout->overview_layout.end())
        return;
    const auto ws_layout = ws_layout_it->second;
    const WORKSPACEID id = layout->get_ws_id_in_direction(ws_layout.x, ws_layout.y, arg);

    if (id == WORKSPACE_INVALID && layout->layout_name() == "grid") {
        const SP<HTLayoutGrid> grid_layout = Hyprutils::Memory::dynamicPointerCast<HTLayoutGrid, HTLayoutBase>(layout);
        if (grid_layout != nullptr) {
            int x = ws_layout.x, y = ws_layout.y;
            if (arg == "up") {
                y--;
            } else if (arg == "down") {
                y++;
            } else if (arg == "right") {
                x++;
            } else if (arg == "left") {
                x--;
            }
            const int ROWS = HTConfig::value<Hyprlang::INT>("grid:rows");
            const int COLS = HTConfig::value<Hyprlang::INT>("grid:cols");
            if (x >= 0 && x < COLS && y >= 0 && y < ROWS) {
                const int ws_per_layer = std::max(1, ROWS * COLS);
                const int target_slot = grid_layout->layer * ws_per_layer + y * COLS + x;
                WORKSPACEID next_id = 1;
                for (PHLWORKSPACE ws : g_pCompositor->getWorkspacesCopy()) {
                    if (ws != nullptr && !ws->m_isSpecialWorkspace && ws->m_id >= next_id) {
                        next_id = ws->m_id + 1;
                    }
                }
                PHLWORKSPACE new_ws = g_pCompositor->createNewWorkspace(next_id, monitor->m_id, "", false);
                if (new_ws != nullptr) {
                    grid_layout->pin_workspace_to_slot(next_id, target_slot);
                    move_id(next_id, move_window);
                    return;
                }
            }
        }
    }

    move_id(id, move_window);
}

PHLMONITOR HTView::get_monitor() {
    const PHLMONITOR monitor = g_pCompositor->getMonitorFromID(monitor_id);
    if (monitor == nullptr)
        Log::logger->log(Log::WARN, "[Hyprtasking] Returning null monitor from get_monitor!");
    return monitor;
}

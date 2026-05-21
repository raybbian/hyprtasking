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
#include "workspace.hpp"

namespace {
void exit_to_grid_cell(
    SP<HTLayoutGrid> grid,
    PHLMONITOR monitor,
    Vector2D mouse_coords,
    WORKSPACEID& ws_id,
    PHLWORKSPACE& workspace
) {
    if (grid == nullptr || monitor == nullptr)
        return;

    const auto [cell_x, cell_y, cell_layer] = grid->get_grid_cell_from_global(mouse_coords);
    if (cell_x < 0 || cell_y < 0 || cell_layer < 0)
        return;

    grid->layer = cell_layer;
    const int ROWS = static_cast<Hyprlang::INT>(HTConfig::value("grid:rows"));
    const int COLS = static_cast<Hyprlang::INT>(HTConfig::value("grid:cols"));
    const int ws_per_layer = std::max(1, ROWS * COLS);
    const int target_slot = cell_layer * ws_per_layer + cell_y * COLS + cell_x;

    workspace = create_workspace_for_monitor(monitor);
    if (workspace == nullptr)
        return;

    grid->pin_workspace_to_slot(workspace->m_id, target_slot);
    ws_id = workspace->m_id;
    Log::logger->log(
        LOG,
        "[Hyprtasking] Using workspace {} at slot ({}, {}) on right-click exit",
        workspace->m_id,
        cell_x,
        cell_y
    );
}
}

HTView::HTView(MONITORID in_monitor_id) {
    monitor_id = in_monitor_id;
    active = false;
    closing = false;
    navigating = false;
    pre_overview_workspace = WORKSPACE_INVALID;
    marked_workspace = WORKSPACE_INVALID;

    if (PHLMONITOR monitor = g_pCompositor->getMonitorFromID(monitor_id))
        monitor_name = monitor->m_name;

    change_layout(HTConfig::value_string("layout"));
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

    const int EXIT_ON_HOVERED = static_cast<Hyprlang::INT>(HTConfig::value("exit_on_hovered"));

    const bool use_hovered = exit_on_mouse || (EXIT_ON_HOVERED && marked_workspace == WORKSPACE_INVALID);
    const PHLWORKSPACE hovered_workspace = use_hovered ? layout->get_workspace_from_layout(try_get_hover_id()) : nullptr;

    WORKSPACEID ws_id = use_hovered && hovered_workspace != nullptr ? hovered_workspace->m_id
                         : marked_workspace != WORKSPACE_INVALID   ? marked_workspace
                                                                     : pre_overview_workspace;
    PHLWORKSPACE workspace = layout->get_workspace_from_layout(ws_id);

    if (use_hovered && ws_id == WORKSPACE_INVALID) {
        const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
        if (cursor_monitor == monitor)
            exit_to_grid_cell(
                Hyprutils::Memory::dynamicPointerCast<HTLayoutGrid, HTLayoutBase>(layout),
                monitor,
                g_pInputManager->getMouseCoordsInternal(),
                ws_id,
                workspace
            );
    }

    if (workspace == nullptr)
        return;

    const PHLMONITOR ws_monitor = g_pCompositor->getMonitorFromID(workspace->monitorID());
    if (ws_monitor != nullptr)
        ws_monitor->changeWorkspace(workspace);
    else
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
    pre_overview_workspace = active_workspace->m_id;
    marked_workspace = active_workspace->m_id;

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
    marked_workspace = WORKSPACE_INVALID;

    layout->on_hide([this](auto self) {
        active = false;
        closing = false;
    });

    Cursor::overrideController->unsetOverride(Cursor::CURSOR_OVERRIDE_UNKNOWN);

    g_pHyprRenderer->damageMonitor(monitor);
    g_pCompositor->scheduleFrameForMonitor(monitor);
}

void HTView::mark_workspace(WORKSPACEID ws_id) {
    if (ws_id == WORKSPACE_INVALID)
        return;

    if (layout->get_workspace_from_layout(ws_id) == nullptr)
        return;

    marked_workspace = ws_id;
}

void HTView::reset_for_monitor_change() {
    active = false;
    closing = false;
    navigating = false;

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    layout->init_position();
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

    mark_workspace(other_workspace->m_id);

    if (move_window && should_move) {
        g_pCompositor->moveWindowToWorkspaceSafe(hovered_window, other_workspace);
    }

    const PHLMONITOR ws_monitor = g_pCompositor->getMonitorFromID(other_workspace->monitorID());
    if (ws_monitor != nullptr)
        ws_monitor->changeWorkspace(other_workspace);
    else
        monitor->changeWorkspace(other_workspace);
    Hyprlang::INT warp = 0;
    if (move_window) {
        Desktop::focusState()->fullWindowFocus(hovered_window, Desktop::FOCUS_REASON_CLICK);
        warp = *CConfigValue<Hyprlang::INT>("plugin:hyprtasking:warp_on_move_window");
    } else {
        warp = *CConfigValue<Hyprlang::INT>("cursor:warp_on_change_workspace");
    }
    warp_window(warp, hovered_window);

    if (active) {
        layout->build_overview_layout(HT_VIEW_CLOSED);
        g_pHyprRenderer->damageMonitor(monitor);
        g_pCompositor->scheduleFrameForMonitor(monitor);
        Log::logger->log(
            LOG,
            "[Hyprtasking] move_id changed active view from workspace {} to {} without navigation animation",
            active_workspace->m_id,
            other_workspace->m_id
        );
        return;
    }

    navigating = true;
    layout->on_move(active_workspace->m_id, other_workspace->m_id, [this](auto self) {
        navigating = false;
    });
}

void HTView::move(std::string arg, bool move_window) {
    if (arg != "up" && arg != "down" && arg != "left" && arg != "right")
        return;

    const SP<HTLayoutGrid> grid_layout = Hyprutils::Memory::dynamicPointerCast<HTLayoutGrid, HTLayoutBase>(layout);
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

    const WORKSPACEID source_ws_id =
        move_window ? hovered_window->workspaceID() : active_workspace->m_id;
    layout->build_overview_layout(HT_VIEW_CLOSED);
    const auto ws_layout_it = layout->overview_layout.find(source_ws_id);
    HTLayoutBase::HTWorkspace ws_layout;
    if (ws_layout_it == layout->overview_layout.end()) {
        if (grid_layout == nullptr || !active)
            return;

        const int ROWS = static_cast<Hyprlang::INT>(HTConfig::value("grid:rows"));
        const int COLS = static_cast<Hyprlang::INT>(HTConfig::value("grid:cols"));
        const int cell = grid_layout->last_layer_cell;
        if (cell < 0 || cell >= ROWS * COLS)
            return;

        ws_layout.x = cell % COLS;
        ws_layout.y = cell / COLS;
    } else {
        ws_layout = ws_layout_it->second;
        if (grid_layout != nullptr) {
            const int COLS = static_cast<Hyprlang::INT>(HTConfig::value("grid:cols"));
            grid_layout->last_layer_cell = ws_layout.y * COLS + ws_layout.x;
        }
    }
    const WORKSPACEID id = layout->get_ws_id_in_direction(ws_layout.x, ws_layout.y, arg);

    if (id == WORKSPACE_INVALID && grid_layout != nullptr) {
        int x = ws_layout.x, y = ws_layout.y;
        const int LOOP = static_cast<Hyprlang::INT>(HTConfig::value("grid:loop"));
        const int ROWS = static_cast<Hyprlang::INT>(HTConfig::value("grid:rows"));
        const int COLS = static_cast<Hyprlang::INT>(HTConfig::value("grid:cols"));
        if (arg == "up") {
            y--;
        } else if (arg == "down") {
            y++;
        } else if (arg == "right") {
            x++;
        } else if (arg == "left") {
            x--;
        }
        if (LOOP) {
            x = (x + COLS) % COLS;
            y = (y + ROWS) % ROWS;
        }
        if (x >= 0 && x < COLS && y >= 0 && y < ROWS) {
            const int ws_per_layer = std::max(1, ROWS * COLS);
            const int target_slot = grid_layout->layer * ws_per_layer + y * COLS + x;
            PHLWORKSPACE new_ws = create_workspace_for_monitor(monitor);
            if (new_ws != nullptr) {
                grid_layout->pin_workspace_to_slot(new_ws->m_id, target_slot);
                move_id(new_ws->m_id, move_window);
                return;
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

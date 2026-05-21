#include <linux/input-event-codes.h>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>

#include "config.hpp"
#include "manager.hpp"
#include "overview.hpp"
#include "layout/grid.hpp"
#include "workspace.hpp"

namespace {
PHLWORKSPACE create_workspace_at_grid_cell(SP<HTLayoutGrid> grid, PHLMONITOR monitor, Vector2D pos) {
    if (grid == nullptr || monitor == nullptr || !monitor->logicalBox().containsPoint(pos))
        return nullptr;

    const auto [cell_x, cell_y, cell_layer] = grid->get_grid_cell_from_global(pos);
    if (cell_x < 0 || cell_y < 0 || cell_layer < 0)
        return nullptr;

    grid->layer = cell_layer;
    const int ROWS = static_cast<Hyprlang::INT>(HTConfig::value("grid:rows"));
    const int COLS = static_cast<Hyprlang::INT>(HTConfig::value("grid:cols"));
    const int ws_per_layer = std::max(1, ROWS * COLS);
    const int target_slot = cell_layer * ws_per_layer + cell_y * COLS + cell_x;

    PHLWORKSPACE workspace = create_workspace_for_monitor(monitor);
    if (workspace != nullptr)
        grid->pin_workspace_to_slot(workspace->m_id, target_slot);

    return workspace;
}

bool is_valid_grid_cell(SP<HTLayoutGrid> grid, PHLMONITOR monitor, Vector2D pos) {
    if (grid == nullptr || monitor == nullptr || !monitor->logicalBox().containsPoint(pos))
        return false;

    const auto [cell_x, cell_y, cell_layer] = grid->get_grid_cell_from_global(pos);
    return cell_x >= 0 && cell_y >= 0 && cell_layer >= 0;
}
}

bool HTManager::start_window_drag() {
    const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (cursor_monitor == nullptr || cursor_view == nullptr || !cursor_view->active
        || cursor_view->closing)
        return false;

    if (!cursor_view->layout->should_manage_mouse()) {
        hide_all_views();
        return true;
    }

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    WORKSPACEID workspace_id = cursor_view->layout->get_ws_id_from_global(mouse_coords);
    PHLWORKSPACE cursor_workspace = cursor_view->layout->get_workspace_from_layout(workspace_id);
    bool hit_empty_grid_cell = false;

    if (cursor_workspace == nullptr && workspace_id == WORKSPACE_INVALID) {
        const SP<HTLayoutGrid> grid_layout = Hyprutils::Memory::dynamicPointerCast<HTLayoutGrid, HTLayoutBase>(cursor_view->layout);
        hit_empty_grid_cell = is_valid_grid_cell(grid_layout, cursor_monitor, mouse_coords);
        cursor_workspace = create_workspace_at_grid_cell(grid_layout, cursor_monitor, mouse_coords);
        if (cursor_workspace != nullptr)
            workspace_id = cursor_workspace->m_id;
    }

    if (cursor_workspace == nullptr)
        return hit_empty_grid_cell;

    const PHLMONITOR ws_monitor = g_pCompositor->getMonitorFromID(cursor_workspace->monitorID());
    if (ws_monitor != nullptr)
        ws_monitor->changeWorkspace(cursor_workspace, true);
    else
        cursor_monitor->changeWorkspace(cursor_workspace, true);

    if (cursor_view != nullptr && cursor_view->active)
        cursor_view->hide(true);

    const Vector2D workspace_coords =
        cursor_view->layout->global_to_local_ws_unscaled(mouse_coords, workspace_id)
        + cursor_monitor->m_position;

    g_pPointerManager->warpTo(workspace_coords);
    g_pKeybindManager->changeMouseBindMode(MBIND_MOVE);
    g_pPointerManager->warpTo(mouse_coords);

    const SP<Layout::ITarget> target = g_layoutManager->dragController()->target();
    if (target == nullptr)
        return false;

    const PHLWINDOW dragged_window = target->window();
    if (dragged_window != nullptr) {
        if (g_layoutManager->dragController()->draggingTiled()) {
            const Vector2D pre_pos = cursor_view->layout->local_ws_unscaled_to_global(
                dragged_window->m_realPosition->value() - dragged_window->m_monitor->m_position,
                workspace_id
            );
            const Vector2D post_pos = cursor_view->layout->local_ws_unscaled_to_global(
                dragged_window->m_realPosition->goal() - dragged_window->m_monitor->m_position,
                workspace_id
            );
            const Vector2D mapped_pre_pos =
                (pre_pos - mouse_coords) / cursor_view->layout->drag_window_scale() + mouse_coords;
            const Vector2D mapped_post_pos =
                (post_pos - mouse_coords) / cursor_view->layout->drag_window_scale() + mouse_coords;

            dragged_window->m_realPosition->setValueAndWarp(mapped_pre_pos);
            *dragged_window->m_realPosition = mapped_post_pos;
        } else {
            g_pInputManager->simulateMouseMovement();
        }
    }

    return true;
}

bool HTManager::end_window_drag() {
    const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (cursor_monitor == nullptr || cursor_view == nullptr) {
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return false;
    }

    if (!cursor_view->active || cursor_view->closing) {
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return false;
    }

    if (!cursor_view->layout->should_manage_mouse()) {
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return false;
    }

    const SP<Layout::ITarget> target = g_layoutManager->dragController()->target();
    if (target == nullptr)
        return false;

    const PHLWINDOW dragged_window = target->window();
    if (dragged_window == nullptr || g_layoutManager->dragController()->mode() != MBIND_MOVE) {
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return false;
    }

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    Vector2D use_mouse_coords = mouse_coords;
    const WORKSPACEID workspace_id = cursor_view->layout->get_ws_id_from_global(mouse_coords);
    PHLWORKSPACE cursor_workspace = cursor_view->layout->get_workspace_from_layout(workspace_id);

    if (workspace_id == WORKSPACE_INVALID) {
        if (cursor_monitor->logicalBox().containsPoint(mouse_coords)) {
            const SP<HTLayoutGrid> grid_layout = Hyprutils::Memory::dynamicPointerCast<HTLayoutGrid, HTLayoutBase>(cursor_view->layout);
            if (grid_layout == nullptr) {
                cursor_workspace = dragged_window->m_workspace;
                if (cursor_workspace == nullptr
                    || cursor_view->layout->get_workspace_from_layout(cursor_workspace->m_id) == nullptr) {
                    cursor_workspace = nullptr;
                }

                if (cursor_workspace == nullptr) {
                    g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
                    return false;
                }

                use_mouse_coords = cursor_view->layout->get_global_ws_box(cursor_workspace->m_id)
                                       .closestPoint(use_mouse_coords);

                Log::logger->log(
                    LOG,
                    "[Hyprtasking] Dragging to invalid position, snapping to last ws {}",
                    cursor_workspace->m_id
                );
            } else {
                const bool hit_empty_grid_cell = is_valid_grid_cell(grid_layout, cursor_monitor, mouse_coords);
                cursor_workspace = create_workspace_at_grid_cell(grid_layout, cursor_monitor, mouse_coords);
                if (cursor_workspace == nullptr && hit_empty_grid_cell) {
                    g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
                    return false;
                }
            }
        } else {
            cursor_workspace = dragged_window->m_workspace;
            if (cursor_workspace == nullptr
                || cursor_view->layout->get_workspace_from_layout(cursor_workspace->m_id) == nullptr) {
                cursor_workspace = nullptr;
            }

            if (cursor_workspace == nullptr) {
                g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
                return false;
            }

            use_mouse_coords = cursor_view->layout->get_global_ws_box(cursor_workspace->m_id)
                                   .closestPoint(use_mouse_coords);

            Log::logger->log(
                LOG,
                "[Hyprtasking] Dragging to invalid position, snapping to last ws {}",
                cursor_workspace->m_id
            );
        }
    }

    if (cursor_workspace == nullptr) {
        Log::logger->log(LOG, "[Hyprtasking] tried to drop on null workspace??");
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return false;
    }

    Log::logger->log(
        LOG,
        "[Hyprtasking] trying to drop window on monitor {} ws {} ({})",
        cursor_monitor->m_name,
        cursor_workspace->m_id,
        cursor_workspace->m_name
    );

    const PHLMONITOR drop_monitor = g_pCompositor->getMonitorFromID(cursor_workspace->monitorID());
    if (drop_monitor != nullptr)
        drop_monitor->changeWorkspace(cursor_workspace, true);
    else
        cursor_monitor->changeWorkspace(cursor_workspace, true);

    g_pCompositor->moveWindowToWorkspaceSafe(dragged_window, cursor_workspace);

    const Vector2D workspace_coords =
        cursor_view->layout->global_to_local_ws_unscaled(use_mouse_coords, cursor_workspace->m_id)
        + cursor_monitor->m_position;

    g_pPointerManager->warpTo(workspace_coords);
    g_pInputManager->simulateMouseMovement();
    g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
    g_pPointerManager->warpTo(mouse_coords);

    dragged_window->m_movingToWorkspaceAlpha->setValueAndWarp(1.0);
    dragged_window->m_movingFromWorkspaceAlpha->setValueAndWarp(1.0);

    // Do not return true and cancel the event! Mouse release requires some stuff to be done for
    // floating windows to be unfocused properly
    return false;
}

bool HTManager::mark_workspace() {
    const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (cursor_monitor == nullptr || cursor_view == nullptr || !cursor_view->active
        || cursor_view->closing)
        return false;

    if (!cursor_view->layout->should_manage_mouse())
        return false;

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    WORKSPACEID workspace_id = cursor_view->layout->get_ws_id_from_global(mouse_coords);
    PHLWORKSPACE cursor_workspace = cursor_view->layout->get_workspace_from_layout(workspace_id);
    bool hit_empty_grid_cell = false;

    if (cursor_workspace == nullptr && workspace_id == WORKSPACE_INVALID) {
        const SP<HTLayoutGrid> grid_layout = Hyprutils::Memory::dynamicPointerCast<HTLayoutGrid, HTLayoutBase>(cursor_view->layout);
        hit_empty_grid_cell = is_valid_grid_cell(grid_layout, cursor_monitor, mouse_coords);
        cursor_workspace = create_workspace_at_grid_cell(grid_layout, cursor_monitor, mouse_coords);
        if (cursor_workspace != nullptr)
            workspace_id = cursor_workspace->m_id;
    }

    if (cursor_workspace == nullptr)
        return hit_empty_grid_cell;

    cursor_view->mark_workspace(cursor_workspace->m_id);
    return true;
}

bool HTManager::exit_to_workspace() {
    const PHTVIEW cursor_view = get_view_from_cursor();
    if (cursor_view == nullptr)
        return false;

    if (!cursor_view->active || !cursor_view->layout->should_manage_mouse())
        return false;

    for (PHTVIEW view : views) {
        if (view == nullptr)
            continue;
        view->hide(true);
    }
    return true;
}

bool HTManager::on_mouse_move() {
    return false;
}

bool HTManager::on_mouse_axis(double delta) {
    const PHTVIEW cursor_view = get_view_from_cursor();
    if (cursor_view == nullptr)
        return false;

    return cursor_view->layout->on_mouse_axis(delta);
}

void HTManager::swipe_start() {
    swipe_state = HT_SWIPE_NONE;
    swipe_amt = 0.0;
}

bool HTManager::swipe_update(IPointer::SSwipeUpdateEvent e) {
    const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (cursor_view == nullptr)
        return false;

    const int ENABLED = static_cast<Hyprlang::INT>(HTConfig::value("gestures:enabled"));
    if (!ENABLED)
        return false;

    const unsigned int MOVE_FINGERS = static_cast<Hyprlang::INT>(HTConfig::value("gestures:move_fingers"));
    const float OPEN_DISTANCE = HTConfig::value_float("gestures:open_distance");
    const unsigned int OPEN_FINGERS = static_cast<Hyprlang::INT>(HTConfig::value("gestures:open_fingers"));
    const int OPEN_POSITIVE = static_cast<Hyprlang::INT>(HTConfig::value("gestures:open_positive"));

    bool res = false;
    char swipe_direction = 0;
    if (std::abs(e.delta.x) > std::abs(e.delta.y)) {
        swipe_direction = 'h';
    } else if (std::abs(e.delta.y) > std::abs(e.delta.x)) {
        swipe_direction = 'v';
    }

    if (e.fingers == OPEN_FINGERS) {
        if (cursor_view->active || swipe_state == HT_SWIPE_OPEN)
            res = true;

        const float deltaY = OPEN_POSITIVE ? e.delta.y : -e.delta.y;

        if (swipe_state != HT_SWIPE_OPEN) {
            if (swipe_direction != 'v' || cursor_view->closing) {
                return res;
            } else if (!cursor_view->active && deltaY <= 0) {
                cursor_view->show();
                swipe_state = HT_SWIPE_OPEN;
                swipe_amt = OPEN_DISTANCE;
            } else if (cursor_view->active && deltaY > 0) {
                cursor_view->hide(false);
                swipe_state = HT_SWIPE_OPEN;
                swipe_amt = 0.0;
            }
        }

        if (swipe_state == HT_SWIPE_OPEN) {
            swipe_amt += deltaY;
            const float swipe_perc = 1.0 - std::clamp(swipe_amt / OPEN_DISTANCE, 0.01f, 1.0f);
            cursor_view->layout->close_open_lerp(swipe_perc);
        }
    } else if (e.fingers == MOVE_FINGERS) {
        if (swipe_state == HT_SWIPE_MOVE)
            res = true;

        if (swipe_state != HT_SWIPE_MOVE) {
            if (cursor_view->active) {
                return res;
            } else {
                swipe_state = HT_SWIPE_MOVE;
                cursor_view->layout->init_position();
                cursor_view->navigating = true;

                cursor_view->layout->init_position();
                // need to schedule frames for monitor, otherwise the screen doesn't re-render
                g_pHyprRenderer->damageMonitor(cursor_monitor);
                g_pCompositor->scheduleFrameForMonitor(cursor_monitor);
            }
        }

        if (swipe_state == HT_SWIPE_MOVE) {
            cursor_view->layout->on_move_swipe(e.delta);
        }
    }
    return res;
}

bool HTManager::swipe_end() {
    const PHTVIEW cursor_view = get_view_from_cursor();
    if (cursor_view == nullptr || swipe_state == HT_SWIPE_NONE)
        return false;

    switch (swipe_state) {
        case HT_SWIPE_OPEN: {
            const float OPEN_DISTANCE = HTConfig::value_float("gestures:open_distance");
            const float swipe_perc = 1.0 - std::clamp(swipe_amt / OPEN_DISTANCE, 0.01f, 1.0f);
            if (swipe_perc >= 0.5) {
                cursor_view->show(false);
            } else {
                cursor_view->hide(false);
            }
            break;
        }
        case HT_SWIPE_MOVE: {
            const WORKSPACEID ws_id = cursor_view->layout->on_move_swipe_end();
            cursor_view->move_id(ws_id, false);
            break;
        }
        case HT_SWIPE_NONE:
            break;
    }

    swipe_state = HT_SWIPE_NONE;
    swipe_amt = 0.0;
    return true;
}

#include <linux/input-event-codes.h>

#include <array>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/desktop/state/ViewState.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/pointer/PointerManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>

#include "config.hpp"
#include "manager.hpp"
#include "overview.hpp"

// Hyprland 0.57 removed CKeybindManager::changeMouseBindMode; this mirrors its
// v0.56.2 implementation.
void HTManager::change_mouse_bind_mode(eMouseBindMode mode) {
    if (mode == MBIND_INVALID) {
        if (g_layoutManager->dragController()->target())
            g_layoutManager->endDragTarget();
        drag_window.reset();
        return;
    }

    if (g_layoutManager->dragController()->target())
        return;

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const PHLWINDOW window = Desktop::viewState()->hitTest().windowAt(
        mouse_coords,
        Desktop::View::RESERVED_EXTENTS | Desktop::View::INPUT_EXTENTS
            | Desktop::View::ALLOW_FLOATING
    );
    if (window == nullptr)
        return;

    if (!Fullscreen::controller()->isFullscreen(window) && mode == MBIND_MOVE
        && window->presentation().checkInputOnDecorations(INPUT_TYPE_DRAG_START, mouse_coords))
        return;

    // Only tiled windows are shrunk by Hyprland's drag float; fullscreen restores an unrelated
    // geometry, so leave those on the plain layout scale.
    drag_window.reset();
    if (mode == MBIND_MOVE && !window->isFloating()
        && !Fullscreen::controller()->isFullscreen(window)) {
        drag_window = window;
        drag_window_size = window->getWindowMainSurfaceBox().size();
    }
    g_layoutManager->beginDragTarget(window->layoutTarget(), mode);
}

float HTManager::drag_preview_scale(PHLWINDOW window, float layout_scale) {
    const double width = window->getWindowMainSurfaceBox().w;
    if (window != drag_window.lock() || drag_window_size.x <= 0 || width <= 0)
        return layout_scale;
    return layout_scale * drag_window_size.x / width;
}

bool HTManager::start_window_drag() {
    const PHLMONITOR cursor_monitor =
        State::monitorState()->query().vec(g_pInputManager->getMouseCoordsInternal()).run();
    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (cursor_monitor == nullptr || cursor_view == nullptr || !cursor_view->active
        || cursor_view->closing)
        return false;

    if (!cursor_view->layout->should_manage_mouse()) {
        // hide all views if should not manage mouse but active
        hide_all_views();
        return true;
    }

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const WORKSPACEID workspace_id = cursor_view->layout->get_ws_id_from_global(mouse_coords);
    PHLWORKSPACE cursor_workspace = find_workspace(workspace_id);

    // If left click on non-workspace workspace, do nothing
    if (cursor_workspace == nullptr)
        return false;

    // PHLWORKSPACEREF o_workspace = cursor_monitor->m_activeWorkspace;
    cursor_monitor->changeWorkspace(cursor_workspace, true);

    const Vector2D workspace_coords =
        cursor_view->layout->global_to_local_ws_unscaled(mouse_coords, workspace_id)
        + cursor_monitor->m_position;

    Pointer::mgr()->warpTo(workspace_coords);
    change_mouse_bind_mode(MBIND_MOVE);
    Pointer::mgr()->warpTo(mouse_coords);

    const SP<Layout::ITarget> target = g_layoutManager->dragController()->target();
    if (target == nullptr)
        return false;

    const PHLWINDOW dragged_window = target->window();
    if (dragged_window != nullptr) {
        if (g_layoutManager->dragController()->draggingTiled()) {
            const Vector2D pre_pos = cursor_view->layout->local_ws_unscaled_to_global(
                dragged_window->positionAnimation()->value() - dragged_window->m_monitor->m_position,
                workspace_id
            );
            const Vector2D post_pos = cursor_view->layout->local_ws_unscaled_to_global(
                dragged_window->positionAnimation()->goal() - dragged_window->m_monitor->m_position,
                workspace_id
            );
            const Vector2D mapped_pre_pos =
                (pre_pos - mouse_coords) / cursor_view->layout->drag_window_scale() + mouse_coords;
            const Vector2D mapped_post_pos =
                (post_pos - mouse_coords) / cursor_view->layout->drag_window_scale() + mouse_coords;

            dragged_window->positionAnimation()->setValueAndWarp(mapped_pre_pos);
            *dragged_window->positionAnimation() = mapped_post_pos;
        } else {
            g_pInputManager->simulateMouseMovement();
        }
    }

    // if (o_workspace != nullptr)
    //     cursor_monitor->changeWorkspace(o_workspace.lock(), true);

    return true;
}

bool HTManager::end_window_drag() {
    const PHLMONITOR cursor_monitor =
        State::monitorState()->query().vec(g_pInputManager->getMouseCoordsInternal()).run();
    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (cursor_monitor == nullptr || cursor_view == nullptr) {
        change_mouse_bind_mode(MBIND_INVALID);
        return false;
    }

    // Required if dragging and dropping from active to inactive
    if (!cursor_view->active || cursor_view->closing) {
        change_mouse_bind_mode(MBIND_INVALID);
        return false;
    }

    // For linear layout: if dropping on big workspace, just pass on
    if (!cursor_view->layout->should_manage_mouse()) {
        change_mouse_bind_mode(MBIND_INVALID);
        return false;
    }

    const SP<Layout::ITarget> target = g_layoutManager->dragController()->target();
    if (target == nullptr)
        return false;

    // If not dragging window or drag is not move, then we just let go (supposed to prevent it
    // from messing up resize on border, but it should be good because above?)
    const PHLWINDOW dragged_window = target->window();
    if (dragged_window == nullptr || g_layoutManager->dragController()->mode() != MBIND_MOVE) {
        change_mouse_bind_mode(MBIND_INVALID);
        return false;
    }

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    Vector2D use_mouse_coords = mouse_coords;
    const WORKSPACEID workspace_id = cursor_view->layout->get_ws_id_from_global(mouse_coords);
    PHLWORKSPACE cursor_workspace = find_workspace(workspace_id);

    // Release on empty dummy workspace, so create and switch to it
    if (cursor_workspace == nullptr && workspace_id != WORKSPACE_INVALID) {
        cursor_workspace = create_workspace(workspace_id, cursor_monitor);
    } else if (workspace_id == WORKSPACE_INVALID) {
        cursor_workspace = dragged_window->m_workspace;
        // Ensure that the mouse coords are snapped to inside the workspace box itself
        use_mouse_coords = cursor_view->layout->get_global_ws_box(get_ws_id(cursor_workspace))
                               .closestPoint(use_mouse_coords);

        Log::logger->log(
            LOG,
            std::format(
                "[Hyprtasking] Dragging to invalid position, snapping to last ws {}",
                get_ws_id(cursor_workspace)
            )
        );
    }

    if (cursor_workspace == nullptr) {
        Log::logger->log(LOG, "[Hyprtasking] tried to drop on null workspace??");
        change_mouse_bind_mode(MBIND_INVALID);
        return false;
    }

    Log::logger->log(
        LOG,
        std::format("[Hyprtasking] trying to drop window on ws {}", get_ws_id(cursor_workspace))
    );

    // PHLWORKSPACEREF o_workspace = cursor_monitor->m_activeWorkspace;
    cursor_monitor->changeWorkspace(cursor_workspace, true);

    Desktop::globalWindowController()->moveWindowToWorkspace(dragged_window, cursor_workspace);
    cursor_workspace->ready();

    // Inverts the scale-around-mouse remap that start_window_drag applies for
    // tiled drags; without it, the post-close m_realPosition reads as
    // workspace-local (0, 0) and the window snaps to the cell's corner.
    const bool dragging_tiled = g_layoutManager->dragController()->draggingTiled();
    const Vector2D drag_pos = dragged_window->positionAnimation()->value();
    if (dragging_tiled) {
        const Vector2D drop_pos =
            cursor_view->layout->global_to_local_ws_unscaled(
                (drag_pos - use_mouse_coords) * cursor_view->layout->drag_window_scale()
                    + use_mouse_coords,
                get_ws_id(cursor_workspace)
            )
            + cursor_monitor->m_position;
        dragged_window->positionAnimation()->setValueAndWarp(drop_pos);
    }

    const Vector2D workspace_coords = cursor_view->layout->global_to_local_ws_unscaled(
                                          use_mouse_coords,
                                          get_ws_id(cursor_workspace)
                                      )
        + cursor_monitor->m_position;

    // Where the size-preserving preview was drawn, in workspace coordinates, so the re-tile
    // animation starts from what the user saw instead of Hyprland's shrunken float geometry.
    std::optional<CBox> preview_box;
    if (dragging_tiled && dragged_window == drag_window.lock()) {
        // Same box the layout renders: scaled about the real cursor, not the snapped drop point.
        const float scale =
            drag_preview_scale(dragged_window, cursor_view->layout->drag_window_scale());
        const Vector2D shown_pos = (drag_pos - mouse_coords) * scale + mouse_coords;
        const Vector2D shown_local = cursor_view->layout->global_to_local_ws_unscaled(
            shown_pos,
            get_ws_id(cursor_workspace)
        );
        preview_box = CBox {
            shown_local + cursor_monitor->m_position,
            dragged_window->sizeAnimation()->value() * drag_preview_scale(dragged_window, 1.F)
        };
    }

    Pointer::mgr()->warpTo(workspace_coords);
    g_pInputManager->simulateMouseMovement();
    // After the simulated motion, which rewrites the drag geometry.
    if (preview_box) {
        dragged_window->positionAnimation()->setValueAndWarp(preview_box->pos());
        dragged_window->sizeAnimation()->setValueAndWarp(preview_box->size());
    }
    change_mouse_bind_mode(MBIND_INVALID);
    Pointer::mgr()->warpTo(mouse_coords);

    // otherwise the window leaves blur (?) artifacts on all
    // workspaces
    dragged_window->presentation()
        .alpha(Desktop::View::WINDOW_ALPHA_MOVE_TO_WORKSPACE)
        ->setValueAndWarp(1.0);
    dragged_window->presentation()
        .alpha(Desktop::View::WINDOW_ALPHA_MOVE_FROM_WORKSPACE)
        ->setValueAndWarp(1.0);

    // if (o_workspace != nullptr)
    //     cursor_monitor->changeWorkspace(o_workspace.lock(), true);

    // Do not return true and cancel the event! Mouse release requires some stuff to be done for
    // floating windows to be unfocused properly
    return false;
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

bool HTManager::on_key(IKeyboard::SKeyEvent event) {
    if (event.state == WL_KEYBOARD_KEY_STATE_RELEASED && jump_pressed_keys.erase(event.keycode) > 0)
        return true;

    if (!HTConfig::value<Config::INTEGER>("jump:enabled"))
        return false;

    const PHTVIEW cursor_view = get_view_from_cursor();
    if (cursor_view == nullptr || !cursor_view->active)
        return false;

    std::optional<size_t> index;
    bool resolved_keyboard = false;
    for (const auto& keyboard : g_pInputManager->m_keyboards) {
        if (keyboard == nullptr || keyboard->m_xkbState == nullptr
            || !keyboard->getPressed(event.keycode))
            continue;

        resolved_keyboard = true;
        const xkb_keysym_t keysym =
            xkb_keysym_to_lower(xkb_state_key_get_one_sym(keyboard->m_xkbState, event.keycode + 8));
        if (keysym >= XKB_KEY_1 && keysym <= XKB_KEY_9)
            index = keysym - XKB_KEY_1;
        else if (keysym == XKB_KEY_0)
            index = 9;
        else if (keysym >= XKB_KEY_a && keysym <= XKB_KEY_z)
            index = 10 + keysym - XKB_KEY_a;
        break;
    }

    // Physical-key fallback for unusual keyboard implementations without XKB state.
    if (!resolved_keyboard) {
        static constexpr std::array<unsigned int, 36> KEYCODES = {
            KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0, KEY_A, KEY_B,
            KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M, KEY_N,
            KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
        };
        for (size_t i = 0; i < KEYCODES.size(); i++) {
            if (KEYCODES[i] == event.keycode) {
                index = i;
                break;
            }
        }
    }

    if (!index.has_value())
        return false;

    const auto target = cursor_view->layout->jump_target(*index);
    if (!target.has_value())
        return false;

    // Keep consuming the matching release even if the close animation has finished by then.
    jump_pressed_keys.insert(event.keycode);

    // Act only on the initial press.
    if (event.state != WL_KEYBOARD_KEY_STATE_PRESSED || cursor_view->closing)
        return true;

    for (const PHTVIEW& view : views) {
        if (view == nullptr || !view->active)
            continue;
        if (view == cursor_view)
            view->hide(false, *target);
        else
            view->hide(false);
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
    const PHLMONITOR cursor_monitor =
        State::monitorState()->query().vec(g_pInputManager->getMouseCoordsInternal()).run();
    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (cursor_view == nullptr)
        return false;

    const int ENABLED = HTConfig::value<Config::INTEGER>("gestures:enabled");
    if (!ENABLED)
        return false;

    const unsigned int MOVE_FINGERS = HTConfig::value<Config::INTEGER>("gestures:move_fingers");
    const float OPEN_DISTANCE = HTConfig::value<Config::FLOAT>("gestures:open_distance");
    const unsigned int OPEN_FINGERS = HTConfig::value<Config::INTEGER>("gestures:open_fingers");
    const int OPEN_POSITIVE = HTConfig::value<Config::INTEGER>("gestures:open_positive");

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
                cursor_view->navigating = true;

                cursor_view->layout->init_position();
                // need to schedule frames for monitor, otherwise the screen doesn't re-render
                g_pHyprRenderer->damageMonitor(cursor_monitor);
                cursor_monitor->scheduleFrame();
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
            const float OPEN_DISTANCE = HTConfig::value<Config::FLOAT>("gestures:open_distance");
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

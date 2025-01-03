#include "overview.hpp"

#include <ctime>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Box.hpp>

#include "config.hpp"
#include "globals.hpp"

HTView::HTView(MONITORID in_monitor_id) {
    monitor_id = in_monitor_id;
    active = false;
    closing = false;
    navigating = false;

    offset.create(
        {0, 0},
        g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
        AVARDAMAGE_NONE
    );
    scale.create(1.f, g_pConfigManager->getAnimationPropertyConfig("windowsMove"), AVARDAMAGE_NONE);

    // Adjust offset to match workspace
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const Vector2D GAPS = gaps();

    build_overview_layout(false);
    const HTWorkspace ws_layout = overview_layout[get_monitor()->activeWorkspaceID()];
    offset.setValueAndWarp(
        Vector2D {ws_layout.col, ws_layout.row} * (-GAPS - monitor->vecPixelSize) - GAPS
    );
}

bool HTView::try_switch_to_hover() {
    const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
    // If the cursor monitor is not the view monitor, then we cannot
    // exit to hover
    if (cursor_monitor != get_monitor())
        return false;

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const WORKSPACEID workspace_id = get_ws_id_from_global(mouse_coords);
    PHLWORKSPACE cursor_workspace = g_pCompositor->getWorkspaceByID(workspace_id);

    // If hovering on new, then create new
    if (cursor_workspace == nullptr && workspace_id != WORKSPACE_INVALID) {
        cursor_workspace = g_pCompositor->createNewWorkspace(workspace_id, cursor_monitor->ID);
    }

    // Failed to create new or hovering over dead space
    if (cursor_workspace == nullptr)
        return false;

    cursor_monitor->changeWorkspace(cursor_workspace);
    cursor_workspace->startAnim(true, false, true);
    return true;
}

bool HTView::try_switch_to_original() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr) //???
        return false;

    PHLWORKSPACE original_ws = ori_workspace.lock();

    // If original workspace died (rip), then we go next
    if (original_ws == nullptr)
        return false;

    monitor->changeWorkspace(original_ws);
    original_ws->startAnim(true, false, true);
    return true;
}

void HTView::do_exit_behavior(bool override_hover) {
    if (override_hover && try_switch_to_hover())
        return;

    CVarList exit_behavior {HTConfig::exit_behavior(), 0, 's', true};

    for (const auto& behavior : exit_behavior) {
        if (behavior == "hovered") {
            if (try_switch_to_hover())
                return;
        } else if (behavior == "original") {
            if (try_switch_to_original())
                return;
        } else {
            // if inavlid string or 'interacted', then we default to last
            // interacted this requires us to do nothing, as we will zoom into
            // the last focused ws on the monitor
            break;
        }
    }
}

void HTView::show() {
    if (active)
        return;
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    active = true;
    ori_workspace = monitor->activeWorkspace;

    build_overview_layout(false);
    const HTWorkspace ws_layout = overview_layout[monitor->activeWorkspaceID()];

    scale = ws_layout.box.w / monitor->vecPixelSize.x; // 1 / ROWS
    offset = {0, 0};

    g_pInputManager->setCursorImageUntilUnset("left_ptr");

    g_pHyprRenderer->damageMonitor(monitor);
    g_pCompositor->scheduleFrameForMonitor(monitor);
}

void HTView::hide() {
    if (closing || !active)
        return;
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    closing = true;
    ori_workspace.reset();

    build_overview_layout(false);
    const HTWorkspace ws_layout = overview_layout[monitor->activeWorkspaceID()];

    const Vector2D GAPS = gaps();

    scale = 1.;
    offset = Vector2D {ws_layout.col, ws_layout.row} * (-GAPS - monitor->vecPixelSize) - GAPS;

    scale.setCallbackOnEnd([this](void*) {
        active = false;
        closing = false;
    });

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const PHLWINDOW hovered_window = g_pCompositor->vectorToWindowUnified(
        mouse_coords,
        RESERVED_EXTENTS | INPUT_EXTENTS | ALLOW_FLOATING
    );
    if (hovered_window)
        g_pCompositor->focusWindow(hovered_window);

    g_pInputManager->unsetCursorImage();

    g_pHyprRenderer->damageMonitor(monitor);
    g_pCompositor->scheduleFrameForMonitor(monitor);
}

void HTView::move(std::string arg) {
    if (closing)
        return;
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;
    const PHLWORKSPACE active_workspace = monitor->activeWorkspace;
    if (active_workspace == nullptr)
        return;

    build_overview_layout();
    const HTWorkspace ws_layout = overview_layout[active_workspace->m_iID];

    int target_row = ws_layout.row;
    int target_col = ws_layout.col;
    if (arg == "up") {
        target_row--;
    } else if (arg == "down") {
        target_row++;
    } else if (arg == "right") {
        target_col++;
    } else if (arg == "left") {
        target_col--;
    }

    const Vector2D GAPS = gaps();
    for (const auto [id, other_layout] : overview_layout) {
        if (other_layout.row == target_row && other_layout.col == target_col) {
            PHLWORKSPACE other_workspace = g_pCompositor->getWorkspaceByID(id);
            if (other_workspace == nullptr && id != WORKSPACE_INVALID)
                other_workspace = g_pCompositor->createNewWorkspace(id, monitor->ID);

            if (other_workspace == nullptr)
                break;

            monitor->changeWorkspace(other_workspace);
            other_workspace->startAnim(true, false, true);

            const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
            const PHLWINDOW hovered_window = g_pCompositor->vectorToWindowUnified(
                mouse_coords,
                RESERVED_EXTENTS | INPUT_EXTENTS | ALLOW_FLOATING
            );
            if (hovered_window)
                g_pCompositor->focusWindow(hovered_window);

            if (!active) {
                navigating = true;
                offset =
                    (Vector2D {target_col, target_row} * (-GAPS - ws_layout.box.size()) - GAPS);
                offset.setCallbackOnEnd([this](void*) { navigating = false; });
            }
            break;
        }
    }
}

Vector2D HTView::global_to_local_ws_unscaled(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr || !overview_layout.count(workspace_id))
        return {};
    CBox workspace_box = overview_layout[workspace_id].box;
    if (workspace_box.empty())
        return {};
    pos -= monitor->vecPosition;
    pos *= monitor->scale;
    pos -= workspace_box.pos();
    pos /= monitor->scale;
    pos /= scale.value();
    return pos;
}

Vector2D HTView::global_to_local_ws_scaled(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};
    pos = global_to_local_ws_unscaled(pos, workspace_id);
    pos *= monitor->scale;
    return pos;
}

Vector2D HTView::local_ws_unscaled_to_global(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr || !overview_layout.count(workspace_id))
        return {};
    CBox workspace_box = overview_layout[workspace_id].box;
    if (workspace_box.empty())
        return {};
    pos *= scale.value();
    pos *= monitor->scale;
    pos += workspace_box.pos();
    pos /= monitor->scale;
    pos += monitor->vecPosition;
    return pos;
}

Vector2D HTView::local_ws_scaled_to_global(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    pos /= monitor->scale;
    return local_ws_unscaled_to_global(pos, workspace_id);
}

WORKSPACEID HTView::get_ws_id_from_global(Vector2D pos) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return WORKSPACE_INVALID;
    if (!monitor->logicalBox().containsPoint(pos))
        return WORKSPACE_INVALID;

    Vector2D relative_pos = (pos - monitor->vecPosition) * monitor->scale;
    for (const auto& [id, layout] : overview_layout)
        if (layout.box.containsPoint(relative_pos))
            return id;

    return WORKSPACE_INVALID;
}

CBox HTView::get_global_window_box(PHLWINDOW window, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr || window == nullptr)
        return {};
    PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(workspace_id);
    if (workspace == nullptr || workspace->m_pMonitor != monitor)
        return {};

    CBox ws_window_box = {window->m_vRealPosition.value(), window->m_vRealSize.value()};

    Vector2D top_left =
        local_ws_unscaled_to_global(ws_window_box.pos() - monitor->vecPosition, workspace->m_iID);
    Vector2D bottom_right = local_ws_unscaled_to_global(
        ws_window_box.pos() + ws_window_box.size() - monitor->vecPosition,
        workspace->m_iID
    );

    return {top_left, bottom_right - top_left};
}

PHLMONITOR HTView::get_monitor() {
    return g_pCompositor->getMonitorFromID(monitor_id);
}

bool HTView::is_active() {
    return active;
}

bool HTView::is_closing() {
    return closing;
}

bool HTView::is_navigating() {
    return navigating;
}

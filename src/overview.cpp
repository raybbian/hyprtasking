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

    build_overview_layout(-1);
    const CBox ws_box = overview_layout[get_monitor()->activeWorkspaceID()].box;
    offset.setValueAndWarp(-ws_box.pos());
}

WORKSPACEID HTView::get_exit_workspace_id(bool override_hover) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr) //???
        return WORKSPACE_INVALID;

    auto try_get_hover_id = [this, &monitor]() {
        const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
        if (cursor_monitor != monitor)
            return WORKSPACE_INVALID;

        const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
        return get_ws_id_from_global(mouse_coords);
    };

    auto try_get_original_id = [this]() {
        const PHLWORKSPACE workspace = ori_workspace.lock();
        return workspace == nullptr ? WORKSPACE_INVALID : workspace->m_iID;
    };

    if (override_hover) {
        const WORKSPACEID hover_ws_id = try_get_hover_id();
        if (hover_ws_id != WORKSPACE_INVALID)
            return hover_ws_id;
    }

    CVarList exit_behavior {HTConfig::exit_behavior(), 0, 's', true};
    for (const auto& behavior : exit_behavior) {
        WORKSPACEID switch_to_ws_id = WORKSPACE_INVALID;
        if (behavior == "hovered") {
            switch_to_ws_id = try_get_hover_id();
        } else if (behavior == "original") {
            switch_to_ws_id = try_get_original_id();
        } else if (behavior == "interacted") {
            switch_to_ws_id = monitor->activeWorkspaceID();
        } else {
            Debug::log(WARN, "[Hyprtasking] invalid behavior for exit behavior: {}", behavior);
        }

        if (switch_to_ws_id != WORKSPACE_INVALID)
            return switch_to_ws_id;
    }
    return WORKSPACE_INVALID;
}

void HTView::do_exit_behavior(bool override_hover) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr) //???
        return;
    const WORKSPACEID ws_id = get_exit_workspace_id(override_hover);
    PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(ws_id);

    if (workspace == nullptr && ws_id != WORKSPACE_INVALID)
        workspace = g_pCompositor->createNewWorkspace(ws_id, monitor->ID);
    if (workspace == nullptr)
        return;

    monitor->changeWorkspace(workspace);
    workspace->startAnim(true, false, true);
}

void HTView::show() {
    if (active)
        return;
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    active = true;
    ori_workspace = monitor->activeWorkspace;

    build_overview_layout(1);
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

    build_overview_layout(-1);
    const CBox ws_box = overview_layout[monitor->activeWorkspaceID()].box;
    scale = 1.;
    offset = -ws_box.pos();

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

    build_overview_layout(-1);
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
                offset = -overview_layout[other_workspace->m_iID].box.pos();
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

#include "manager.hpp"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>

#include "config/shared/workspace/WorkspaceRuleManager.hpp"
#include "layout/grid.hpp"
#include "overview.hpp"

HTManager::HTManager() {
    swipe_state = HT_SWIPE_NONE;
    swipe_amt = 0.0;
}

PHTVIEW HTManager::get_view_from_monitor(PHLMONITOR monitor) {
    if (monitor == nullptr)
        return nullptr;
    for (PHTVIEW view : views) {
        if (view == nullptr)
            continue;
        if (view->get_monitor() != monitor)
            continue;
        return view;
    }
    return nullptr;
}

PHTVIEW HTManager::get_view_from_cursor() {
    return get_view_from_monitor(g_pCompositor->getMonitorFromCursor());
}

PHTVIEW HTManager::get_view_from_id(VIEWID view_id) {
    for (PHTVIEW view : views) {
        if (view == nullptr)
            continue;
        if (view->monitor_id != view_id)
            continue;
        return view;
    }
    return nullptr;
}

PHLWINDOW HTManager::get_window_from_cursor(bool return_focused) {
    const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
    if (cursor_monitor == nullptr)
        return nullptr;

    if (return_focused)
        return cursor_monitor->m_activeWorkspace->getLastFocusedWindow();

    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (cursor_view == nullptr)
        return nullptr;

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();

    if (!cursor_view->active || !cursor_view->layout->should_manage_mouse()) {
        return g_pCompositor->vectorToWindowUnified(
            mouse_coords,
            Desktop::View::RESERVED_EXTENTS | Desktop::View::INPUT_EXTENTS | Desktop::View::ALLOW_FLOATING
        );
    }

    const WORKSPACEID ws_id = cursor_view->layout->get_ws_id_from_global(mouse_coords);
    const PHLWORKSPACE hovered_workspace = g_pCompositor->getWorkspaceByID(ws_id);
    if (hovered_workspace == nullptr)
        return nullptr;

    const Vector2D ws_coords = cursor_view->layout->global_to_local_ws_unscaled(mouse_coords, ws_id)
        + cursor_monitor->m_position;

    const PHLWORKSPACEREF o_workspace = cursor_monitor->m_activeWorkspace;
    cursor_monitor->changeWorkspace(hovered_workspace, true);

    const PHLWINDOW hovered_window = g_pCompositor->vectorToWindowUnified(
        ws_coords,
        Desktop::View::RESERVED_EXTENTS | Desktop::View::INPUT_EXTENTS | Desktop::View::ALLOW_FLOATING
    );

    if (o_workspace != nullptr)
        cursor_monitor->changeWorkspace(o_workspace.lock(), true);

    return hovered_window;
}

void HTManager::show_all_views() {
    for (PHTVIEW view : views) {
        if (view == nullptr)
            continue;
        view->show();
    }
}

void HTManager::hide_all_views() {
    for (PHTVIEW view : views) {
        if (view == nullptr)
            continue;
        view->hide(false);
    }
}

void HTManager::show_cursor_view() {
    const PHTVIEW view = get_view_from_cursor();
    if (view != nullptr)
        view->show();
}

void HTManager::reset() {
    swipe_state = HT_SWIPE_NONE;
    swipe_amt = 0.0;
    views.clear();
}

void HTManager::refresh_all_grid_caches() {
    // Enforce monitor-binding rules globally first. Per-grid refresh below
    // sees one workspace at a time on one monitor; if a rule-bound ws still
    // lives on the wrong monitor, the first grid to refresh would claim it
    // via Pass 2 and the rule-bound grid would then skip it via off_limits,
    // so the migration would never happen.
    const auto& all_rules = Config::workspaceRuleMgr()->getAllWorkspaceRules();
    for (const auto& rule : all_rules) {
        if (rule.m_workspaceId <= 0)
            continue;
        const auto bound = Config::workspaceRuleMgr()->getBoundMonitorForWS(
            rule.m_workspaceName.starts_with("name:") ? rule.m_workspaceName.substr(5)
                                                    : rule.m_workspaceName
        );
        if (bound == nullptr)
            continue;
        const PHLWORKSPACE ws = g_pCompositor->getWorkspaceByID(rule.m_workspaceId);
        if (ws == nullptr)
            continue;
        if (ws->m_monitor.lock() != bound)
            g_pCompositor->moveWorkspaceToMonitor(ws, bound);
    }

    std::vector<HTLayoutGrid*> grids;
    grids.reserve(views.size());
    for (PHTVIEW view : views) {
        if (view == nullptr || view->layout == nullptr)
            continue;
        if (view->layout->layout_name() != "grid")
            continue;
        grids.push_back(static_cast<HTLayoutGrid*>(view->layout.get()));
    }
    // Sorted iteration so synthetic-ID assignment is stable run-to-run.
    std::sort(grids.begin(), grids.end(), [](HTLayoutGrid* a, HTLayoutGrid* b) {
        return a->get_view_id() < b->get_view_id();
    });

    std::unordered_set<WORKSPACEID> taken;
    for (HTLayoutGrid* grid : grids) {
        grid->refresh_workspace_cache(taken);
        for (const auto& [id, slot] : grid->cache())
            taken.insert(id);
    }

    // Re-anchor each inactive view's overlay on its monitor's current
    // active workspace. Hyprland may have switched the active workspace
    // (e.g. after rule-driven migration moved the previous one away), and
    // without this the overlay's stored offset still points at the old
    // slot, producing a visible jump when the overview opens.
    for (PHTVIEW view : views) {
        if (view == nullptr || view->layout == nullptr)
            continue;
        if (view->active)
            continue;
        view->layout->init_position();
    }
}

void HTManager::remove_view_for_monitor_id(MONITORID mid) {
    std::erase_if(views, [mid](const PHTVIEW& v) {
        return v == nullptr || v->monitor_id == mid;
    });
}

bool HTManager::has_active_view() {
    for (const auto& view : views) {
        if (view == nullptr)
            continue;
        if (view->active)
            return true;
    }
    return false;
}

bool HTManager::cursor_view_active() {
    const PHTVIEW view = get_view_from_cursor();
    if (view == nullptr)
        return false;
    return view->active;
}

#include "workspace.hpp"

#include <unordered_map>
#include <set>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>

static std::unordered_map<MONITORID, WORKSPACEID> next_workspace_ids;

// Keep allocation local to the requested monitor. Workspace rules may move a
// newly created id elsewhere, so every candidate is checked before returning.
static bool is_workspace_on_monitor(PHLWORKSPACE workspace, PHLMONITOR monitor) {
    return workspace != nullptr && monitor != nullptr && !workspace->inert()
        && !workspace->m_isSpecialWorkspace && workspace->monitorID() == monitor->m_id;
}

PHLWORKSPACE create_workspace_for_monitor(PHLMONITOR monitor) {
    if (monitor == nullptr)
        return nullptr;

    // Collect existing workspace IDs on this monitor to find gaps
    std::set<WORKSPACEID> monitor_ws_ids;
    for (PHLWORKSPACE ws : g_pCompositor->getWorkspacesCopy()) {
        if (ws != nullptr && !ws->inert() && !ws->m_isSpecialWorkspace
            && ws->monitorID() == monitor->m_id) {
            monitor_ws_ids.insert(ws->m_id);
        }
    }

    // Find lowest available ID >= 1 to avoid monotonically growing IDs
    WORKSPACEID candidate = 1;
    for (WORKSPACEID id : monitor_ws_ids) {
        if (id == candidate)
            ++candidate;
        else if (id > candidate)
            break;
    }

    // Fallback to counter-based allocation if all low IDs are taken
    WORKSPACEID& next_id = next_workspace_ids[monitor->m_id];
    if (next_id < candidate)
        next_id = candidate;

    while (true) {
        PHLWORKSPACE existing = g_pCompositor->getWorkspaceByID(next_id);
        if (existing != nullptr) {
            ++next_id;
            if (next_id < 0)
                next_id = 1;
            continue;
        }

        PHLWORKSPACE workspace = g_pCompositor->createNewWorkspace(next_id, monitor->m_id, "", false);
        if (workspace == nullptr)
            return nullptr;

        // Workspace rules may redirect an id to another monitor. Do not keep
        // probing after that, or we could create a chain of wrong workspaces.
        if (is_workspace_on_monitor(workspace, monitor))
            return workspace;

        return nullptr;
    }
}

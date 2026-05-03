#include "workspace.hpp"

#include <limits>
#include <unordered_map>

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

static WORKSPACEID highest_workspace_id_on_monitor(PHLMONITOR monitor) {
    WORKSPACEID highest_id = 0;
    for (PHLWORKSPACE ws : g_pCompositor->getWorkspacesCopy()) {
        if (is_workspace_on_monitor(ws, monitor) && ws->m_id > highest_id)
            highest_id = ws->m_id;
    }
    return highest_id;
}

static WORKSPACEID highest_workspace_id() {
    WORKSPACEID highest_id = 0;
    for (PHLWORKSPACE ws : g_pCompositor->getWorkspacesCopy()) {
        if (ws != nullptr && !ws->m_isSpecialWorkspace && ws->m_id > highest_id)
            highest_id = ws->m_id;
    }
    return highest_id;
}

PHLWORKSPACE create_workspace_for_monitor(PHLMONITOR monitor) {
    if (monitor == nullptr)
        return nullptr;

    WORKSPACEID& next_id = next_workspace_ids[monitor->m_id];
    if (next_id <= 0)
        // Start after the monitor's own range instead of from the global max;
        // this keeps split-monitor/ranged setups from jumping unnecessarily.
        next_id = highest_workspace_id_on_monitor(monitor);

    constexpr int MAX_ATTEMPTS = 10000;
    for (int attempts = 0; attempts < MAX_ATTEMPTS; ++attempts) {
        if (next_id >= std::numeric_limits<WORKSPACEID>::max())
            next_id = 0;
        ++next_id;

        PHLWORKSPACE existing = g_pCompositor->getWorkspaceByID(next_id);
        if (existing != nullptr)
            continue;

        PHLWORKSPACE workspace = g_pCompositor->createNewWorkspace(next_id, monitor->m_id, "", false);
        if (workspace == nullptr)
            continue;

        // Workspace rules may redirect an id to another monitor. Do not keep
        // probing after that, or we could create a chain of wrong workspaces.
        if (is_workspace_on_monitor(workspace, monitor))
            return workspace;

        return nullptr;
    }

    // Extremely dense layouts can fill the bounded search window. Fall back to
    // the old global max+1 behavior rather than failing forever.
    next_id = highest_workspace_id();
    if (next_id >= std::numeric_limits<WORKSPACEID>::max())
        return nullptr;
    ++next_id;

    PHLWORKSPACE workspace = g_pCompositor->createNewWorkspace(next_id, monitor->m_id, "", false);
    if (is_workspace_on_monitor(workspace, monitor))
        return workspace;

    return nullptr;
}

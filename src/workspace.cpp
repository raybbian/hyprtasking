#include "workspace.hpp"

#include <unordered_map>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>

static std::unordered_map<MONITORID, WORKSPACEID> reusable_workspace_ids;

// Safe only for the workspace we are leaving: it already belongs to this
// monitor, and with no windows Hyprland would clean it up anyway.
bool can_reuse_empty_workspace(PHLWORKSPACE workspace, PHLMONITOR monitor) {
    return workspace != nullptr && monitor != nullptr && !workspace->inert()
        && !workspace->m_isSpecialWorkspace && workspace->m_id > 0
        && workspace->monitorID() == monitor->m_id && workspace->getWindows() == 0;
}

void remember_empty_workspace(PHLWORKSPACE workspace, PHLMONITOR monitor) {
    if (can_reuse_empty_workspace(workspace, monitor))
        reusable_workspace_ids[monitor->m_id] = workspace->m_id;
}

PHLWORKSPACE create_workspace_for_monitor(PHLMONITOR monitor) {
    if (monitor == nullptr)
        return nullptr;

    const auto reusable_it = reusable_workspace_ids.find(monitor->m_id);
    if (reusable_it != reusable_workspace_ids.end()) {
        const WORKSPACEID reusable_id = reusable_it->second;
        reusable_workspace_ids.erase(reusable_it);

        // If the empty workspace still exists, use it directly. Otherwise ask
        // Hyprland to recreate the same id instead of going to max+1.
        PHLWORKSPACE reusable_workspace = g_pCompositor->getWorkspaceByID(reusable_id);
        if (can_reuse_empty_workspace(reusable_workspace, monitor))
            return reusable_workspace;

        if (reusable_workspace == nullptr && reusable_id > 0)
            return g_pCompositor->createNewWorkspace(reusable_id, monitor->m_id, "", false);
    }

    WORKSPACEID next_id = 1;
    for (PHLWORKSPACE ws : g_pCompositor->getWorkspacesCopy()) {
        if (ws != nullptr && !ws->m_isSpecialWorkspace && ws->m_id >= next_id)
            next_id = ws->m_id + 1;
    }

    // Fallback to the old behavior when the source workspace is not reusable.
    return g_pCompositor->createNewWorkspace(next_id, monitor->m_id, "", false);
}

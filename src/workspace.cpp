#include "workspace.hpp"

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

PHLWORKSPACE create_workspace_for_monitor(PHLMONITOR monitor) {
    if (monitor == nullptr)
        return nullptr;

    WORKSPACEID& next_id = next_workspace_ids[monitor->m_id];

    while (true) {
        ++next_id;
        if (next_id < 0)
            next_id = 1;

        PHLWORKSPACE existing = g_pCompositor->getWorkspaceByID(next_id);
        if (existing != nullptr)
            continue;

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

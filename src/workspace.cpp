#include "workspace.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>

bool can_reuse_empty_workspace(PHLWORKSPACE workspace, PHLMONITOR monitor) {
    return workspace != nullptr && monitor != nullptr && !workspace->inert()
        && !workspace->m_isSpecialWorkspace && workspace->m_id > 0
        && workspace->monitorID() == monitor->m_id && workspace->getWindows() == 0;
}


PHLWORKSPACE create_workspace_for_monitor(PHLMONITOR monitor) {
    if (monitor == nullptr)
        return nullptr;

    WORKSPACEID next_id = 1;
    for (PHLWORKSPACE ws : g_pCompositor->getWorkspacesCopy()) {
        if (ws != nullptr && !ws->m_isSpecialWorkspace && ws->m_id >= next_id)
            next_id = ws->m_id + 1;
    }

    return g_pCompositor->createNewWorkspace(next_id, monitor->m_id, "", false);
}

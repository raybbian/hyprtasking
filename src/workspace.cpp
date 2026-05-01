#include "workspace.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>

static void remember_workspace_id(std::vector<WORKSPACEID>& ids, WORKSPACEID id) {
    if (id <= 0 || std::ranges::find(ids, id) != ids.end())
        return;
    ids.push_back(id);
    std::ranges::sort(ids);
}

static PHLWORKSPACE create_workspace_with_id(PHLMONITOR monitor, WORKSPACEID id) {
    if (id <= 0)
        return nullptr;

    return g_pCompositor->createNewWorkspace(id, monitor->m_id, "", false);
}

static bool is_workspace_on_monitor(PHLWORKSPACE workspace, MONITORID monitor_id) {
    return workspace != nullptr && !workspace->inert() && !workspace->m_isSpecialWorkspace
        && workspace->monitorID() == monitor_id;
}

PHLWORKSPACE create_workspace_for_monitor(PHLMONITOR monitor) {
    if (monitor == nullptr)
        return nullptr;

    static std::unordered_map<MONITORID, std::vector<WORKSPACEID>> monitor_workspace_history;

    std::unordered_set<WORKSPACEID> used_ids;
    std::unordered_set<WORKSPACEID> reserved_ids;
    std::unordered_set<MONITORID> live_monitor_ids;
    std::vector<WORKSPACEID> monitor_ids;
    WORKSPACEID next_id = 1;

    for (const PHLMONITOR& live_monitor : g_pCompositor->m_monitors) {
        if (live_monitor != nullptr)
            live_monitor_ids.insert(live_monitor->m_id);
    }

    for (auto it = monitor_workspace_history.begin(); it != monitor_workspace_history.end(); ) {
        if (!live_monitor_ids.contains(it->first))
            it = monitor_workspace_history.erase(it);
        else
            ++it;
    }

    for (PHLWORKSPACE ws : g_pCompositor->getWorkspacesCopy()) {
        if (ws == nullptr || ws->m_isSpecialWorkspace)
            continue;

        used_ids.insert(ws->m_id);
        if (ws->m_id >= next_id)
            next_id = ws->m_id + 1;

        if (is_workspace_on_monitor(ws, monitor->m_id) && ws->m_id > 0)
            monitor_ids.push_back(ws->m_id);
    }

    std::vector<WORKSPACEID>& remembered_ids = monitor_workspace_history[monitor->m_id];

    for (const auto& [monitor_id, ids] : monitor_workspace_history) {
        if (monitor_id == monitor->m_id)
            continue;

        for (WORKSPACEID id : ids) {
            if (id > 0)
                reserved_ids.insert(id);
        }
    }

    bool created_wrong_monitor_workspace = false;

    auto try_create = [&](WORKSPACEID id) -> PHLWORKSPACE {
        if (created_wrong_monitor_workspace || id <= 0 || used_ids.contains(id) || reserved_ids.contains(id))
            return nullptr;

        PHLWORKSPACE workspace = create_workspace_with_id(monitor, id);
        if (workspace == nullptr)
            return nullptr;

        if (!is_workspace_on_monitor(workspace, monitor->m_id)) {
            created_wrong_monitor_workspace = true;
            return nullptr;
        }

        remember_workspace_id(remembered_ids, workspace->m_id);
        return workspace;
    };

    // Prefer IDs hyprtasking already created for this monitor. Empty non-
    // persistent workspaces can disappear, so this keeps revisiting the same
    // empty grid slot from steadily increasing workspace IDs.
    for (WORKSPACEID id : remembered_ids) {
        if (PHLWORKSPACE workspace = try_create(id); workspace != nullptr)
            return workspace;
        if (created_wrong_monitor_workspace)
            return nullptr;
    }

    if (!monitor_ids.empty()) {
        std::ranges::sort(monitor_ids);
        const WORKSPACEID first_id = monitor_ids.front();
        const WORKSPACEID last_id = monitor_ids.back();

        // Only fill gaps inside this monitor's existing ID span. Reusing the
        // global lowest free ID could cross monitor-specific workspace ranges.
        for (WORKSPACEID id = first_id; id <= last_id; ++id) {
            if (PHLWORKSPACE workspace = try_create(id); workspace != nullptr)
                return workspace;
            if (created_wrong_monitor_workspace)
                return nullptr;
        }
    }

    while (used_ids.contains(next_id) || reserved_ids.contains(next_id))
        ++next_id;

    return try_create(next_id);
}

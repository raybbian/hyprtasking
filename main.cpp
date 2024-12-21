#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "overview.hpp"

inline HANDLE PHANDLE = nullptr;

Hyprutils::Memory::CSharedPointer<HOOK_CALLBACK_FN> g_pAddMonitorHook;

static void failNotification(const std::string &reason) {
    HyprlandAPI::addNotification(PHANDLE, "[Hyprtasking] " + reason,
                                 CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
}

static void infoNotification(const std::string &message) {
    HyprlandAPI::addNotification(PHANDLE, "[Hyprtasking] " + message,
                                 CHyprColor{0.2, 0.2, 1.0, 1.0}, 5000);
}

APICALL EXPORT std::string PLUGIN_API_VERSION() { return HYPRLAND_API_VERSION; }

std::shared_ptr<CHyprtaskingView> getViewForMonitor(PHLMONITORREF pMonitor) {
    for (auto &view : g_overviews) {
        if (!view)
            continue;
        if (view->getMonitor() != pMonitor)
            continue;
        return view;
    }
    return nullptr;
}

void registerMonitors() {
    for (auto &m : g_pCompositor->m_vMonitors) {
        if (getViewForMonitor(m) != nullptr)
            continue;

        infoNotification("Creating view for monitor " + m->szName);

        CHyprtaskingView *view = new CHyprtaskingView(m->ID);
        g_overviews.emplace_back(view);
    }
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH = __hyprland_api_get_hash();

    if (HASH != GIT_COMMIT_HASH) {
        failNotification("Mismatched headers! Can't proceed.");
        throw std::runtime_error("[Hyprtasking] Version mismatch");
    }

    registerMonitors();
    g_pAddMonitorHook = HyprlandAPI::registerCallbackDynamic(
        PHANDLE, "monitorAdded",
        [&](void *thisptr, SCallbackInfo &info, std::any data) {
            registerMonitors();
        });

    infoNotification("Plugin initialized");

    return {"Hyprtasking", "A workspace management plugin", "raybbian", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    // ...
}

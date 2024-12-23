#include <ctime>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprutils/math/Box.hpp>

#include "globals.hpp"
#include "overview.hpp"

CFunctionHook *g_pRenderWorkspaceHook;

APICALL EXPORT std::string PLUGIN_API_VERSION() { return HYPRLAND_API_VERSION; }

static std::shared_ptr<CHyprtaskingView>
getViewForMonitor(PHLMONITORREF pMonitor) {
    for (auto &view : g_overviews) {
        if (!view)
            continue;
        if (view->getMonitor() != pMonitor)
            continue;
        return view;
    }
    return nullptr;
}

static SDispatchResult dispatchToggleView(std::string arg) {
    const auto currentMonitor = g_pCompositor->getMonitorFromCursor();
    const auto currentView = getViewForMonitor(currentMonitor);
    if (currentView == nullptr)
        return SDispatchResult{false, false, "Failed to get view for monitor."};

    if (currentView->isActive()) {
        Debug::log(LOG, "[Hyprtasking] Hiding overviews");
        for (auto &view : g_overviews) {
            if (view == nullptr)
                continue;
            if (!view->isActive())
                continue;
            view->hide();
        }
    } else {
        Debug::log(LOG, "[Hyprtasking] Showing overviews");
        for (auto &view : g_overviews) {
            if (view == nullptr)
                continue;
            if (view->isActive())
                continue;
            view->show();
        }
    }

    return SDispatchResult{};
}

static void hkRenderWorkspace(void *thisptr, PHLMONITOR pMonitor,
                              PHLWORKSPACE pWorkspace, timespec *now,
                              const CBox &geometry) {
    const auto view = getViewForMonitor(pMonitor);
    if (view == nullptr || !view->isActive()) {
        ((tRenderWorkspace)(g_pRenderWorkspaceHook->m_pOriginal))(
            thisptr, pMonitor, pWorkspace, now, geometry);
        return;
    }
    view->render();
}

static void registerMonitors() {
    for (auto &m : g_pCompositor->m_vMonitors) {
        if (getViewForMonitor(m) != nullptr)
            continue;

        Debug::log(LOG, "[Hyprtasking] Creating view for monitor " + m->szName);

        CHyprtaskingView *view = new CHyprtaskingView(m->ID);
        g_overviews.emplace_back(view);
    }
}

static void failNotification(const std::string &reason) {
    HyprlandAPI::addNotification(PHANDLE, "[Hyprtasking] " + reason,
                                 CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
}

static void initFunctions() {
    static auto FNS =
        HyprlandAPI::findFunctionsByName(PHANDLE, "renderWorkspace");
    if (FNS.empty()) {
        failNotification("No fns for hook renderWorkspace!");
        throw std::runtime_error(
            "[Hyprtasking] No fns for hook renderWorkspace");
    }
    g_pRenderWorkspaceHook = HyprlandAPI::createFunctionHook(
        PHANDLE, FNS[0].address, (void *)hkRenderWorkspace);

    bool success = g_pRenderWorkspaceHook->hook();
    if (!success) {
        failNotification("Failed initializing hooks");
        throw std::runtime_error("[Hyprtasking] Failed initializing hooks");
    }
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH = __hyprland_api_get_hash();

    if (HASH != GIT_COMMIT_HASH) {
        failNotification("Mismatched headers! Can't proceed.");
        throw std::runtime_error("[Hyprtasking] Version mismatch");
    }

    HyprlandAPI::addDispatcher(PHANDLE, "hyprtasking:toggle",
                               dispatchToggleView);

    initFunctions();

    registerMonitors();
    static auto P2 = HyprlandAPI::registerCallbackDynamic(
        PHANDLE, "monitorAdded",
        [&](void *thisptr, SCallbackInfo &info, std::any data) {
            registerMonitors();
        });

    Debug::log(LOG, "[Hyprtasking] Plugin initialized");

    return {"Hyprtasking", "A workspace management plugin", "raybbian", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    // ...
}

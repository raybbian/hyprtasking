#include <ctime>
#include <hyprutils/math/Vector2D.hpp>
#include <linux/input-event-codes.h>

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

APICALL EXPORT std::string PLUGIN_API_VERSION() { return HYPRLAND_API_VERSION; }

static std::shared_ptr<CHyprtaskingView>
getViewForMonitor(PHLMONITORREF pMonitor) {
    if (pMonitor == nullptr)
        return nullptr;
    for (auto &view : g_vOverviews) {
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
        for (auto &view : g_vOverviews) {
            if (view == nullptr)
                continue;
            if (!view->isActive())
                continue;
            view->hide();
        }
    } else {
        Debug::log(LOG, "[Hyprtasking] Showing overviews");
        for (auto &view : g_vOverviews) {
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

// Only hook when triggering a window drag
static Vector2D hkGetMouseCoordsInternal(void *thisptr) {
    const Vector2D oMousePos =
        ((tGetMouseCoordsInternal)(g_pGetMouseCoordsInternalHook->m_pOriginal))(
            thisptr);
    const PHLMONITOR pMonitor = g_pCompositor->getMonitorFromVector(oMousePos);
    const auto view = getViewForMonitor(pMonitor);

    if (pMonitor == nullptr || view == nullptr || !view->isActive())
        return oMousePos;

    return view->mouseCoordsWorkspaceRelative(oMousePos);
}

static void onMouseButton(void *thisptr, SCallbackInfo &info, std::any args) {

    const PHLMONITOR pMonitor = g_pCompositor->getMonitorFromCursor();
    const auto view = getViewForMonitor(pMonitor);
    if (pMonitor == nullptr || view == nullptr || !view->isActive())
        return;

    info.cancelled = true;

    const auto e = std::any_cast<IPointer::SButtonEvent>(args);
    if (e.button != BTN_LEFT)
        return;
    const bool pressed = e.state == WL_POINTER_BUTTON_STATE_PRESSED;
    view->mouseButtonEvent(pressed);
}

static void registerMonitors() {
    for (auto &m : g_pCompositor->m_vMonitors) {
        if (getViewForMonitor(m) != nullptr)
            continue;

        Debug::log(LOG, "[Hyprtasking] Creating view for monitor " + m->szName);

        CHyprtaskingView *view = new CHyprtaskingView(m->ID);
        g_vOverviews.emplace_back(view);
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

    FNS = HyprlandAPI::findFunctionsByName(PHANDLE, "getMouseCoordsInternal");
    if (FNS.empty()) {
        failNotification("No fns for hook getMouseCoordsInternal");
        throw std::runtime_error(
            "[Hyprtasking] No fns for hook getMouseCoordsInternal");
    }
    g_pGetMouseCoordsInternalHook = HyprlandAPI::createFunctionHook(
        PHANDLE, FNS[0].address, (void *)hkGetMouseCoordsInternal);

    FNS = HyprlandAPI::findFunctionsByName(PHANDLE, "renderWindow");
    if (FNS.empty()) {
        failNotification("No renderWindow");
        throw std::runtime_error("[Hyprtasking] No renderWindow");
    }
    g_pRenderWindow = FNS[0].address;

    bool success = g_pRenderWorkspaceHook->hook();
    success = g_pGetMouseCoordsInternalHook->hook();
    if (!success) {
        failNotification("Failed initializing hooks");
        throw std::runtime_error("[Hyprtasking] Failed initializing hooks");
    }
}

static void registerCallbacks() {
    static auto P1 = HyprlandAPI::registerCallbackDynamic(
        PHANDLE, "mouseButton", onMouseButton);
    static auto P2 = HyprlandAPI::registerCallbackDynamic(
        PHANDLE, "monitorAdded",
        [&](void *thisptr, SCallbackInfo &info, std::any data) {
            registerMonitors();
        });
    registerMonitors();
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
    registerCallbacks();

    Debug::log(LOG, "[Hyprtasking] Plugin initialized");

    return {"Hyprtasking", "A workspace management plugin", "raybbian", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    // ...
}

#include <ctime>
#include <linux/input-event-codes.h>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/LayoutManager.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/plugins/PluginSystem.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "globals.hpp"
#include "overview.hpp"

APICALL EXPORT std::string PLUGIN_API_VERSION() { return HYPRLAND_API_VERSION; }

static SDispatchResult dispatchToggleView(std::string arg) {
    if (g_pHyprtasking == nullptr)
        return SDispatchResult{false, false, "Failed to get view for monitor."};

    if (!g_pHyprtasking->isActive()) {
        Debug::log(LOG, "[Hyprtasking] Showing overviews");
        g_pHyprtasking->show();
    } else {
        Debug::log(LOG, "[Hyprtasking] Hiding overviews");
        g_pHyprtasking->hide();
    }

    return SDispatchResult{};
}

static void hkRenderWorkspace(void *thisptr, PHLMONITOR pMonitor,
                              PHLWORKSPACE pWorkspace, timespec *now,
                              const CBox &geometry) {
    if (g_pHyprtasking == nullptr || !g_pHyprtasking->isActive()) {
        ((tRenderWorkspace)(g_pRenderWorkspaceHook->m_pOriginal))(
            thisptr, pMonitor, pWorkspace, now, geometry);
        return;
    }

    const PHTVIEW pView = g_pHyprtasking->getViewFromMonitor(pMonitor);
    if (pView == nullptr)
        return;

    pView->render();
}

static bool hkShouldRenderWindow(void *thisptr, PHLWINDOW pWindow,
                                 PHLMONITOR pMonitor) {
    const bool oResult =
        ((tShouldRenderWindow)g_pShouldRenderWindowHook->m_pOriginal)(
            thisptr, pWindow, pMonitor);

    if (g_pHyprtasking == nullptr || !g_pHyprtasking->isActive() ||
        pWindow == nullptr || pMonitor == nullptr)
        return oResult;

    const PHTVIEW pView = g_pHyprtasking->getViewFromMonitor(pMonitor);
    if (pView == nullptr)
        return oResult;

    const PHLWINDOW dragWindow = g_pInputManager->currentlyDraggedWindow.lock();
    if (dragWindow == nullptr || pWindow != dragWindow)
        return oResult;

    return pMonitor->activeWorkspace == pWindow->m_pWorkspace;

    // TODO: would realistically like to render the other half of the window on
    // the other monitor. This involves either (on the other monitor): changing
    // workspaces to the one that intersects with the window's render box, or
    // rendering the dragged window myself
}

static void onMouseButton(void *thisptr, SCallbackInfo &info, std::any args) {
    if (g_pHyprtasking == nullptr || !g_pHyprtasking->isActive())
        return;
    info.cancelled = true;
    const auto e = std::any_cast<IPointer::SButtonEvent>(args);
    const bool pressed = e.state == WL_POINTER_BUTTON_STATE_PRESSED;
    g_pHyprtasking->onMouseButton(pressed, e.button);
}

static void onMouseMove(void *thisptr, SCallbackInfo &info, std::any args) {
    if (g_pHyprtasking == nullptr || !g_pHyprtasking->isActive())
        return;
    info.cancelled = true;
    g_pHyprtasking->onMouseMove();
}

static void onKeyPress(void *thisptr, SCallbackInfo &info, std::any args) {
    if (g_pHyprtasking == nullptr || !g_pHyprtasking->isActive())
        return;

    const auto e = std::any_cast<IKeyboard::SKeyEvent>(
        std::any_cast<std::unordered_map<std::string, std::any>>(
            args)["event"]);

    if (e.keycode == KEY_ESC) {
        info.cancelled = true;
        g_pHyprtasking->hide();
    }
}

static void cancelEvent(void *thisptr, SCallbackInfo &info, std::any args) {
    if (g_pHyprtasking == nullptr || !g_pHyprtasking->isActive())
        return;
    info.cancelled = true;
}

static void registerMonitors() {
    if (g_pHyprtasking == nullptr)
        return;
    for (const PHLMONITOR &pMonitor : g_pCompositor->m_vMonitors) {
        if (g_pHyprtasking->getViewFromMonitor(pMonitor) != nullptr)
            continue;

        Debug::log(LOG, "[Hyprtasking] Creating view for monitor " +
                            pMonitor->szName);

        CHyprtaskingView *view = new CHyprtaskingView(pMonitor->ID);
        g_pHyprtasking->m_vViews.emplace_back(view);
    }
}

static void failNotification(const std::string &reason) {
    HyprlandAPI::addNotification(PHANDLE, "[Hyprtasking] " + reason,
                                 CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
}

static void initFunctions() {
    bool success = true;

    static auto FNS1 =
        HyprlandAPI::findFunctionsByName(PHANDLE, "renderWorkspace");
    if (FNS1.empty()) {
        failNotification("No renderWorkspace!");
        throw std::runtime_error("[Hyprtasking] No renderWorkspace");
    }
    if (g_pRenderWorkspaceHook == nullptr) {
        g_pRenderWorkspaceHook = HyprlandAPI::createFunctionHook(
            PHANDLE, FNS1[0].address, (void *)hkRenderWorkspace);
    }
    Debug::log(LOG, "[Hyprtasking] Attempting hook {}", FNS1[0].signature);
    success = g_pRenderWorkspaceHook->hook();

    static auto FNS2 = HyprlandAPI::findFunctionsByName(
        PHANDLE, "_ZN13CHyprRenderer18shouldRenderWindowEN9Hyprutils6Memory14CS"
                 "haredPointerI7CWindowEENS2_I8CMonitorEE");
    if (FNS2.empty()) {
        failNotification("No shouldRenderWindow");
        throw std::runtime_error("[Hyprtasking] No shouldRenderWindow");
    }
    if (g_pShouldRenderWindowHook == nullptr) {
        g_pShouldRenderWindowHook = HyprlandAPI::createFunctionHook(
            PHANDLE, FNS2[0].address, (void *)hkShouldRenderWindow);
    }
    Debug::log(LOG, "[Hyprtasking] Attempting hook {}", FNS2[0].signature);
    success = g_pShouldRenderWindowHook->hook() && success;

    static auto FNS3 =
        HyprlandAPI::findFunctionsByName(PHANDLE, "renderWindow");
    if (FNS3.empty()) {
        failNotification("No renderWindow");
        throw std::runtime_error("[Hyprtasking] No renderWindow");
    }
    g_pRenderWindow = FNS3[0].address;

    if (!success) {
        failNotification("Failed initializing hooks");
        throw std::runtime_error("[Hyprtasking] Failed initializing hooks");
    }
}

static void registerCallbacks() {
    static auto P1 = HyprlandAPI::registerCallbackDynamic(
        PHANDLE, "mouseButton", onMouseButton);
    static auto P2 =
        HyprlandAPI::registerCallbackDynamic(PHANDLE, "mouseMove", onMouseMove);
    static auto P3 =
        HyprlandAPI::registerCallbackDynamic(PHANDLE, "mouseAxis", cancelEvent);

    // TODO: support touch
    static auto P4 =
        HyprlandAPI::registerCallbackDynamic(PHANDLE, "touchDown", cancelEvent);
    static auto P5 =
        HyprlandAPI::registerCallbackDynamic(PHANDLE, "touchUp", cancelEvent);
    static auto P6 =
        HyprlandAPI::registerCallbackDynamic(PHANDLE, "touchMove", cancelEvent);

    static auto P7 =
        HyprlandAPI::registerCallbackDynamic(PHANDLE, "keyPress", onKeyPress);

    static auto P8 = HyprlandAPI::registerCallbackDynamic(
        PHANDLE, "monitorAdded",
        [&](void *thisptr, SCallbackInfo &info, std::any data) {
            registerMonitors();
        });
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH = __hyprland_api_get_hash();

    if (HASH != GIT_COMMIT_HASH) {
        failNotification("Mismatched headers! Can't proceed.");
        throw std::runtime_error("[Hyprtasking] Version mismatch");
    }

    if (g_pHyprtasking == nullptr)
        g_pHyprtasking = std::make_unique<CHyprtaskingManager>();
    else
        g_pHyprtasking->reset();

    HyprlandAPI::addDispatcher(PHANDLE, "hyprtasking:toggle",
                               dispatchToggleView);

    initFunctions();
    registerCallbacks();
    registerMonitors();

    Debug::log(LOG, "[Hyprtasking] Plugin initialized");

    return {"Hyprtasking", "A workspace management plugin", "raybbian", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    Debug::log(LOG, "[Hyprtasking] Plugin exiting");
    g_pHyprtasking->reset();
}

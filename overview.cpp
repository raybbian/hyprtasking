#include <ctime>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Box.hpp>

#include "overview.hpp"

CHyprtaskingView::CHyprtaskingView(MONITORID inMonitorID) {
    monitorID = inMonitorID;
    active = false;
}

CHyprtaskingView::~CHyprtaskingView() {}

void CHyprtaskingView::show() {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return;

    active = true;

    g_pHyprRenderer->damageMonitor(pMonitor);
    g_pCompositor->scheduleFrameForMonitor(pMonitor);
}

void CHyprtaskingView::hide() {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return;

    active = false;

    g_pHyprRenderer->damageMonitor(pMonitor);
    g_pCompositor->scheduleFrameForMonitor(pMonitor);
}

PHLMONITOR CHyprtaskingView::getMonitor() {
    return g_pCompositor->getMonitorFromID(monitorID);
}

bool CHyprtaskingView::isActive() { return active; }

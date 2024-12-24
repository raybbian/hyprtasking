#include <ctime>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Box.hpp>

#include "globals.hpp"
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

Vector2D
CHyprtaskingView::mouseCoordsWorkspaceRelative(Vector2D mousePos,
                                               PHLWORKSPACE pWorkspace) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return mousePos;

    if (pWorkspace == nullptr)
        pWorkspace = pMonitor->activeWorkspace;
    if (pWorkspace == nullptr)
        return mousePos;

    // mousePos is relative to (0, 0), whereas workspaceBoxes are relative
    // to the monitor. Make mousePos relative to the monitor.
    Vector2D relMousePos = mousePos - pMonitor->vecPosition;

    CBox workspaceBox{};
    for (const auto &[id, box] : workspaceBoxes) {
        if (pWorkspace->m_iID == id)
            workspaceBox = box;
    }

    if (workspaceBox.w == 0)
        return mousePos;

    Vector2D offset = relMousePos - workspaceBox.pos();
    // The offset between mouse position and workspace box position must be
    // scaled up to the actual size of the workspace
    offset *= ROWS;
    // Make offset relative to (0, 0) again
    offset += pMonitor->vecPosition;

    return offset;
}

PHLWORKSPACE CHyprtaskingView::mouseWorkspace(Vector2D mousePos) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return nullptr;
    // mousePos is relative to (0, 0), whereas workspaceBoxes are relative
    // to the monitor. Make mousePos relative to the monitor.
    Vector2D relMousePos = mousePos - pMonitor->vecPosition;
    for (const auto &[id, box] : workspaceBoxes) {
        if (box.containsPoint(relMousePos)) {
            return g_pCompositor->getWorkspaceByID(id);
        }
    }
    return nullptr;
}

PHLMONITOR CHyprtaskingView::getMonitor() {
    return g_pCompositor->getMonitorFromID(monitorID);
}

bool CHyprtaskingView::isActive() { return active; }

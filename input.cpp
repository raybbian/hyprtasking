#include <hyprland/src/debug/Log.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/Timer.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "globals.hpp"
#include "overview.hpp"
#include "src/Compositor.hpp"

void CHyprtaskingView::mouseButtonEvent(bool pressed) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return;

    const Vector2D mousePos =
        ((tGetMouseCoordsInternal)(g_pGetMouseCoordsInternalHook->m_pOriginal))(
            g_pInputManager.get());

    const PHLWORKSPACE hoverWorkspace = mouseWorkspace(mousePos);
    const PHLWINDOW dragWindow = g_pInputManager->currentlyDraggedWindow.lock();
    if (pressed) {
        g_pKeybindManager->changeMouseBindMode(MBIND_MOVE);
    } else if (dragWindow != nullptr) {
        dragWindow->moveToWorkspace(hoverWorkspace);
        pMonitor->changeWorkspace(hoverWorkspace);
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
    }
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
    for (const auto &[id, box] : workspaceBoxes) {
        if (box.containsPoint(mousePos)) {
            return g_pCompositor->getWorkspaceByID(id);
        }
    }
    return nullptr;
}

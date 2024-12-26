#include <cstdint>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <linux/input-event-codes.h>

#include "overview.hpp"

void CHyprtaskingManager::onMouseButton(bool pressed, uint32_t button) {
    const PHLMONITOR pMonitor = g_pCompositor->getMonitorFromCursor();
    const PHTVIEW pView = getViewFromCursor();
    if (pView == nullptr)
        return;

    const Vector2D mouseCoords = g_pInputManager->getMouseCoordsInternal();
    const WORKSPACEID workspaceID =
        pView->getWorkspaceIDFromVector(mouseCoords);
    PHLWORKSPACE pWorkspace = g_pCompositor->getWorkspaceByID(workspaceID);
    const Vector2D mappedCoords =
        pView->mouseCoordsWorkspaceRelative(mouseCoords, pWorkspace);

    if (button == BTN_LEFT) {
        if (pressed) {
            pMonitor->changeWorkspace(pWorkspace, true);

            g_pPointerManager->warpTo(mappedCoords);
            g_pKeybindManager->changeMouseBindMode(MBIND_MOVE);
            g_pPointerManager->warpTo(mouseCoords);

            Debug::log(
                LOG,
                "[Hyprtasking] Attempting to grab window at ({}, {}) on ws {}",
                mappedCoords.x, mappedCoords.y,
                pMonitor->activeWorkspace->m_iID);

        } else {
            // Release on empty dummy workspace, so create and switch to it
            const PHLWINDOW dragWindow =
                g_pInputManager->currentlyDraggedWindow.lock();
            if (dragWindow != nullptr && pWorkspace == nullptr &&
                workspaceID >= SPECIAL_WORKSPACE_START) {
                pWorkspace = g_pCompositor->createNewWorkspace(workspaceID,
                                                               pMonitor->ID);
                pMonitor->changeWorkspace(pWorkspace);
                g_pCompositor->moveWindowToWorkspaceSafe(dragWindow,
                                                         pWorkspace);
                // otherwise the window leaves blur (?) artifacts on all
                // workspaces
                dragWindow->m_fMovingToWorkspaceAlpha.setValueAndWarp(1.0);

                Debug::log(LOG, "[Hyprtasking] Creating new workspace {}",
                           workspaceID);
            }

            g_pPointerManager->warpTo(mappedCoords);
            g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
            g_pPointerManager->warpTo(mouseCoords);

            Debug::log(
                LOG,
                "[Hyprtasking] Attempting to drop window at ({}, {}) on ws {}",
                mappedCoords.x, mappedCoords.y,
                pMonitor->activeWorkspace->m_iID);
        }
    } else if (button == BTN_RIGHT) {
        if (pressed) {
            pMonitor->changeWorkspace(pWorkspace, true);
            pWorkspace->startAnim(true, false, true);
            hide();
        }
    }
}

void CHyprtaskingManager::onMouseMove() {
    const PHLMONITOR pMonitor = g_pCompositor->getMonitorFromCursor();
    const PHTVIEW pView = getViewFromCursor();
    if (pView == nullptr)
        return;

    const Vector2D mouseCoords = g_pInputManager->getMouseCoordsInternal();
    const WORKSPACEID workspaceID =
        pView->getWorkspaceIDFromVector(mouseCoords);
    const PHLWORKSPACE pWorkspace =
        g_pCompositor->getWorkspaceByID(workspaceID);
    const Vector2D mappedCoords =
        pView->mouseCoordsWorkspaceRelative(mouseCoords, pWorkspace);

    const PHLWINDOW dragWindow = g_pInputManager->currentlyDraggedWindow.lock();
    if (dragWindow == nullptr)
        return;

    // Will this cause unecessary callbacks to be fired?
    dragWindow->m_vRealPosition.setValueAndWarp(
        mappedCoords - dragWindow->m_vRealSize.value() / 2.);

    // Move the window's workspace
    if (pWorkspace != nullptr && pWorkspace != pMonitor->activeWorkspace) {
        pMonitor->changeWorkspace(pWorkspace, true);
        g_pCompositor->moveWindowToWorkspaceSafe(dragWindow, pWorkspace);
        // otherwise the window leaves blur (?) artifacts on all workspaces
        dragWindow->m_fMovingToWorkspaceAlpha.setValueAndWarp(1.0);
    }
}

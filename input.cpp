#include <cstdint>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <linux/input-event-codes.h>

#include "overview.hpp"

void CHyprtaskingManager::onMouseButton(bool pressed, uint32_t button) {
    const PHLMONITOR pMonitor = g_pCompositor->getMonitorFromCursor();
    const PHTVIEW pView = getViewFromMonitor(pMonitor);
    if (pView == nullptr)
        return;

    const Vector2D mouseCoords = g_pInputManager->getMouseCoordsInternal();
    const WORKSPACEID workspaceID =
        pView->getWorkspaceIDFromVector(mouseCoords);
    PHLWORKSPACE pWorkspace = g_pCompositor->getWorkspaceByID(workspaceID);
    const Vector2D mappedCoords =
        pView->posRelativeToWorkspaceID(mouseCoords, workspaceID);

    if (button == BTN_LEFT) {
        if (pressed) {
            // If left click on dummy workspace, do nothing
            if (pWorkspace == nullptr)
                return;

            pMonitor->changeWorkspace(pWorkspace, true);
            pWorkspace->startAnim(true, false, true);
            pWorkspace->m_bVisible = true;

            g_pPointerManager->warpTo(mappedCoords);
            g_pKeybindManager->changeMouseBindMode(MBIND_MOVE);
            g_pPointerManager->warpTo(mouseCoords);

            Debug::log(
                LOG,
                "[Hyprtasking] Attempting to grab window at ({}, {}) on ws {}",
                mappedCoords.x, mappedCoords.y,
                pMonitor->activeWorkspace->m_iID);

        } else {
            const PHLWINDOW dragWindow =
                g_pInputManager->currentlyDraggedWindow.lock();
            if (dragWindow == nullptr) {
                g_pPointerManager->warpTo(mappedCoords);
                g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
                g_pPointerManager->warpTo(mouseCoords);
                return;
            }

            // Release on empty dummy workspace, so create and switch to it
            if (pWorkspace == nullptr &&
                workspaceID >= SPECIAL_WORKSPACE_START) {
                pWorkspace = g_pCompositor->createNewWorkspace(workspaceID,
                                                               pMonitor->ID);
                pMonitor->changeWorkspace(pWorkspace);
                g_pCompositor->moveWindowToWorkspaceSafe(dragWindow,
                                                         pWorkspace);

                // Need to also adjust window's real size and position to make
                // animation correct. Mapped coords is dummy relative, so this
                // is good.
                dragWindow->m_vRealPosition.setValueAndWarp(
                    mappedCoords - dragWindow->m_vRealSize.value() / 2.);

                Debug::log(LOG, "[Hyprtasking] Creating new workspace {}",
                           workspaceID);
            }

            // Just in case the mouse teleported here??
            if (pWorkspace != nullptr) {
                pMonitor->changeWorkspace(pWorkspace, true);
                pWorkspace->startAnim(true, false, true);
                pWorkspace->m_bVisible = true;
            }

            g_pPointerManager->warpTo(mappedCoords);
            g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
            g_pPointerManager->warpTo(mouseCoords);

            // otherwise the window leaves blur (?) artifacts on all
            // workspaces
            dragWindow->m_fMovingToWorkspaceAlpha.setValueAndWarp(1.0);

            Debug::log(
                LOG,
                "[Hyprtasking] Attempting to drop window at ({}, {}) on ws {}",
                mappedCoords.x, mappedCoords.y,
                pMonitor->activeWorkspace->m_iID);
        }
    } else if (button == BTN_RIGHT) {
        if (pressed) {
            // If right click on dummy workspace, create and go here as well
            if (pWorkspace == nullptr &&
                workspaceID >= SPECIAL_WORKSPACE_START) {
                pWorkspace = g_pCompositor->createNewWorkspace(workspaceID,
                                                               pMonitor->ID);

                Debug::log(LOG, "[Hyprtasking] Creating new workspace {}",
                           workspaceID);
            }

            if (pWorkspace != nullptr) {
                pMonitor->changeWorkspace(pWorkspace, true);
                pWorkspace->startAnim(true, false, true);
                pWorkspace->m_bVisible = true;
                hide();
            }
        }
    }
}

void CHyprtaskingManager::onMouseMove() {
    const PHLMONITOR pMonitor = g_pCompositor->getMonitorFromCursor();
    const PHTVIEW pView = getViewFromMonitor(pMonitor);
    if (pView == nullptr)
        return;

    const Vector2D mouseCoords = g_pInputManager->getMouseCoordsInternal();
    const WORKSPACEID workspaceID =
        pView->getWorkspaceIDFromVector(mouseCoords);
    const PHLWORKSPACE pWorkspace =
        g_pCompositor->getWorkspaceByID(workspaceID);

    const PHLWINDOW dragWindow = g_pInputManager->currentlyDraggedWindow.lock();
    if (dragWindow == nullptr)
        return;
    const PHLWORKSPACE targetWorkspace =
        pWorkspace == nullptr ? pMonitor->activeWorkspace : pWorkspace;
    if (targetWorkspace == nullptr)
        return;

    const Vector2D mappedCoords =
        pView->posRelativeToWorkspaceID(mouseCoords, targetWorkspace->m_iID);
    dragWindow->m_vRealPosition.setValueAndWarp(
        mappedCoords - dragWindow->m_vRealSize.value() / 2.);

    pMonitor->changeWorkspace(targetWorkspace, true);
    targetWorkspace->startAnim(true, false, true);
    targetWorkspace->m_bVisible = true;

    g_pCompositor->moveWindowToWorkspaceSafe(dragWindow, targetWorkspace);
    // otherwise the window leaves blur (?) artifacts on all workspaces
    dragWindow->m_fMovingToWorkspaceAlpha.setValueAndWarp(1.0);

    // Need to do this again to avoid glitchy artifacts
    dragWindow->m_vRealPosition.setValueAndWarp(
        mappedCoords - dragWindow->m_vRealSize.value() / 2.);
}

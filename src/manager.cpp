#include "manager.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>

#include "overview.hpp"

HTManager::HTManager() {
    ;
}

PHTVIEW HTManager::get_view_from_monitor(PHLMONITOR pMonitor) {
    if (pMonitor == nullptr)
        return nullptr;
    for (PHTVIEW view : views) {
        if (view == nullptr)
            continue;
        if (view->get_monitor() != pMonitor)
            continue;
        return view;
    }
    return nullptr;
}

PHTVIEW HTManager::get_view_from_cursor() {
    return get_view_from_monitor(g_pCompositor->getMonitorFromCursor());
}

void HTManager::show_all_views() {
    for (PHTVIEW view : views) {
        if (view == nullptr)
            continue;
        view->show();
    }
}

void HTManager::hide_all_views() {
    for (PHTVIEW view : views) {
        if (view == nullptr)
            continue;
        view->do_exit_behavior();
        view->hide();
    }
}

void HTManager::show_cursor_view() {
    const PHTVIEW view = get_view_from_cursor();
    if (view != nullptr)
        view->show();
}

void HTManager::reset() {
    views.clear();
}

bool HTManager::has_active_view() {
    for (const auto& view : views) {
        if (view == nullptr)
            continue;
        if (view->is_active())
            return true;
    }
    return false;
}

bool HTManager::cursor_view_active() {
    const PHTVIEW view = get_view_from_cursor();
    if (view == nullptr)
        return false;
    return view->is_active();
}

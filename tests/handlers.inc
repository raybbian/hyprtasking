#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>

#include <array>
#include <cstdlib>
#include <functional>
#include <hyprutils/animation/AnimatedVariable.hpp>
#include <iostream>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

using WORKSPACEID = long;
constexpr WORKSPACEID WORKSPACE_INVALID = -1;
constexpr unsigned WL_KEYBOARD_KEY_STATE_RELEASED = 0;
constexpr unsigned WL_KEYBOARD_KEY_STATE_PRESSED = 1;
constexpr unsigned HL_MODIFIER_SHIFT = 1 << 0;
constexpr unsigned HL_MODIFIER_CTRL = 1 << 2;
constexpr unsigned HL_MODIFIER_ALT = 1 << 3;
constexpr unsigned HL_MODIFIER_META = 1 << 6;

namespace Config {
using INTEGER = int;
}

namespace HTConfig {
template<class T>
T value(const char*) {
    return 1;
}
} // namespace HTConfig

struct IKeyboard {
    struct SKeyEvent {
        unsigned keycode;
        unsigned state;
    };

    xkb_state* m_xkbState = nullptr;

    bool getPressed(unsigned) const {
        return true;
    }
};

struct InputManager {
    std::vector<std::shared_ptr<IKeyboard>> m_keyboards;
    unsigned modifiers = 0;

    unsigned getModsFromAllKBs() const {
        return modifiers;
    }
} input_manager;

auto* g_pInputManager = &input_manager;

WORKSPACEID selected_workspace = WORKSPACE_INVALID;

struct Monitor {
    int m_id = 0;
    std::shared_ptr<int> m_activeWorkspace = std::make_shared<int>(1);

    void changeWorkspace(std::shared_ptr<int> workspace) {
        selected_workspace = *workspace;
    }

    void scheduleFrame() {}
};

using PHLMONITOR = std::shared_ptr<Monitor>;
using PHLWORKSPACE = std::shared_ptr<int>;

namespace State {
struct WorkspaceQuery {
    WORKSPACEID selected;

    WorkspaceQuery& id(WORKSPACEID id) {
        selected = id;
        return *this;
    }

    PHLWORKSPACE run() {
        return std::make_shared<int>(selected);
    }
};

struct WorkspaceState {
    WorkspaceQuery query() {
        return {};
    }

    PHLWORKSPACE create(WORKSPACEID id, int) {
        return std::make_shared<int>(id);
    }
} workspaces;

auto* workspaceState() {
    return &workspaces;
}
} // namespace State

namespace Config::Actions {
WORKSPACEID& selected = selected_workspace;

std::optional<int> changeWorkspace(PHLWORKSPACE workspace) {
    selected = *workspace;
    return 0;
}
} // namespace Config::Actions

namespace Log {
constexpr int WARN = 1;

struct Logger {
    template<class... T>
    void log(T...) {}
} log_instance;

auto* logger = &log_instance;
} // namespace Log

struct Renderer {
    void damageMonitor(PHLMONITOR) {}
} renderer;

auto* g_pHyprRenderer = &renderer;

namespace Pointer::Cursor {
constexpr int CURSOR_OVERRIDE_UNKNOWN = 0;

struct Controller {
    void setOverride(const char*, int) {}

    void unsetOverride(int) {}
} controller;

auto* overrideController = &controller;
} // namespace Pointer::Cursor

struct AnimationContext {};

struct Layout {
    Hyprutils::Animation::CGenericAnimatedVariable<float, AnimationContext> offset;
    std::unordered_set<WORKSPACEID> targets = {1, 2, 10, 21, 36};

    void init_position() {
        offset.setValueAndWarp(1.f);
    }

    int hides = 0;

    void on_hide(Hyprutils::Animation::CBaseAnimatedVariable::CallbackFun callback) {
        hides++;
        offset = 1.f;
        offset.setCallbackOnEnd(callback);
    }

    void on_show() {
        offset = 0.f;
    }

    std::optional<WORKSPACEID> jump_target(size_t index) const {
        const auto id = static_cast<WORKSPACEID>(index) + 1;
        return targets.contains(id) ? std::optional(id) : std::nullopt;
    }
};

struct HTView {
    bool active = false, closing = false, navigating = false;
    std::shared_ptr<Layout> layout = std::make_shared<Layout>();
    PHLMONITOR monitor = std::make_shared<Monitor>();

    PHLMONITOR get_monitor() {
        return monitor;
    }

    void show(bool recalculate = true);

    void do_exit_behavior(bool) {}

    void hide(bool exit_on_mouse, std::optional<WORKSPACEID> target_workspace = std::nullopt);
};

using PHTVIEW = std::shared_ptr<HTView>;

struct HTManager {
    std::vector<PHTVIEW> views;
    std::unordered_set<unsigned> jump_pressed_keys;

    PHTVIEW get_view_from_cursor() {
        return views.front();
    }

    bool on_key(IKeyboard::SKeyEvent event);
};

static void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

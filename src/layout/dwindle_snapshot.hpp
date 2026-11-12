#pragma once

#include <optional>
#include <vector>

#include <hyprland/src/desktop/DesktopTypes.hpp>

namespace HTDwindle {
    struct Split {
        PHLWINDOW window;
        size_t first = 0;
        size_t second = 0;
        float ratio = 1.f;
        bool vertical = false;
    };

    // Postorder. Every branch refers to two earlier entries and the root is last.
    using Tree = std::vector<Split>;

    std::optional<Tree> capture(const PHLWORKSPACE& workspace);
    bool restore(const Tree& tree, const PHLWORKSPACE& workspace);
}

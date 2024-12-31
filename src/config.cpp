#include "config.hpp"

#include <hyprutils/math/Vector2D.hpp>

#include "globals.hpp"

int HTConfig::bg_color() {
    static long* const* PBG_COLOR =
        (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprtasking:bg_color")
            ->getDataStaticPtr();
    return **PBG_COLOR;
}

int HTConfig::gap_size() {
    static long* const* PGAP_SIZE =
        (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprtasking:gap_size")
            ->getDataStaticPtr();
    return **PGAP_SIZE;
}

int HTConfig::rows() {
    static long* const* PROWS =
        (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprtasking:rows")
            ->getDataStaticPtr();
    return **PROWS;
}

std::string HTConfig::exit_behavior() {
    static auto const* PEXIT_BEHAVIOR =
        (Hyprlang::STRING const*)
            HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprtasking:exit_behavior")
                ->getDataStaticPtr();
    return *PEXIT_BEHAVIOR;
}

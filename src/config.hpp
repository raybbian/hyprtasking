#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <string>
#include <unordered_map>

#include "globals.hpp"

namespace HTConfig {

inline Hyprlang::INT value(std::string config) {
    static std::unordered_map<std::string, Hyprlang::INT* const*> cache;

    if (!cache.count(config))
        cache[config] = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(
                            PHANDLE,
                            "plugin:hyprtasking:" + config
                        )->getDataStaticPtr();
    return **cache[config];
}

inline Hyprlang::FLOAT value_float(std::string config) {
    static std::unordered_map<std::string, Hyprlang::FLOAT* const*> cache;

    if (!cache.count(config))
        cache[config] = (Hyprlang::FLOAT* const*)HyprlandAPI::getConfigValue(
                            PHANDLE,
                            "plugin:hyprtasking:" + config
                        )->getDataStaticPtr();
    return **cache[config];
}

inline Hyprlang::STRING value_string(std::string config) {
    static std::unordered_map<std::string, Hyprlang::STRING const*> cache;

    if (!cache.count(config))
        cache[config] = (Hyprlang::STRING const*)HyprlandAPI::getConfigValue(
                            PHANDLE,
                            "plugin:hyprtasking:" + config
                        )->getDataStaticPtr();
    return *cache[config];
}

} // namespace HTConfig

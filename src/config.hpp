#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprutils/animation/AnimationConfig.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include <algorithm>

#include "globals.hpp"

using namespace Config::Values;

namespace HTConfig {

template<typename T>
inline T value(std::string config) {
    static std::unordered_map<std::string, CConfigValue<T>> cache;

    if (!cache.count(config)) {
        const CConfigValue<T> val("plugin:hyprtasking:" + config);
        cache[config] = val;
    }

    return *cache[config];
}

inline SP<Hyprutils::Animation::SAnimationPropertyConfig> gridAnimationConfig() {
    if (value<Config::INTEGER>("grid:animation:enabled") < 0)
        return Config::animationTree()->getAnimationPropertyConfig("workspaces");

    static auto config = makeShared<Hyprutils::Animation::SAnimationPropertyConfig>();
    config->overridden = true;
    config->internalEnabled = value<Config::INTEGER>("grid:animation:enabled");
    config->internalSpeed = std::max(0.1f, value<Config::FLOAT>("grid:animation:speed"));
    config->internalBezier = value<Config::STRING>("grid:animation:bezier");
    config->internalStyle = "";
    config->pValues = config;
    return config;
}

} // namespace HTConfig

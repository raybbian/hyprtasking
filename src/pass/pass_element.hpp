#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>

class HTPassElement: public IPassElement {
  public:
    HTPassElement();
    virtual ~HTPassElement() = default;

    virtual std::vector<UP<IPassElement>> draw() override;
    virtual bool needsLiveBlur() override;
    virtual bool needsPrecomputeBlur() override;
    virtual bool disableSimplification() override;
    virtual ePassElementType type() override {
        return EK_CUSTOM;
    }

    virtual const char* passName() override {
        return "HTDisableSimplification";
    }
};

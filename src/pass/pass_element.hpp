#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>

class HTPassElement: public IPassElement {
  public:
    HTPassElement();
    virtual ~HTPassElement() = default;

    virtual std::vector<UP<IPassElement>> draw(Render::CRenderContext& ctx) override;
    virtual bool needsLiveBlur(Render::CRenderContext& ctx) override;
    virtual bool needsPrecomputeBlur(Render::CRenderContext& ctx) override;
    virtual bool disableSimplification(Render::CRenderContext& ctx) override;
    virtual ePassElementType type() override {
        return EK_CUSTOM;
    }

    virtual const char* passName() override {
        return "HTDisableSimplification";
    }
};

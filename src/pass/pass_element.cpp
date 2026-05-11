#include "pass_element.hpp"

HTPassElement::HTPassElement() {
    ;
}

void HTPassElement::draw(const CRegion& damage) {
    ;
}

ePassElementType HTPassElement::type() {
    return EK_FRAMEBUFFER;
}

bool HTPassElement::needsLiveBlur() {
    return false;
}

bool HTPassElement::needsPrecomputeBlur() {
    return true;
}

bool HTPassElement::disableSimplification() {
    return true;
}

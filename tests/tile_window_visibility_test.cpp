#include "../src/tile_window_visibility.hpp"

int main() {
    struct Case {
        bool tile;
        bool pinned;
        bool special;
        bool matching;
        bool expected;
    };

    constexpr Case cases[] = {
        {false, false, false, false, true},
        {false, false, false, true, true},
        {true, false, false, true, true},
        {true, false, false, false, false},
        {true, true, false, false, true},
        {true, false, true, false, true},
        {true, true, true, false, true},
        {true, true, false, true, true},
        {true, false, true, true, true},
    };
    for (const Case& c : cases)
        if (hyprtasking_tile_accepts_window(c.tile, c.pinned, c.special, c.matching) != c.expected)
            return 1;
    return 0;
}

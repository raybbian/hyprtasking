int main() {
    HTManager manager;
    auto view = std::make_shared<HTView>();
    auto other = std::make_shared<HTView>();
    manager.views = {view, other};

    view->show();
    other->show();
    check(manager.on_key({KEY_2, WL_KEYBOARD_KEY_STATE_PRESSED}), "plain digit must jump");
    check(
        Config::Actions::selected == 2 && view->closing && view->layout->hides == 1
            && other->closing && other->layout->hides == 1,
        "jump must close all active views"
    );
    view->show();
    check(view->active && !view->closing, "reopening during close must stay active");
    check(
        manager.on_key({KEY_2, WL_KEYBOARD_KEY_STATE_RELEASED}),
        "handled release must be consumed"
    );
    check(!manager.on_key({KEY_2, WL_KEYBOARD_KEY_STATE_RELEASED}), "release consumed only once");

    for (unsigned modifiers : {HL_MODIFIER_META, HL_MODIFIER_CTRL, HL_MODIFIER_ALT}) {
        input_manager.modifiers = modifiers;
        int hides = view->layout->hides;
        check(
            !manager.on_key({KEY_K, WL_KEYBOARD_KEY_STATE_PRESSED}),
            "modified letter must reach keybinds"
        );
        check(view->layout->hides == hides, "modified letter must not select workspace 21");
        check(
            !manager.on_key({KEY_K, WL_KEYBOARD_KEY_STATE_RELEASED}),
            "unhandled shortcut release must pass through"
        );
    }
    input_manager.modifiers = 0;
    check(
        manager.on_key({KEY_K, WL_KEYBOARD_KEY_STATE_PRESSED}),
        "plain k must select workspace 21"
    );
    check(Config::Actions::selected == 21, "plain letter maps to correct workspace");
    input_manager.modifiers = HL_MODIFIER_META;
    view->active = false;
    check(
        manager.on_key({KEY_K, WL_KEYBOARD_KEY_STATE_RELEASED}),
        "release remains paired after close or modifier changes"
    );
    input_manager.modifiers = 0;
    check(
        !manager.on_key({KEY_1, WL_KEYBOARD_KEY_STATE_PRESSED}),
        "inactive overview must pass keys through"
    );

    view->show();
    check(manager.on_key({KEY_0, WL_KEYBOARD_KEY_STATE_PRESSED}), "zero must select workspace 10");
    check(Config::Actions::selected == 10, "zero maps to workspace 10");
    view->layout->offset.warp();
    check(!view->active && !view->closing, "completed close must clear state");
    view->show();
    check(view->active && !view->closing, "reopen after completed close must stay active");
    check(
        !manager.on_key({KEY_3, WL_KEYBOARD_KEY_STATE_PRESSED}),
        "unavailable target must pass through"
    );

    auto context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    check(context != nullptr, "XKB context must initialize");
    xkb_rule_names names {};
    names.layout = "de";
    auto keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    check(keymap != nullptr, "German XKB keymap must initialize");
    auto state = xkb_state_new(keymap);
    check(state != nullptr, "German XKB state must initialize");
    auto keyboard = std::make_shared<IKeyboard>();
    keyboard->m_xkbState = state;
    input_manager.m_keyboards = {keyboard};
    view->show();
    check(
        !manager.on_key({KEY_Z, WL_KEYBOARD_KEY_STATE_PRESSED}),
        "German z position produces y, unavailable workspace 35"
    );
    check(manager.on_key({KEY_Y, WL_KEYBOARD_KEY_STATE_PRESSED}), "German y position produces z");
    check(Config::Actions::selected == 36, "XKB mapping must override physical fallback");
    xkb_state_unref(state);
    xkb_keymap_unref(keymap);

    names.layout = "fr";
    keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    check(keymap != nullptr, "French XKB keymap must initialize");
    state = xkb_state_new(keymap);
    check(state != nullptr, "French XKB state must initialize");
    keyboard->m_xkbState = state;
    view->show();
    check(
        !manager.on_key({KEY_1, WL_KEYBOARD_KEY_STATE_PRESSED}),
        "French unshifted digit position is not a jump label"
    );
    xkb_state_update_mask(
        state,
        1 << xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT),
        0,
        0,
        0,
        0,
        0
    );
    input_manager.modifiers = HL_MODIFIER_SHIFT;
    check(
        manager.on_key({KEY_1, WL_KEYBOARD_KEY_STATE_PRESSED}),
        "Shift must allow typing French numeric jump labels"
    );
    check(Config::Actions::selected == 1, "French shifted digit must select workspace 1");
    xkb_state_unref(state);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    std::cout << "overview reopen, shortcuts, paired releases and XKB layouts: passed\n";
}

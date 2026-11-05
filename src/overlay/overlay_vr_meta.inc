/* The VR settings' labels and hints, included inside the k_meta[] initialiser of overlay_ui.cpp, in xbox_settings_vr_ids.inc's order. */
    /* XS_VR_IPD */ { XS_VR_IPD, "Eye separation", "The distance between the two eyes' cameras, in millimetres. Set it to your own (the headset's IPD setting shows it): 64 is the average.", 1, FMT_MM, NULL, 0 },
    /* XS_VR_NEAR */ { XS_VR_NEAR, "Near plane", "How close to the eyes things stop being drawn, in millimetres. Smaller shows the dashboard and wheel arms up close; too small and the picture's depth flickers.", 10, FMT_MM, NULL, 0 },
    /* XS_VR_FOV */ { XS_VR_FOV, "Headset field of view", "On: the picture uses the headset's own field of view. Off: the game's (about 106 by 74 degrees), with black around it.", 1, FMT_PLAIN, NULL, 0 },
    /* XS_VR_MENU_ASPECT */ { XS_VR_MENU_ASPECT, "Menu screen shape", "The shape of the floating screen the menus are shown on in the headset: width over height. 1.42 suits the game's 3:2 menus.", 0.05, FMT_PLAIN, NULL, 0 },
    /* XS_VR_HIDE_DRIVER */ { XS_VR_HIDE_DRIVER, "Hide the driver", "Hides the driver's figure, which can sit in front of the eyes in the seat view.", 1, FMT_PLAIN, NULL, 0 },
    /* XS_VR_BOOSTFX */ { XS_VR_BOOSTFX, "Boost effect", "The speed streaks and blur while boosting. Off if they are uncomfortable in the headset.", 1, FMT_PLAIN, NULL, 0 },
    /* XS_VR_KEEP_FX */ { XS_VR_KEEP_FX, "Bloom and motion blur", "On keeps the game's bloom and motion blur in the headset picture (it may come out brighter). Off, the default, turns them off for it.", 1, FMT_PLAIN, NULL, 0 },

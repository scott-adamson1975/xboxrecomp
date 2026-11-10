/* The racing wheel's bindings (the Wheel page's "press now" rows), included inside the s_text[] initialiser of xbox_settings.c: each
 * the text of a RECOMP_WHEEL_* variable, "VID:PID:N" (input/wheel_pad.h), set into it at start like the other text settings. A
 * variable set in a launcher still wins and locks the row. Their order is overlay_wheel.inc's (wheel_bind_info). */
    { "wheel.bind_steer",     RENV_INPUT_WHEEL_STEER,        "", 0, 0, 0 },
    { "wheel.bind_throttle",  RENV_INPUT_WHEEL_THROTTLE,     "", 0, 0, 0 },
    { "wheel.bind_brake",     RENV_INPUT_WHEEL_BRAKE,        "", 0, 0, 0 },
    { "wheel.bind_hat",       RENV_INPUT_WHEEL_HAT,          "", 0, 0, 0 },
    { "wheel.bind_accept",    RENV_INPUT_WHEEL_BTN_ACCEPT,   "", 0, 0, 0 },
    { "wheel.bind_back",      RENV_INPUT_WHEEL_BTN_BACK,     "", 0, 0, 0 },
    { "wheel.bind_pause",     RENV_INPUT_WHEEL_BTN_PAUSE,    "", 0, 0, 0 },
    { "wheel.bind_lookback",  RENV_INPUT_WHEEL_BTN_LOOKBACK, "", 0, 0, 0 },
    { "wheel.bind_view",      RENV_INPUT_WHEEL_BTN_VIEW,     "", 0, 0, 0 },
    { "wheel.bind_recentre",  RENV_INPUT_WHEEL_BTN_RECENTRE, "", 0, 0, 0 },

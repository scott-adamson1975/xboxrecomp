/* The SimHub telemetry settings' rows, included inside the s_set[] initialiser of xbox_settings.c (see xbox_settings_simhub_ids.inc). */
    [XS_SIMHUB_ENABLE] = SETTING("simhub.enable", XS_BOOL, XS_RESTART, RENV_SIMHUB, NULL, NULL, 0, 0, 0),
    [XS_SIMHUB_PORT] = SETTING("simhub.port", XS_INT, XS_RESTART, RENV_SIMHUB_PORT, NULL, NULL, 1024, 65535, 8000),
    [XS_SIMHUB_RPM] = SETTING("simhub.made_up_rpm", XS_BOOL, XS_RESTART, RENV_SIMHUB_RPM, NULL, NULL, 0, 1, 1),
    [XS_SIMHUB_GRAVITY] = SETTING("simhub.gravity", XS_BOOL, XS_RESTART, RENV_SIMHUB_GRAVITY, NULL, NULL, 0, 0, 0),
    [XS_SIMHUB_FLIP_PITCH] = SETTING("simhub.flip_pitch", XS_BOOL, XS_RESTART, RENV_SIMHUB_FLIP_PITCH, NULL, NULL, 0, 0, 0),
    [XS_SIMHUB_FLIP_ROLL] = SETTING("simhub.flip_roll", XS_BOOL, XS_RESTART, RENV_SIMHUB_FLIP_ROLL, NULL, NULL, 0, 0, 0),
    [XS_SIMHUB_FLIP_YAW] = SETTING("simhub.flip_yaw", XS_BOOL, XS_RESTART, RENV_SIMHUB_FLIP_YAW, NULL, NULL, 0, 0, 0),

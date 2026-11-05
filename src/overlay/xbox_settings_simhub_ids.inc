/* The SimHub telemetry settings' ids (the SimHub page), included inside the xs_id enum of xbox_settings.h after the wheel's. Own files, like the wheel's: rows in xbox_settings_simhub_set.inc, labels in overlay_simhub_meta.inc, the page in overlay_simhub.inc. The game reads them at start through the RECOMP_SIMHUB_* variables (src/game/simhub_udp.c). */
    XS_SIMHUB_ENABLE,       /* the SimHub page: the car's motion sent over UDP, in Forza Motorsport 7's Data Out format */
    XS_SIMHUB_PORT,
    XS_SIMHUB_RPM,
    XS_SIMHUB_GRAVITY,
    XS_SIMHUB_FLIP_PITCH,
    XS_SIMHUB_FLIP_ROLL,
    XS_SIMHUB_FLIP_YAW,

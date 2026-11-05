/* overlay_simhub.inc -- included in overlay_ui.cpp after overlay_wheel.inc: the SimHub page, the car's motion sent to SimHub over
 * UDP (the settings are in xbox_settings_simhub_*.inc, the sender is the game's: src/game/simhub_udp.c). */
static int simhub_rows(int page, Row *r)
{
    int n = 0;
    static char target[160];
#define SINFO(t) r[n++] = Row{ R_INFO, XS_COUNT, 0, (t) }
#define SSET(i)  r[n++] = Row{ R_SETTING, (i), 0, NULL }

    (void)page;
    SINFO("Sends the car's motion to SimHub over UDP, in Forza Motorsport 7's format: in SimHub pick that game and its port.");
    SINFO("Changes apply after a restart. The game has no rpm or gear, so those are made up from the speed.");
    SSET(XS_SIMHUB_ENABLE);
    if ((int)xbox_SettingValue(XS_SIMHUB_ENABLE)) {
        const char *host = xbox_SettingsText("simhub.host", NULL);

        snprintf(target, sizeof target, "Sending to %s:%d (the address is simhub.host in settings.toml: another PC on the network, say).",
                 host && host[0] ? host : "127.0.0.1", (int)xbox_SettingValue(XS_SIMHUB_PORT));
        SINFO(target);
        SSET(XS_SIMHUB_PORT);
        SSET(XS_SIMHUB_RPM);
        SINFO("If SimHub's motion preview moves the wrong way, turn these round:");
        SSET(XS_SIMHUB_GRAVITY);
        SSET(XS_SIMHUB_FLIP_PITCH); SSET(XS_SIMHUB_FLIP_ROLL); SSET(XS_SIMHUB_FLIP_YAW);
    }
    r[n++] = Row{ R_ACTION, XS_COUNT, A_RESET_PAGE, "Reset this page's settings to the defaults" };
    return n;
#undef SSET
#undef SINFO
}

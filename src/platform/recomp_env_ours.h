/* recomp_env_ours.h -- the fork's own environment keys (the racing wheel, SimHub), in the toolkit's row format. They are
 * included by recomp_env.h and expanded where RECOMP_ENV_OURS_KEYS(X) stands in RECOMP_ENV_KEYS, so that the toolkit's own
 * table has only those two lines of ours and not a row for each key (fewer merge conflicts with upstream). */
#ifndef RECOMP_ENV_OURS_H
#define RECOMP_ENV_OURS_H

#define RECOMP_ENV_OURS_KEYS(X) \
    X(INPUT_WHEEL_ENABLE, CONFIG, "RECOMP_WHEEL_ENABLE",     NULL, "0: the racing wheel is not used even if it is set up (default on once a steering axis is bound)") \
    X(INPUT_WHEEL_STEER, CONFIG, "RECOMP_WHEEL_STEER",       NULL, "racing wheel, steering axis: VID:PID:AXIS of a joystick (hex ids, SDL axis index; e.g. 044F:B696:0). Setting any of the three RECOMP_WHEEL_ axes turns the wheel on (the SDL host)") \
    X(INPUT_WHEEL_THROTTLE, CONFIG, "RECOMP_WHEEL_THROTTLE", NULL, "racing wheel, throttle pedal axis, VID:PID:AXIS (a trailing ! for a pedal that reads high at rest); may be another device than the steering") \
    X(INPUT_WHEEL_BRAKE, CONFIG, "RECOMP_WHEEL_BRAKE",       NULL, "racing wheel, brake pedal axis, VID:PID:AXIS (a trailing ! for a pedal that reads high at rest)") \
    X(INPUT_WHEEL_RANGE, CONFIG, "RECOMP_WHEEL_RANGE",       NULL, "racing wheel: degrees the base turns lock to lock (default 900)") \
    X(INPUT_WHEEL_LOCK,  CONFIG, "RECOMP_WHEEL_LOCK",        NULL, "racing wheel: degrees lock to lock the game gets, up to the range (default 540): a smaller lock reaches full steering sooner") \
    X(INPUT_WHEEL_DEADZONE, CONFIG, "RECOMP_WHEEL_DEADZONE", NULL, "racing wheel: pedal deadzone, percent of travel (default 4)") \
    X(INPUT_WHEEL_LOG,   CONFIG, "RECOMP_WHEEL_LOG",         NULL, "1: print the wheel devices opened and, once a second while a control moves, the raw axes (to find the axis indexes)") \
    X(INPUT_WHEEL_FFB_SPRING, CONFIG, "RECOMP_WHEEL_FFB_SPRING", NULL, "racing wheel force feedback: a centring spring the base runs itself. The value is the force at the software lock's edge (RECOMP_WHEEL_LOCK) as a percent of the base's maximum, 0 (default) off, never above 50; beyond the edge the force holds, it does not grow. Faded in over 1.5 s, and off at once while the overlay is open, the window is not in front or the game ends. Needs RECOMP_WHEEL_STEER") \
    X(INPUT_WHEEL_STEER_DEAD, CONFIG, "RECOMP_WHEEL_STEER_DEAD", NULL, "racing wheel steering deadzone, percent of the stick's travel after the lock (default 0): the wheel inside it reads centred. A base that rests a little off centre (a T818 reads about 4% here at a 360 degree lock) needs a few percent when RECOMP_WHEEL_STEER_SKIP is used") \
    X(INPUT_WHEEL_STEER_SKIP, CONFIG, "RECOMP_WHEEL_STEER_SKIP", NULL, "racing wheel steering: the percent of the stick's travel around its centre that the game ignores (default 0, off); the wheel jumps over it so the first degrees already steer. A 20 degree dead spot at a 360 degree lock is about 11") \
    X(INPUT_WHEEL_FFB_SPEED, CONFIG, "RECOMP_WHEEL_FFB_SPEED", NULL, "racing wheel spring (needs RECOMP_WHEEL_FFB_SPRING and a host that feeds the car): the speed in m/s at which the spring has its whole strength; slower, it is weaker, down to RECOMP_WHEEL_FFB_REST of it at a standstill. 0 (default) off: the strength does not follow the speed. 40 m/s is about 145 km/h") \
    X(INPUT_WHEEL_FFB_REST, CONFIG, "RECOMP_WHEEL_FFB_REST", NULL, "racing wheel spring: the percent of its strength left at a standstill when RECOMP_WHEEL_FFB_SPEED is set (default 30)") \
    X(INPUT_WHEEL_FFB_RACE_ONLY, CONFIG, "RECOMP_WHEEL_FFB_RACE_ONLY", NULL, "1: the racing wheel spring is off outside a race (menus, pauses, results), faded out and back in. Needs RECOMP_WHEEL_FFB_SPRING and a host that feeds the car. Default off: the spring is the same everywhere") \
    X(INPUT_WHEEL_RUMBLE, CONFIG, "RECOMP_WHEEL_RUMBLE", NULL, "racing wheel: the title's rumble (boost, crashes, bumps) as a 25 Hz vibration on the wheel, this percent of the base's force at the title's full rumble (needs RECOMP_WHEEL_FFB_SPRING; default 0, off)") \
    X(INPUT_WHEEL_ENGINE, CONFIG, "RECOMP_WHEEL_ENGINE", NULL, "racing wheel: a light engine vibration while racing, a little at a standstill and more with speed, this percent of the base's force at most (needs RECOMP_WHEEL_FFB_SPRING; default 0, off)") \
    X(INPUT_WHEEL_KICK, CONFIG, "RECOMP_WHEEL_KICK", NULL, "racing wheel: when the car is hit, a weight on the wheel toward the side it is hit from (a constant force for about half a second), this percent of the base's force at the hardest hit (needs RECOMP_WHEEL_FFB_SPRING and a host that reports hits; default 0, off)") \
    X(INPUT_WHEEL_KICK_FLIP, CONFIG, "RECOMP_WHEEL_KICK_FLIP", NULL, "1: the hit's weight the other way round, if it comes out on the wrong side") \
    X(INPUT_WHEEL_STEER_SENS, CONFIG, "RECOMP_WHEEL_STEER_SENS", NULL, "racing wheel: steering sensitivity, percent (default 100): how much of the wheel's travel the stick gets, on top of the lock; 200 reaches full deflection at half the turn") \
    X(INPUT_WHEEL_STEER_LIN, CONFIG, "RECOMP_WHEEL_STEER_LIN", NULL, "racing wheel: steering linearity, -100..100 (default 0, linear): above 0 the stick moves less around the centre and more near the ends, below 0 more around the centre") \
    X(INPUT_WHEEL_THROTTLE_SENS, CONFIG, "RECOMP_WHEEL_THROTTLE_SENS", NULL, "racing wheel: throttle pedal sensitivity, percent (default 100): 200 reaches full throttle at half the travel") \
    X(INPUT_WHEEL_THROTTLE_LIN, CONFIG, "RECOMP_WHEEL_THROTTLE_LIN", NULL, "racing wheel: throttle pedal linearity, -100..100 (default 0): above 0 gentler at first, below 0 sharper at first") \
    X(INPUT_WHEEL_BRAKE_SENS, CONFIG, "RECOMP_WHEEL_BRAKE_SENS", NULL, "racing wheel: brake pedal sensitivity, percent (default 100)") \
    X(INPUT_WHEEL_BRAKE_LIN, CONFIG, "RECOMP_WHEEL_BRAKE_LIN", NULL, "racing wheel: brake pedal linearity, -100..100 (default 0)") \
    X(INPUT_WHEEL_FFB_AIR, CONFIG, "RECOMP_WHEEL_FFB_AIR", NULL, "racing wheel (the Burnout 3 host): the percent of the spring left while the car is in the air (default 0)") \
    X(INPUT_WHEEL_FFB_DRIFT, CONFIG, "RECOMP_WHEEL_FFB_DRIFT", NULL, "racing wheel (the Burnout 3 host): the percent of the spring left in a full drift, so that the wheel goes light as the front slips (default 15; 100 leaves the spring alone)") \
    X(INPUT_WHEEL_BTN_ACCEPT, CONFIG, "RECOMP_WHEEL_BTN_ACCEPT", NULL, "racing wheel, accept button, VID:PID:N (N: the joystick's button index, from 0): the game's A (confirms in menus; boost in a race; impact time in a crash)") \
    X(INPUT_WHEEL_BTN_BACK, CONFIG, "RECOMP_WHEEL_BTN_BACK", NULL, "racing wheel, back button, VID:PID:N: the game's B (back in menus; the crashbreaker)") \
    X(INPUT_WHEEL_BTN_PAUSE, CONFIG, "RECOMP_WHEEL_BTN_PAUSE", NULL, "racing wheel, pause button, VID:PID:N: the game's Start") \
    X(INPUT_WHEEL_BTN_LOOKBACK, CONFIG, "RECOMP_WHEEL_BTN_LOOKBACK", NULL, "racing wheel, look back button, VID:PID:N: the game's X") \
    X(INPUT_WHEEL_BTN_VIEW, CONFIG, "RECOMP_WHEEL_BTN_VIEW", NULL, "racing wheel, change view button, VID:PID:N: the game's Y") \
    X(INPUT_WHEEL_BTN_RECENTRE, CONFIG, "RECOMP_WHEEL_BTN_RECENTRE", NULL, "racing wheel, recentre button, VID:PID:N: the game's right stick pressed in (R3; the VR mode recentres the headset's view with it, and holding it stores the seat)") \
    X(INPUT_WHEEL_HAT, CONFIG, "RECOMP_WHEEL_HAT", NULL, "racing wheel, hat switch, VID:PID:N (N: the hat index, usually 0): the D-pad, and the left stick (a crash's aftertouch is steered with it; left and right only while the wheel is within about a quarter of full lock)") \
    X(SIMHUB,            CONFIG, "RECOMP_SIMHUB",            NULL, "1: send the player's car to SimHub over UDP, in Forza Motorsport 7's Data Out format (pick that game in SimHub). The Settings overlay's SimHub page") \
    X(SIMHUB_PORT,       CONFIG, "RECOMP_SIMHUB_PORT",       NULL, "the UDP port the SimHub telemetry goes to (default 8000: the port Forza Motorsport 7 uses)") \
    X(SIMHUB_HOST,       CONFIG, "RECOMP_SIMHUB_HOST",       NULL, "the IPv4 address the SimHub telemetry goes to (default 127.0.0.1: this computer)") \
    X(SIMHUB_RPM,        CONFIG, "RECOMP_SIMHUB_RPM",        NULL, "0: send rpm 0 and gear 1 to SimHub instead of values made up from the speed (the game has neither)") \
    X(SIMHUB_GRAVITY,    CONFIG, "RECOMP_SIMHUB_GRAVITY",    NULL, "1: add 9.81 m/s2 to the vertical acceleration sent to SimHub, if it expects gravity in it") \
    X(SIMHUB_FLIP_PITCH, CONFIG, "RECOMP_SIMHUB_FLIP_PITCH", NULL, "1: the pitch angle sent to SimHub the other way round") \
    X(SIMHUB_FLIP_ROLL,  CONFIG, "RECOMP_SIMHUB_FLIP_ROLL",  NULL, "1: the roll angle sent to SimHub the other way round") \
    X(SIMHUB_FLIP_YAW,   CONFIG, "RECOMP_SIMHUB_FLIP_YAW",   NULL, "1: the yaw angle sent to SimHub the other way round") \
    X(SIMHUB_LOG,        CONFIG, "RECOMP_SIMHUB_LOG",        NULL, "1: log a line a second while the SimHub telemetry is sent")

#endif

/*
 * Sky Circuit — native PS3 port.
 * Draws the road, car, ramps and bumps with tiny3d. Does not launch the browser.
 * Target: firmware 4.92, HEN 3.4.1 (NPDRM HDD game package).
 */
#include <io/pad.h>
#include <stdio.h>
#include <string.h>
#include <sysmodule/sysmodule.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <tiny3d.h>

#include "draw.h"
#include "game.h"
#include "log.h"

static Game game;
static unsigned prev_bits;
static volatile int g_exit;

static void sysutil_callback(uint64_t status, uint64_t param, void *userdata)
{
    (void)param;
    (void)userdata;
    switch (status) {
    case SYSUTIL_EXIT_GAME:
        log_msg("sysutil: EXIT_GAME");
        g_exit = 1;
        break;
    case SYSUTIL_MENU_OPEN:
        log_msg("sysutil: MENU_OPEN");
        break;
    case SYSUTIL_MENU_CLOSE:
        log_msg("sysutil: MENU_CLOSE");
        break;
    default:
        break;
    }
}

static float stick_x(unsigned int raw)
{
    float v = ((int)raw - 128) / 128.0f;
    if (v > -0.22f && v < 0.22f) return 0.0f;
    if (v < -1.0f) v = -1.0f;
    if (v > 1.0f) v = 1.0f;
    return v;
}

static void read_input(Input *in)
{
    padInfo info;
    padData data;
    int i;
    unsigned bits = 0;
    float steer = 0.0f;
    int got_stick = 0;

    memset(in, 0, sizeof(*in));
    ioPadGetInfo(&info);
    for (i = 0; i < MAX_PADS; i++) {
        if (!info.status[i]) continue;
        ioPadGetData(i, &data);
        if (data.BTN_LEFT) in->left = 1;
        if (data.BTN_RIGHT) in->right = 1;
        if (data.BTN_UP) bits |= 1u;
        if (data.BTN_DOWN) bits |= 2u;
        if (data.BTN_CROSS) bits |= 4u;
        if (data.BTN_START) bits |= 8u;
        if (data.BTN_TRIANGLE) bits |= 16u;
        if (data.BTN_CROSS || data.BTN_R2) in->gas = 1;
        if (data.BTN_SQUARE || data.BTN_L2) in->brake = 1;
        if (data.BTN_CIRCLE || data.BTN_L1) in->hbrake = 1;
        if (!got_stick) {
            steer = stick_x(data.ANA_L_H);
            got_stick = 1;
        }
    }
    if (in->left) steer = -1.0f;
    if (in->right) steer = 1.0f;
    in->steer = steer;
    in->press_up = (bits & 1u) && !(prev_bits & 1u);
    in->press_down = (bits & 2u) && !(prev_bits & 2u);
    in->press_confirm = (bits & 4u) && !(prev_bits & 4u);
    in->press_pause = (bits & 8u) && !(prev_bits & 8u);
    in->press_triangle = (bits & 16u) && !(prev_bits & 16u);
    prev_bits = bits;
}

static int load_modules(void)
{
    int r;
    r = sysModuleLoad(SYSMODULE_RSX_DEVICE);
    log_msg("sysModuleLoad RSX_DEVICE -> %d", r);
    r = sysModuleLoad(SYSMODULE_IO);
    log_msg("sysModuleLoad IO -> %d", r);
    r = sysModuleLoad(SYSMODULE_FS);
    log_msg("sysModuleLoad FS -> %d", r);
    return 0;
}

int main(int argc, char **argv)
{
    int frame;
    (void)argc;
    (void)argv;

    log_init();
    log_msg("main: enter");

    load_modules();

    if (sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_callback, NULL) != 0)
        log_msg("sysUtilRegisterCallback failed (continuing)");
    else
        log_msg("sysutil callback registered");

    log_msg("tiny3d_Init...");
    if (tiny3d_Init(1024 * 1024) < 0) {
        log_msg("FATAL: tiny3d_Init failed");
        log_flush();
        return 1;
    }
    log_msg("tiny3d_Init ok");

    log_msg("ioPadInit...");
    if (ioPadInit(7) != 0)
        log_msg("ioPadInit returned non-zero (continuing)");
    else
        log_msg("ioPadInit ok");

    log_msg("game_init...");
    game_init(&game);
    log_msg("game_init done state=%d level=%d", game.state, game.level);

    /* First frames: solid colour so a hang after init is visible vs pure black */
    for (frame = 0; frame < 3 && !g_exit; frame++) {
        tiny3d_Clear(0xff70c5ce, TINY3D_CLEAR_ALL);
        tiny3d_Flip();
        sysUtilCheckCallback();
        log_msg("warmup flip %d", frame);
    }

    frame = 0;
    while (!g_exit) {
        Input in;

        sysUtilCheckCallback();

        tiny3d_Clear(0xff70c5ce, TINY3D_CLEAR_ALL);
        tiny3d_AlphaTest(1, 0x10, TINY3D_ALPHA_FUNC_GEQUAL);
        tiny3d_BlendFunc(1,
            TINY3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA,
            TINY3D_BLEND_FUNC_DST_RGB_ONE_MINUS_SRC_ALPHA | TINY3D_BLEND_FUNC_DST_ALPHA_ZERO,
            TINY3D_BLEND_RGB_FUNC_ADD | TINY3D_BLEND_ALPHA_FUNC_ADD);

        read_input(&in);
        game_frame(&game, &in);
        draw_frame(&game);
        tiny3d_Flip();

        frame++;
        if ((frame % 300) == 0) {
            log_msg("alive frame=%d state=%d", frame, game.state);
            log_flush();
        }
    }

    log_msg("main: exit after %d frames", frame);
    log_flush();
    return 0;
}

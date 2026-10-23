/*
 * Sky Circuit — native PS3 port.
 * Draws the road, car, ramps and bumps with tiny3d. Does not launch the browser.
 * Target: firmware 4.92, HEN 3.4.1 (NPDRM HDD game package).
 */
#include <io/pad.h>
#include <string.h>
#include <tiny3d.h>

#include "draw.h"
#include "game.h"

/* Debug bisect: 1 = clear+flip only, 2 = + one red rect, 3 = full game */
#ifndef STAGE
#define STAGE 1
#endif

static Game game;
static unsigned prev_bits;

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

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    tiny3d_Init(1024 * 1024);
    ioPadInit(7);
#if STAGE >= 3
    game_init(&game);
#endif

    while (1) {
        Input in;
        tiny3d_Clear(0xff70c5ce, TINY3D_CLEAR_ALL);
#if STAGE >= 2
        tiny3d_AlphaTest(1, 0x10, TINY3D_ALPHA_FUNC_GEQUAL);
        tiny3d_BlendFunc(1,
            TINY3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA,
            TINY3D_BLEND_FUNC_DST_RGB_ONE_MINUS_SRC_ALPHA | TINY3D_BLEND_FUNC_DST_ALPHA_ZERO,
            TINY3D_BLEND_RGB_FUNC_ADD | TINY3D_BLEND_ALPHA_FUNC_ADD);
#endif
#if STAGE >= 3
        read_input(&in);
        game_frame(&game, &in);
        draw_frame(&game);
#elif STAGE == 2
        (void)in;
        tiny3d_Project2D();
        tiny3d_SetPolygon(TINY3D_QUADS);
        tiny3d_VertexPos(300, 150, 1000); tiny3d_VertexColor(0xff0000ff);
        tiny3d_VertexPos(550, 150, 1000); tiny3d_VertexColor(0xff0000ff);
        tiny3d_VertexPos(550, 350, 1000); tiny3d_VertexColor(0xff0000ff);
        tiny3d_VertexPos(300, 350, 1000); tiny3d_VertexColor(0xff0000ff);
        tiny3d_End();
#else
        (void)in;
#endif
        tiny3d_Flip();
    }
    return 0;
}

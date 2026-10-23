#include "game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define BASE_ROAD_WIDTH 55.0f
#define CAR_WIDTH 24.0f
#define CAR_LENGTH 72.0f
#define GAP_REQ_SPEED 4.8f
#define BOOST_MULT 2.0f
#define BOOST_MIN_CHARGE 0.2f
#define OVERSPEED_DECAY 0.98f
#define TIME_TO_EXPLOSION 5.0f

static float rnd(Game *g)
{
    g->rng = g->rng * 1664525u + 1013904223u;
    return (g->rng >> 8) * (1.0f / 16777216.0f);
}

float game_road_width(const Game *g)
{
    return g->half_road ? BASE_ROAD_WIDTH * 0.5f : BASE_ROAD_WIDTH;
}

static void set_status(Game *g, const char *text, u32 color)
{
    snprintf(g->status, sizeof(g->status), "%s", text);
    g->status_color = color;
}

static void reset_car(Game *g, float x, float y, float angle, float speed)
{
    Car *c = &g->car;
    c->x = x;
    c->y = y;
    c->angle = angle;
    c->speed = speed;
    c->airborne = 0;
    c->air_timer = 0;
    c->scale = 1.0f;
    c->vertical_offset = 0.0f;
    c->wheels = 4;
}

static void reset_run(Game *g)
{
    g->cp_timer = 0;
    g->cp_banner = 0;
    g->game_over = 0;
    g->paused = 0;
    g->stuck = 0;
    g->stuck_time = 3.0f;
    g->burnout_timer = 0.0f;
    g->burnout_rocking = 0;
    g->rock_wobble = 0.0f;
    g->exploded = 0;
    g->explosion_anim = 0.0f;
    g->post_explosion = 0.0f;
    g->nsmoke = 0;
    g->speed_is_text = 0;
    g->death_reason[0] = 0;
    set_status(g, "ROAD OK", 0x27ae60ff);
}

static void update_bounds(Game *g)
{
    float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f;
    int i;
    for (i = 0; i < g->nsegs; i++) {
        Seg *s = &g->segs[i];
        if (s->x1 < minx) minx = s->x1;
        if (s->x2 < minx) minx = s->x2;
        if (s->x1 > maxx) maxx = s->x1;
        if (s->x2 > maxx) maxx = s->x2;
        if (s->y1 < miny) miny = s->y1;
        if (s->y2 < miny) miny = s->y2;
        if (s->y1 > maxy) maxy = s->y1;
        if (s->y2 > maxy) maxy = s->y2;
    }
    g->bounds_min_x = minx - 100.0f;
    g->bounds_max_x = maxx + 100.0f;
    g->bounds_min_y = miny - 100.0f;
    g->bounds_max_y = maxy + 100.0f;
}

static void push_seg(Game *g, float x1, float y1, float x2, float y2, int ramp)
{
    Seg *s;
    if (g->nsegs >= MAX_SEGS) return;
    s = &g->segs[g->nsegs++];
    s->x1 = x1;
    s->y1 = y1;
    s->x2 = x2;
    s->y2 = y2;
    s->is_ramp = ramp;
}

/* PLACEHOLDER - full file will be updated */
void game_init(Game *g)
{
    memset(g, 0, sizeof(*g));
    g->rng = 1;
    g->state = ST_SELECT;
    g->level = 1;
    g->car.max_speed = 12.0f;
    g->car.accel = 0.18f;
    g->car.friction = 0.03f;
    g->car.turn_speed = 0.055f;
    set_status(g, "SELECT TRACK", 0x1a2530ff);
}

void game_frame(Game *g, const Input *in)
{
    (void)in;
    g->tick++;
}

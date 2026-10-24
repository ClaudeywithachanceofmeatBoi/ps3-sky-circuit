#ifndef SKY_GAME_H
#define SKY_GAME_H

#include <ppu-types.h>

#define VW 848.0f
#define VH 512.0f

#define MAX_SEGS  640
#define MAX_GAPS  16
#define MAX_BUMPS 8
#define MAX_CLOUDS 30
#define MAX_SMOKE 160

enum {
    ST_SELECT = 0,
    ST_PLAY,
    ST_PAUSE,
    ST_DEAD
};

typedef struct {
    float x1, y1, x2, y2;
    int is_ramp;
} Seg;

typedef struct {
    float x, y, radius, min_speed;
} Gap;

typedef struct {
    float x, y;
    int hit_cooldown;
} Bump;

typedef struct {
    float x, y, r;
} Cloud;

typedef struct {
    float x, y, r;
    int life;
} Smoke;

typedef struct {
    int left, right, gas, brake, hbrake;
    float steer;  /* -1 left .. +1 right (player 1: front wheels) */
    float steer2; /* player 2: rear wheels */
    int two_player, reset1, reset2;
    int press_up, press_down, press_confirm, press_pause, press_triangle;
} Input;

typedef struct {
    float x, y, angle, speed;
    float max_speed, accel, friction, turn_speed;
    int airborne, air_timer;
    float scale, vertical_offset;
    int wheels;
    float front_angle, rear_angle; /* wheel angles relative to the car body (rad) */
} Car;

typedef struct {
    int state;
    int level;
    int menu;
    int half_road;
    int game_over;
    int two_player;
    int paused;

    Car car;
    float last_x, last_y, last_angle, last_speed;

    Seg segs[MAX_SEGS];
    int nsegs;
    Gap gaps[MAX_GAPS];
    int ngaps;
    Bump bumps[MAX_BUMPS];
    int nbumps;
    Cloud clouds[MAX_CLOUDS];
    int nclouds;
    Smoke smoke[MAX_SMOKE];
    int nsmoke;

    float bounds_min_x, bounds_max_x, bounds_min_y, bounds_max_y;

    int cp_timer;
    int cp_banner;
    int stuck;
    float stuck_time;
    int danger;

    float burnout_timer;
    int burnout_rocking;
    float rock_wobble;
    int exploded;
    float explosion_anim;
    float post_explosion;

    unsigned tick;
    unsigned rng;
    char death_reason[64];
    char status[32];
    u32 status_color;
    int speed_kmh;
    int speed_is_text;
} Game;

void game_init(Game *g);
void game_frame(Game *g, const Input *in);

float game_road_width(const Game *g);

#endif

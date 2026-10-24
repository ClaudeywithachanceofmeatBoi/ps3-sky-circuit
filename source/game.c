#include "game.h"
#include "sound.h"

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
    c->front_angle = 0.0f;
    c->rear_angle = 0.0f;
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

static void add_wavy(Game *g, float x1, float y1, float x2, float y2, int waves)
{
    int steps = 30;
    int i;
    float dx = (x2 - x1) / (float)steps;
    float dy = (y2 - y1) / (float)steps;
    float nx = -dy, ny = dx;
    float len = sqrtf(nx * nx + ny * ny);
    float prevx, prevy;
    if (len > 0.0001f) {
        nx /= len;
        ny /= len;
    }
    prevx = x1;
    prevy = y1;
    for (i = 1; i <= steps; i++) {
        float progress = (float)i / (float)steps;
        float offset = sinf(progress * 3.14159265f * 2.0f * (float)waves) * 100.0f;
        float cx = x1 + dx * (float)i + nx * offset;
        float cy = y1 + dy * (float)i + ny * offset;
        push_seg(g, prevx, prevy, cx, cy, 0);
        prevx = cx;
        prevy = cy;
    }
}

static void add_arc(Game *g, float x1, float y1, float x2, float y2, float cx, float cy)
{
    int steps = 25;
    int i;
    float prevx = x1, prevy = y1;
    for (i = 1; i <= steps; i++) {
        float t = (float)i / (float)steps;
        float u = 1.0f - t;
        float px = u * u * x1 + 2.0f * u * t * cx + t * t * x2;
        float py = u * u * y1 + 2.0f * u * t * cy + t * t * y2;
        push_seg(g, prevx, prevy, px, py, 0);
        prevx = px;
        prevy = py;
    }
}

static void add_gap(Game *g, float x1, float y1, float x2, float y2, float gap, float min_speed)
{
    float mx = (x1 + x2) * 0.5f;
    float my = (y1 + y2) * 0.5f;
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    float ndx, ndy, hx, hy;
    if (len < 0.0001f) return;
    ndx = dx / len;
    ndy = dy / len;
    hx = ndx * (gap * 0.5f);
    hy = ndy * (gap * 0.5f);
    push_seg(g, x1, y1, mx - hx, my - hy, 1);
    push_seg(g, mx + hx, my + hy, x2, y2, 0);
    if (g->ngaps < MAX_GAPS) {
        Gap *j = &g->gaps[g->ngaps++];
        j->x = mx - hx;
        j->y = my - hy;
        j->radius = 45.0f;
        j->min_speed = min_speed;
    }
}

static void push_bump(Game *g, float x, float y)
{
    if (g->nbumps >= MAX_BUMPS) return;
    g->bumps[g->nbumps].x = x;
    g->bumps[g->nbumps].y = y;
    g->bumps[g->nbumps].hit_cooldown = 0;
    g->nbumps++;
}

static void build_level(Game *g, int lvl)
{
    typedef struct { float x, y; } Node;
    Node nodes[12];
    int n = 10;
    int i;

    g->nsegs = 0;
    g->ngaps = 0;
    g->nbumps = 0;
    g->level = lvl;

    if (lvl == 1) {
        Node a[] = {{0,0},{1000,0},{1000,800},{1800,800},{1800,1800},{800,1800},{800,2600},{-600,2600},{-600,1200},{0,1200}};
        memcpy(nodes, a, sizeof(a));
    } else if (lvl == 2) {
        Node a[] = {{0,0},{1200,0},{1200,1200},{400,1200},{400,2000},{1600,2000},{1600,3200},{-400,3200},{-400,1600},{0,1600}};
        memcpy(nodes, a, sizeof(a));
    } else {
        Node a[] = {{0,0},{1400,0},{1400,1000},{2400,1000},{2400,2200},{1200,2200},{1200,3200},{-800,3200},{-800,1400},{0,1400}};
        memcpy(nodes, a, sizeof(a));
        lvl = 3;
        g->level = 3;
    }

    for (i = 0; i < n; i++) {
        Node a = nodes[i];
        Node b = nodes[(i + 1) % n];
        if (lvl == 1) {
            if (i == 1) add_wavy(g, a.x, a.y, b.x, b.y, 3);
            else if (i == 3) add_gap(g, a.x, a.y, b.x, b.y, 100.0f, GAP_REQ_SPEED);
            else if (i == 6) add_arc(g, a.x, a.y, b.x, b.y, 0.0f, 2600.0f);
            else push_seg(g, a.x, a.y, b.x, b.y, 0);
        } else if (lvl == 2) {
            if (i == 0 || i == 4) add_gap(g, a.x, a.y, b.x, b.y, 100.0f, GAP_REQ_SPEED);
            else if (i == 7) add_wavy(g, a.x, a.y, b.x, b.y, 2);
            else push_seg(g, a.x, a.y, b.x, b.y, 0);
        } else {
            if (i == 0 || i == 3 || i == 5 || i == 8) add_gap(g, a.x, a.y, b.x, b.y, 100.0f, GAP_REQ_SPEED);
            else if (i == 1) add_wavy(g, a.x, a.y, b.x, b.y, 5);
            else if (i == 7) add_wavy(g, a.x, a.y, b.x, b.y, 4);
            else push_seg(g, a.x, a.y, b.x, b.y, 0);
        }
    }

    if (lvl == 1) push_bump(g, 450.0f, 0.0f);
    else if (lvl == 2) {
        push_bump(g, 1200.0f, 600.0f);
        push_bump(g, -400.0f, 2400.0f);
    } else {
        push_bump(g, 2400.0f, 1600.0f);
        push_bump(g, -800.0f, 2300.0f);
    }
    update_bounds(g);
}

static float dist_to_seg(float px, float py, float x1, float y1, float x2, float y2)
{
    float dx = x2 - x1, dy = y2 - y1;
    float l2 = dx * dx + dy * dy;
    float t, qx, qy;
    if (l2 == 0.0f) return hypotf(px - x1, py - y1);
    t = ((px - x1) * dx + (py - y1) * dy) / l2;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    qx = x1 + t * dx;
    qy = y1 + t * dy;
    return hypotf(px - qx, py - qy);
}

static float min_dist_to_road(const Game *g, float px, float py)
{
    float best = 1e30f;
    int i;
    for (i = 0; i < g->nsegs; i++) {
        float d = dist_to_seg(px, py, g->segs[i].x1, g->segs[i].y1, g->segs[i].x2, g->segs[i].y2);
        if (d < best) best = d;
    }
    return best;
}

static int point_on_road(const Game *g, float px, float py)
{
    return min_dist_to_road(g, px, py) <= game_road_width(g) * 0.5f;
}

static void wheel_at(const Car *c, float lx, float ly, float *ox, float *oy)
{
    float co = cosf(c->angle);
    float si = sinf(c->angle);
    *ox = c->x + (lx * co - ly * si);
    *oy = c->y + (lx * si + ly * co);
}

static void eval_status(Game *g, int *stuck, int *failing, int *danger, int *wheels)
{
    float hw = CAR_WIDTH * 0.5f;
    float hh = CAR_LENGTH * 0.5f;
    float x, y;
    int fl, fr, bl, br, front, rear;
    float width, center, com;
    Car *c = &g->car;

    if (c->airborne) {
        *wheels = 0;
        *stuck = 0;
        *failing = 0;
        *danger = 0;
        return;
    }

    wheel_at(c, -hw, -hh, &x, &y); fl = point_on_road(g, x, y);
    wheel_at(c,  hw, -hh, &x, &y); fr = point_on_road(g, x, y);
    wheel_at(c, -hw,  hh, &x, &y); bl = point_on_road(g, x, y);
    wheel_at(c,  hw,  hh, &x, &y); br = point_on_road(g, x, y);
    *wheels = fl + fr + bl + br;

    width = game_road_width(g);
    center = min_dist_to_road(g, c->x, c->y);
    com = center <= width * 0.5f;
    *danger = center >= (width * 0.5f) * 0.60f;
    front = fl || fr;
    rear = bl || br;
    *failing = 0;
    *stuck = 0;
    if (com) {
        if (*wheels == 0) *stuck = 1;
        else if (!front && !rear) *failing = 1;
    } else if (!(front && rear)) {
        *failing = 1;
    }
}

static void emit_smoke(Game *g, int per_wheel)
{
    float hw = CAR_WIDTH * 0.5f;
    float hh = CAR_LENGTH * 0.5f;
    float locals[4][2] = {{-hw,-hh},{hw,-hh},{-hw,hh},{hw,hh}};
    int w, n;
    for (w = 0; w < 4; w++) {
        float x, y;
        wheel_at(&g->car, locals[w][0], locals[w][1], &x, &y);
        for (n = 0; n < per_wheel; n++) {
            Smoke *s;
            if (g->nsmoke >= MAX_SMOKE) return;
            s = &g->smoke[g->nsmoke++];
            s->x = x + (rnd(g) - 0.5f) * 6.0f;
            s->y = y + (rnd(g) - 0.5f) * 6.0f;
            s->r = 4.0f + rnd(g) * 5.0f;
            s->life = 25;
        }
    }
}

static void age_smoke(Game *g)
{
    int i;
    for (i = 0; i < g->nsmoke; ) {
        g->smoke[i].life--;
        g->smoke[i].r += 0.2f;
        if (g->smoke[i].life <= 0) {
            g->smoke[i] = g->smoke[g->nsmoke - 1];
            g->nsmoke--;
        } else {
            i++;
        }
    }
}

static float beep_timer;

static void trigger_death(Game *g, const char *reason)
{
    snd_screech(0.0f);
    snd_fall();
    snd_engine_stop();
    g->game_over = 1;
    g->stuck = 0;
    g->state = ST_DEAD;
    g->menu = 0;
    snprintf(g->death_reason, sizeof(g->death_reason), "%s", reason);
    set_status(g, reason, 0xd93838ff);
}

static void launch_boost(Game *g, const Input *in)
{
    float dir = (in->brake && !in->gas) ? -1.0f : 1.0f;
    float charge = g->burnout_timer / TIME_TO_EXPLOSION;
    if (charge > 1.0f) charge = 1.0f;
    g->car.angle -= g->rock_wobble;
    g->car.speed = dir * g->car.max_speed * (BOOST_MULT + charge * 0.5f);
    emit_smoke(g, 3);
    snd_boost();
}

static void save_checkpoint(Game *g)
{
    g->last_x = g->car.x;
    g->last_y = g->car.y;
    g->last_angle = g->car.angle;
    g->last_speed = g->car.speed;
    g->cp_banner = 90;
}

static void simulate(Game *g, const Input *in)
{
    int stuck = 0, failing = 0, danger = 0, wheels = 4;
    int both;
    Car *c = &g->car;
    float power;
    int i;

    if (g->exploded) {
        g->post_explosion -= 1.0f / 60.0f;
        if (g->post_explosion <= 0.0f) {
            g->state = ST_DEAD;
            g->menu = 0;
        }
        return;
    }
    if (g->explosion_anim > 0.0f) {
        g->explosion_anim -= 1.0f / 60.0f;
        if (g->explosion_anim <= 0.0f) {
            g->exploded = 1;
            g->post_explosion = 2.0f;
        }
        return;
    }

    if (in->press_pause) {
        g->state = ST_PAUSE;
        g->menu = 0;
        g->paused = 1;
        return;
    }

    age_smoke(g);
    eval_status(g, &stuck, &failing, &danger, &wheels);
    c->wheels = wheels;
    g->danger = danger;

    if (failing && !c->airborne) {
        trigger_death(g, "FELL OFF INTO THE SKY!");
        return;
    }

    if (stuck && !c->airborne) {
        if (!g->stuck) {
            g->stuck = 1;
            g->stuck_time = 3.0f;
        }
        g->stuck_time -= 1.0f / 60.0f;
        if (g->stuck_time <= 0.0f) {
            trigger_death(g, "VEHICLE STUCK!");
            return;
        }
    } else {
        g->stuck = 0;
    }

    both = in->gas && in->brake;
    if (g->burnout_rocking && !both && !in->hbrake && !c->airborne &&
        c->wheels >= 1 && g->burnout_timer >= BOOST_MIN_CHARGE) {
        launch_boost(g, in);
    }
    if (!g->burnout_rocking) g->rock_wobble = 0.0f;
    g->burnout_rocking = 0;

    if (both && !c->airborne && c->wheels >= 1) {
        if (fabsf(c->speed) > 0.5f) {
            float brake_dir = c->speed > 0.0f ? 1.0f : -1.0f;
            c->speed -= brake_dir * c->accel * 2.5f;
            if (brake_dir > 0.0f && c->speed < 0.0f) c->speed = 0.0f;
            if (brake_dir < 0.0f && c->speed > 0.0f) c->speed = 0.0f;
            g->burnout_timer = 0.0f;
            beep_timer = 0.0f;
        } else {
            float wiggle, shake;
            g->burnout_rocking = 1;
            snd_screech(0.0f);
            snd_engine(0.4f, 1);
            c->speed = 0.0f;
            wiggle = sinf((float)g->tick / 60.0f * 35.0f) * 0.4f;
            shake = cosf((float)g->tick / 60.0f * 25.0f) * 0.03f;
            c->angle += shake;
            g->rock_wobble += shake;
            if (in->steer < -0.2f) c->angle -= 0.012f + rnd(g) * 0.008f;
            if (in->steer > 0.2f) c->angle += 0.012f + rnd(g) * 0.008f;
            c->x += sinf(c->angle) * wiggle;
            c->y -= cosf(c->angle) * wiggle;
            emit_smoke(g, 1);
            g->burnout_timer += 1.0f / 60.0f;
            beep_timer += 1.0f / 60.0f;
            {
                float prog = g->burnout_timer / TIME_TO_EXPLOSION;
                if (prog > 1.0f) prog = 1.0f;
                if (beep_timer >= 0.6f - prog * 0.55f) {
                    snd_beep(prog);
                    beep_timer = 0.0f;
                }
            }
            if (g->burnout_timer >= TIME_TO_EXPLOSION) {
                g->burnout_rocking = 0;
                snd_explosion();
                snd_engine_stop();
                g->explosion_anim = 0.6f;
                g->game_over = 1;
                snprintf(g->death_reason, sizeof(g->death_reason), "VEHICLE EXPLODED FROM BURNOUT!");
                set_status(g, "EXPLODED!", 0xd93838ff);
                return;
            }
            g->speed_is_text = 1;
            g->speed_kmh = 0;
            set_status(g, "BURNOUT!", 0xd93838ff);
            return;
        }
    } else {
        g->burnout_timer = 0.0f;
        beep_timer = 0.0f;
    }

    if (in->two_player) {
        /* Two players: P1 steers the front wheels, P2 the rear wheels. Wheels stay where they are
         * pointed until turned again or reset (only possible while the car is stationary). */
        const float rate = 0.045f, lim = 1.5708f;
        int still = fabsf(c->speed) < 0.3f;
        if (in->reset1 && still) {
            if (c->front_angle > 0.12f) c->front_angle -= 0.12f;
            else if (c->front_angle < -0.12f) c->front_angle += 0.12f;
            else c->front_angle = 0.0f;
        } else {
            c->front_angle += in->steer * rate;
        }
        if (in->reset2 && still) {
            if (c->rear_angle > 0.12f) c->rear_angle -= 0.12f;
            else if (c->rear_angle < -0.12f) c->rear_angle += 0.12f;
            else c->rear_angle = 0.0f;
        } else {
            c->rear_angle += in->steer2 * rate;
        }
        if (c->front_angle > lim) c->front_angle = lim;
        if (c->front_angle < -lim) c->front_angle = -lim;
        if (c->rear_angle > lim) c->rear_angle = lim;
        if (c->rear_angle < -lim) c->rear_angle = -lim;
    } else {
        /* Single player: tight-cornering steering, the car can turn on the spot (same as the HTML). */
        c->front_angle = in->steer * 0.5f;
        c->rear_angle = 0.0f;
        if (c->wheels >= 1 && !c->airborne && !g->stuck) {
            float dir = c->speed >= 0.0f ? 1.0f : -1.0f;
            c->angle += c->turn_speed * dir * in->steer;
        }
    }

    power = (c->airborne || g->stuck) ? 0.0f : (float)c->wheels / 4.0f;
    if (in->hbrake && power > 0.0f) {
        if (fabsf(c->speed) > 0.5f) {
            float brake_dir = c->speed > 0.0f ? 1.0f : -1.0f;
            c->speed -= brake_dir * c->accel * 2.2f;
            if (brake_dir > 0.0f && c->speed < 0.0f) c->speed = 0.0f;
            if (brake_dir < 0.0f && c->speed > 0.0f) c->speed = 0.0f;
        } else {
            c->speed = 0.0f;
        }
    } else if (both) {
        /* scrubbed above */
    } else if (in->gas && power > 0.0f) {
        if (c->speed < c->max_speed) {
            c->speed += c->accel * power;
            if (c->speed > c->max_speed) c->speed = c->max_speed;
        }
    } else if (in->brake && power > 0.0f) {
        if (c->speed > -c->max_speed) {
            c->speed -= c->accel * power;
            if (c->speed < -c->max_speed) c->speed = -c->max_speed;
        }
    } else {
        c->speed *= c->friction;
    }

    if (fabsf(c->speed) > c->max_speed) c->speed *= OVERSPEED_DECAY;

    {
        float asp = fabsf(c->speed);
        int skid = (in->hbrake || both) && power > 0.0f && asp > 0.5f;
        float lvl = asp / 3.0f;
        if (lvl > 1.0f) lvl = 1.0f;
        snd_screech(skid ? lvl : 0.0f);
        snd_engine(c->speed / c->max_speed, (in->gas || in->brake) && power > 0.0f);
    }

    if (in->two_player) {
        /* Rolling speed acts along each wheel set's heading; the body follows the average:
         * same angle = sideways slide, opposite angles = spin, facing each other = no movement. */
        float sf = sinf(c->front_angle), cf = cosf(c->front_angle);
        float sr = sinf(c->rear_angle), cr = cosf(c->rear_angle);
        float vx = c->speed * (sf + sr) * 0.5f;
        float vy = c->speed * (cf + cr) * 0.5f;
        float dth = c->speed * (sf - sr) / 52.0f;
        if (dth > 0.09f) dth = 0.09f;
        if (dth < -0.09f) dth = -0.09f;
        if (c->wheels >= 1 && !c->airborne && !g->stuck) c->angle += dth;
        c->x += sinf(c->angle) * vy + cosf(c->angle) * vx;
        c->y += -cosf(c->angle) * vy + sinf(c->angle) * vx;
    } else {
        c->x += sinf(c->angle) * c->speed;
        c->y -= cosf(c->angle) * c->speed;
    }

    if (!c->airborne) {
        for (i = 0; i < g->nbumps; i++) {
            float d;
            if (g->bumps[i].hit_cooldown > 0) g->bumps[i].hit_cooldown--;
            d = hypotf(c->x - g->bumps[i].x, c->y - g->bumps[i].y);
            if (d < 28.0f && g->bumps[i].hit_cooldown == 0) {
                c->speed *= 0.55f;
                c->vertical_offset = -8.0f;
                snd_bump();
                g->bumps[i].hit_cooldown = 20;
            }
        }
    }
    c->vertical_offset *= 0.7f;

    for (i = 0; i < g->ngaps; i++) {
        float d = hypotf(c->x - g->gaps[i].x, c->y - g->gaps[i].y);
        if (d < g->gaps[i].radius && !c->airborne) {
            float sp = fabsf(c->speed);
            if (sp >= g->gaps[i].min_speed) {
                c->airborne = 1;
                c->air_timer = 28;
                snd_jump();
            } else if (sp >= g->gaps[i].min_speed * 0.5f) {
                c->airborne = 1;
                c->air_timer = 12;
                snd_jump();
            }
        }
    }

    if (c->airborne) {
        c->air_timer--;
        c->scale = 1.0f + sinf(((float)c->air_timer / 28.0f) * 3.14159265f) * 0.4f;
        if (c->air_timer <= 0) {
            c->airborne = 0;
            c->scale = 1.0f;
        }
    }

    g->cp_timer++;
    if (g->cp_timer >= 300) {
        if (c->wheels == 4 && !c->airborne) save_checkpoint(g);
        g->cp_timer = 0;
    }

    g->speed_is_text = 0;
    g->speed_kmh = (int)(fabsf(c->speed) * 16.0f);
    if (g->stuck) set_status(g, "STUCK!", 0xd93838ff);
    else if (!(c->wheels >= 1 && !c->airborne && !g->stuck) && !c->airborne) set_status(g, "NO TRACTION!", 0xe67e22ff);
    else if (c->airborne) set_status(g, "AIRBORNE!", 0x3498dbff);
    else set_status(g, "ROAD OK", 0x27ae60ff);
}

static void start_level(Game *g, int lvl)
{
    Car *c = &g->car;
    build_level(g, lvl);
    c->max_speed = 8.0f;
    c->accel = 0.16f;
    c->friction = 0.965f;
    c->turn_speed = 0.055f;
    reset_car(g, 0, 0, 0, 0);
    g->last_x = 0;
    g->last_y = 0;
    g->last_angle = 0;
    g->last_speed = 0;
    reset_run(g);
    g->state = ST_PLAY;
    g->menu = 0;
}

void game_init(Game *g)
{
    int i;
    memset(g, 0, sizeof(*g));
    g->rng = 1;
    g->car.max_speed = 8.0f;
    g->car.accel = 0.16f;
    g->car.friction = 0.965f;
    g->car.turn_speed = 0.055f;
    g->nclouds = MAX_CLOUDS;
    for (i = 0; i < MAX_CLOUDS; i++) {
        g->clouds[i].x = (rnd(g) - 0.5f) * 6000.0f;
        g->clouds[i].y = (rnd(g) - 0.5f) * 6000.0f;
        g->clouds[i].r = 40.0f + rnd(g) * 60.0f;
    }
    build_level(g, 1);
    reset_car(g, 0, 0, 0, 0);
    reset_run(g);
    g->state = ST_SELECT;
    g->menu = 0;
    set_status(g, "SELECT A TRACK", 0x1a2530ff);
}

static void preview_level(Game *g, int lvl)
{
    build_level(g, lvl);
    reset_car(g, 0, 0, 0, 0);
    g->nsmoke = 0;
    g->exploded = 0;
    g->explosion_anim = 0.0f;
    g->stuck = 0;
}

static void go_select(Game *g)
{
    int lvl = g->level;
    if (lvl < 1) lvl = 1;
    g->state = ST_SELECT;
    g->menu = lvl - 1;
    g->paused = 0;
    g->game_over = 0;
    preview_level(g, lvl);
}

static void respawn(Game *g)
{
    reset_car(g, g->last_x, g->last_y, g->last_angle, 0);
    reset_run(g);
    g->state = ST_PLAY;
}

static void menu_move(Game *g, const Input *in, int count)
{
    if (in->press_up) g->menu = (g->menu + count - 1) % count;
    if (in->press_down) g->menu = (g->menu + 1) % count;
}

void game_frame(Game *g, const Input *in)
{
    g->tick++;
    g->two_player = in->two_player;
    if (g->cp_banner > 0) g->cp_banner--;
    if (g->state != ST_PLAY) {
        snd_engine_stop();
        snd_screech(0.0f);
    }

    if (g->state == ST_SELECT) {
        int prev = g->menu;
        menu_move(g, in, 3);
        if (g->menu != prev) preview_level(g, g->menu + 1);
        if (in->press_confirm) start_level(g, g->menu + 1);
        return;
    }

    if (g->state == ST_PAUSE) {
        menu_move(g, in, 3);
        if (in->press_triangle) g->half_road = !g->half_road;
        if (in->press_confirm) {
            if (g->menu == 0) {
                g->state = ST_PLAY;
                g->paused = 0;
            } else if (g->menu == 1) {
                g->half_road = !g->half_road;
            } else {
                go_select(g);
            }
        }
        return;
    }

    if (g->state == ST_DEAD) {
        menu_move(g, in, 3);
        if (in->press_confirm) {
            if (g->menu == 0) respawn(g);
            else if (g->menu == 1) start_level(g, g->level);
            else go_select(g);
        }
        return;
    }

    simulate(g, in);
}

#include "draw.h"
#include "font8x8_basic.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <tiny3d.h>

#define CAR_WIDTH 24.0f
#define CAR_LENGTH 72.0f

static void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float z, u32 col)
{
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(x0, y0, z); tiny3d_VertexColor(col);
    tiny3d_VertexPos(x1, y1, z); tiny3d_VertexColor(col);
    tiny3d_VertexPos(x2, y2, z); tiny3d_VertexColor(col);
    tiny3d_VertexPos(x3, y3, z); tiny3d_VertexColor(col);
    tiny3d_End();
}

static void rect(float x, float y, float w, float h, float z, u32 col)
{
    quad(x, y, x + w, y, x + w, y + h, x, y + h, z, col);
}

static void circle(float cx, float cy, float r, float z, u32 col)
{
    int i;
    const int n = 12;
    tiny3d_SetPolygon(TINY3D_TRIANGLE_FAN);
    tiny3d_VertexPos(cx, cy, z);
    tiny3d_VertexColor(col);
    for (i = 0; i <= n; i++) {
        float a = (float)i / (float)n * 6.2831853f;
        tiny3d_VertexPos(cx + cosf(a) * r, cy + sinf(a) * r, z);
        tiny3d_VertexColor(col);
    }
    tiny3d_End();
}

static void thick_line(float x1, float y1, float x2, float y2, float width, float z, u32 col)
{
    float dx = x2 - x1, dy = y2 - y1;
    float len = hypotf(dx, dy);
    float nx, ny;
    if (len < 0.001f) return;
    nx = -dy / len * width * 0.5f;
    ny = dx / len * width * 0.5f;
    quad(x1 + nx, y1 + ny, x1 - nx, y1 - ny, x2 - nx, y2 - ny, x2 + nx, y2 + ny, z, col);
}

static int seg_visible(float x1, float y1, float x2, float y2, float margin)
{
    float minx = x1 < x2 ? x1 : x2;
    float maxx = x1 > x2 ? x1 : x2;
    float miny = y1 < y2 ? y1 : y2;
    float maxy = y1 > y2 ? y1 : y2;
    return maxx >= -margin && minx <= VW + margin && maxy >= -margin && miny <= VH + margin;
}

static void to_screen(const Game *g, float wx, float wy, float *sx, float *sy)
{
    *sx = (wx - g->car.x) + VW * 0.5f;
    *sy = (wy - g->car.y) + VH * 0.5f;
}

static void draw_text(float x, float y, float scale, u32 col, const char *str)
{
    float cx = x;
    const unsigned char *p;
    for (p = (const unsigned char *)str; *p; p++) {
        unsigned char ch = *p;
        int row;
        if (ch > 127) ch = '?';
        for (row = 0; row < 8; row++) {
            unsigned bits = (unsigned char)font8x8_basic[ch][row];
            int coln = 0;
            while (coln < 8) {
                if (bits & (1u << coln)) {
                    int start = coln;
                    while (coln < 8 && (bits & (1u << coln))) coln++;
                    rect(cx + start * scale, y + row * scale, (coln - start) * scale, scale, 0.0f, col);
                } else {
                    coln++;
                }
            }
        }
        cx += 8.0f * scale;
    }
}

static float text_w(const char *s, float scale)
{
    return (float)strlen(s) * 8.0f * scale;
}

static void draw_text_centered(float y, float scale, u32 col, const char *s)
{
    draw_text((VW - text_w(s, scale)) * 0.5f, y, scale, col, s);
}

static void draw_road(const Game *g)
{
    float width = game_road_width(g);
    int i;
    for (i = 0; i < g->nsegs; i++) {
        float x1, y1, x2, y2;
        const Seg *s = &g->segs[i];
        to_screen(g, s->x1 + 15.0f, s->y1 + 15.0f, &x1, &y1);
        to_screen(g, s->x2 + 15.0f, s->y2 + 15.0f, &x2, &y2);
        if (!seg_visible(x1, y1, x2, y2, width)) continue;
        thick_line(x1, y1, x2, y2, width + 8.0f, 40.0f, 0x142d4640);
    }
    for (i = 0; i < g->nsegs; i++) {
        float x1, y1, x2, y2;
        const Seg *s = &g->segs[i];
        to_screen(g, s->x1, s->y1, &x1, &y1);
        to_screen(g, s->x2, s->y2, &x2, &y2);
        if (!seg_visible(x1, y1, x2, y2, width)) continue;
        thick_line(x1, y1, x2, y2, width, 30.0f, 0x2c3e50ff);
        thick_line(x1, y1, x2, y2, 4.0f, 20.0f, 0xe74c3cff);
        {
            float dx = x2 - x1, dy = y2 - y1;
            float len = hypotf(dx, dy);
            float t;
            if (len < 1.0f) continue;
            for (t = 0.0f; t < len; t += 24.0f) {
                float a = t / len;
                float b = (t + 12.0f) / len;
                if (b > 1.0f) b = 1.0f;
                thick_line(x1 + dx * a, y1 + dy * a, x1 + dx * b, y1 + dy * b, 2.0f, 10.0f, 0xf1c40fff);
            }
        }
    }
    for (i = 0; i < g->nsegs; i++) {
        float x, y;
        if (!g->segs[i].is_ramp) continue;
        to_screen(g, g->segs[i].x2, g->segs[i].y2, &x, &y);
        if (x < -80 || x > VW + 80 || y < -80 || y > VH + 80) continue;
        circle(x, y, width * 0.5f, 9.0f, 0xe67e22ff);
    }
    for (i = 0; i < g->nbumps; i++) {
        float x, y;
        to_screen(g, g->bumps[i].x, g->bumps[i].y, &x, &y);
        if (x < -80 || x > VW + 80 || y < -80 || y > VH + 80) continue;
        rect(x - 12.0f, y - width * 0.5f, 24.0f, width, 8.0f, 0xf1c40fff);
        {
            float yy;
            for (yy = -width * 0.5f; yy < width * 0.5f; yy += 12.0f)
                rect(x - 12.0f, y + yy, 8.0f, 6.0f, 7.0f, 0x1a2530ff);
        }
    }
}

static void draw_clouds(const Game *g)
{
    int i;
    for (i = 0; i < g->nclouds; i++) {
        float x, y;
        to_screen(g, g->clouds[i].x, g->clouds[i].y, &x, &y);
        if (x < -80 || x > VW + 80 || y < -80 || y > VH + 80) continue;
        circle(x, y, g->clouds[i].r, 50.0f, 0xffffff88);
    }
}

static void draw_smoke(const Game *g)
{
    int i;
    for (i = 0; i < g->nsmoke; i++) {
        float x, y;
        to_screen(g, g->smoke[i].x, g->smoke[i].y, &x, &y);
        circle(x, y, g->smoke[i].r, 6.0f, 0xc8c8c880);
    }
}

static void local_rect(float x, float y, float w, float h, float z, u32 col)
{
    rect(x, y, w, h, z, col);
}

static void draw_car(const Game *g)
{
    const Car *c = &g->car;
    float sx = VW * 0.5f;
    float sy = VH * 0.5f + c->vertical_offset;
    MATRIX m;
    int flash;
    u32 body;

    m = MatrixScale(c->scale, c->scale, 1.0f);
    m = MatrixMultiply(m, MatrixRotationZ(c->angle));
    m = MatrixMultiply(m, MatrixTranslation(sx, sy, 0.0f));
    tiny3d_SetMatrixModelView(&m);

    if (g->exploded) {
        local_rect(-15, -20, 30, 40, 4, 0x00000066);
        local_rect(-12, -15, 10, 14, 3, 0x333333ff);
        local_rect(4, 2, 12, 16, 3, 0x222222ff);
        local_rect(-6, 8, 10, 10, 3, 0x444444ff);
        circle(0, 0, 14.0f + sinf((float)g->tick * 0.4f) * 6.0f, 2, 0xe74c3cff);
        circle(-2, 2, 8.0f, 1, 0xf1c40fff);
        tiny3d_SetMatrixModelView(NULL);
        return;
    }

    if (g->explosion_anim > 0.0f) {
        float r = (1.0f - (g->explosion_anim / 0.6f)) * 80.0f;
        circle(0, 0, r, 2, 0xe74c3cff);
        circle(0, 0, r * 0.6f, 1, 0xf1c40fff);
        tiny3d_SetMatrixModelView(NULL);
        return;
    }

    local_rect(-CAR_WIDTH * 0.5f + 4.0f, -CAR_LENGTH * 0.5f + 4.0f, CAR_WIDTH, CAR_LENGTH, 5, 0x0000004c);
    flash = (g->danger || g->stuck || g->burnout_rocking) && ((g->tick / 15) % 2 == 0);
    body = flash ? 0xd93838ff : 0x1b365dff;
    local_rect(-CAR_WIDTH * 0.5f, -CAR_LENGTH * 0.5f, CAR_WIDTH, CAR_LENGTH, 4, body);
    local_rect(-CAR_WIDTH * 0.5f + 3.0f, -14.0f, CAR_WIDTH - 6.0f, 28.0f, 3, 0x112233ff);
    local_rect(-CAR_WIDTH * 0.5f, -2.0f, CAR_WIDTH, 4.0f, 2, 0xffffffff);
    local_rect(-CAR_WIDTH * 0.5f + 2.0f, -CAR_LENGTH * 0.5f + 1.0f, 5, 4, 2, 0xf1c40fff);
    local_rect(CAR_WIDTH * 0.5f - 7.0f, -CAR_LENGTH * 0.5f + 1.0f, 5, 4, 2, 0xf1c40fff);
    local_rect(-CAR_WIDTH * 0.5f + 2.0f, CAR_LENGTH * 0.5f - 5.0f, 5, 4, 2, 0xf1c40fff);
    local_rect(CAR_WIDTH * 0.5f - 7.0f, CAR_LENGTH * 0.5f - 5.0f, 5, 4, 2, 0xf1c40fff);
    tiny3d_SetMatrixModelView(NULL);
}

static void draw_minimap(const Game *g)
{
    float w, h, scale, ox, oy;
    float box = 110.0f;
    float x0 = VW - box - 10.0f;
    float y0 = 8.0f;
    int i;
    if (g->nsegs <= 0) return;
    w = g->bounds_max_x - g->bounds_min_x;
    h = g->bounds_max_y - g->bounds_min_y;
    if (w < 1.0f) w = 1.0f;
    if (h < 1.0f) h = 1.0f;
    scale = (box - 16.0f) / (w > h ? w : h);
    ox = x0 + box * 0.5f - (g->bounds_min_x + w * 0.5f) * scale;
    oy = y0 + box * 0.5f - (g->bounds_min_y + h * 0.5f) * scale;
    rect(x0, y0, box, box, 1.0f, 0xffffff40);
    rect(x0 + 2, y0 + 2, box - 4, box - 4, 0.9f, 0x70c5ce88);
    for (i = 0; i < g->nsegs; i++) {
        float x1 = ox + g->segs[i].x1 * scale;
        float y1 = oy + g->segs[i].y1 * scale;
        float x2 = ox + g->segs[i].x2 * scale;
        float y2 = oy + g->segs[i].y2 * scale;
        thick_line(x1, y1, x2, y2, 2.0f, 0.5f, 0x2c3e50ff);
    }
    {
        float cx = ox + g->car.x * scale;
        float cy = oy + g->car.y * scale;
        circle(cx, cy, 3.0f, 0.4f, 0xd93838ff);
    }
}

static void menu_panel(const char **lines, int n, int selected, const char *title)
{
    float w = 420.0f, h = 60.0f + (float)n * 36.0f;
    float x = (VW - w) * 0.5f, y = (VH - h) * 0.5f;
    int i;
    rect(x, y, w, h, 0.2f, 0xffffffe8);
    rect(x, y, w, 4, 0.1f, 0x1a2530ff);
    float ts = 2.0f;
    if (text_w(title, ts) > w - 24.0f) ts = 1.0f;
    draw_text_centered(y + 12.0f, ts, 0xd93838ff, title);
    for (i = 0; i < n; i++) {
        float ly = y + 48.0f + (float)i * 36.0f;
        if (i == selected) rect(x + 16.0f, ly - 4.0f, w - 32.0f, 28.0f, 0.12f, 0x1a253028);
        draw_text_centered(ly, 1.5f, 0x1a2530ff, lines[i]);
    }
}

static void draw_hud(const Game *g)
{
    char line[96];
    snprintf(line, sizeof(line), "LEVEL: %d", g->level);
    draw_text(12, 10, 1.0f, 0x1a2530ff, line);
    if (g->speed_is_text) snprintf(line, sizeof(line), "SPEED: BURNOUT");
    else snprintf(line, sizeof(line), "SPEED: %d KM/H", g->speed_kmh);
    draw_text(12, 22, 1.0f, 0x1a2530ff, line);
    snprintf(line, sizeof(line), "WHEELS ON ROAD: %d/4", g->car.airborne ? 0 : g->car.wheels);
    draw_text(12, 34, 1.0f, 0x1a2530ff, line);
    draw_text(12, 46, 1.0f, g->status_color, g->status);

    if (g->cp_banner > 0)
        draw_text_centered(8, 1.0f, 0x27ae60ff, "CHECKPOINT SAVED");

    if (g->stuck && g->state == ST_PLAY) {
        snprintf(line, sizeof(line), "STUCK! FAILING IN %ds", (int)(g->stuck_time + 0.999f));
        draw_text_centered(64, 1.5f, 0xd93838ff, line);
    }

    if (g->state == ST_SELECT) {
        const char *lines[3] = {
            "LVL 1: SKY HIGHWAY",
            "LVL 2: PRECISION CITY",
            "LVL 3: THE GAUNTLET"
        };
        menu_panel(lines, 3, g->menu, "SELECT A TRACK");
        draw_text(12, VH - 28, 1.0f, 0x1a2530ff, "DPAD + CROSS    L-STICK STEER  R2/CROSS GAS  L2/SQUARE REV");
    } else if (g->state == ST_PAUSE) {
        char width[32];
        const char *lines[3];
        snprintf(width, sizeof(width), g->half_road ? "ROAD: HALF WIDTH" : "ROAD: FULL WIDTH");
        lines[0] = "RESUME";
        lines[1] = width;
        lines[2] = "CHANGE LEVEL";
        menu_panel(lines, 3, g->menu, "PAUSED");
    } else if (g->state == ST_DEAD && g->exploded && g->post_explosion > 0.0f) {
        /* wreck is on screen; menu waits until the blast settles */
    } else if (g->state == ST_DEAD) {
        const char *lines[3] = { "CHECKPOINT", "RESTART TRACK", "CHANGE LEVEL" };
        menu_panel(lines, 3, g->menu, g->death_reason[0] ? g->death_reason : "FELL OFF INTO THE SKY!");
    }
}

void draw_frame(const Game *g)
{
    tiny3d_Project2D();
    draw_clouds(g);
    draw_road(g);
    draw_smoke(g);
    draw_car(g);
    draw_minimap(g);
    draw_hud(g);
}

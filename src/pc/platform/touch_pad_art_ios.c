/* Port/eame: the on-screen controller's pictures on iOS (touch_pad_art.h).
 *
 * Instead of the game's 16 x 16 button sprites enlarged, every control is
 * drawn here at the screen's own resolution: dark glass with a light rim, a
 * soft shadow under it and thin white symbols, in one colour (monochrome).
 * Held, a control lights up and its symbol turns fully white. Each picture
 * is drawn once for its size and kept (a sprite), so a frame only blends
 * them; nothing of the game, and no image file, is used. Main thread only. */
#ifdef MEMORIES_IOS
#include "touch_pad_art.h"
#include "touch_pad.h"
#include "pc/text/overlay_text.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t *pixels; /* 0xAARRGGBB, straight alpha */
    int x, y, w, h;   /* where it goes in the window */
} Sprite;

/* [button][held]; the D-pad is one picture for each of the 16 ways its
 * arrows can be held (TOUCH_UP's slots unused, `dpad` instead). */
static Sprite sprites[TOUCH_BUTTONS][2];
static Sprite dpad[16];
static int cached_unit = -1, cached_layout[TOUCH_BUTTONS][4];

/* --- shapes (signed distances in pixels, negative inside) ------------------ */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float coverage(float d) { return clampf(0.5f - d, 0.0f, 1.0f); }

static float rounded_box(float px, float py, float cx, float cy, float hw, float hh, float r)
{
    float qx = fabsf(px - cx) - hw + r, qy = fabsf(py - cy) - hh + r;
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    return sqrtf(ox * ox + oy * oy) + (qx > qy ? (qx < 0 ? qx : 0) : (qy < 0 ? qy : 0)) - r;
}

static float segment(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float t = clampf((wx * vx + wy * vy) / (vx * vx + vy * vy), 0.0f, 1.0f);
    float dx = wx - vx * t, dy = wy - vy * t;
    return sqrtf(dx * dx + dy * dy);
}

/* A smooth union: the D-pad's inner corners are rounded. */
static float smooth_min(float a, float b, float k)
{
    float h = clampf(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
    return b + (a - b) * h - k * h * (1.0f - h);
}

/* Paints straight-alpha colour c at alpha a over the pixel (Porter-Duff over). */
static void over(uint32_t *pixel, float r, float g, float b, float a)
{
    float da = (float)(*pixel >> 24) / 255.0f, dr = (float)(*pixel >> 16 & 255) / 255.0f,
          dg = (float)(*pixel >> 8 & 255) / 255.0f, db = (float)(*pixel & 255) / 255.0f, oa;
    if (a <= 0.0f) return;
    oa = a + da * (1.0f - a);
    if (oa <= 0.0f) return;
    dr = (r * a + dr * da * (1.0f - a)) / oa;
    dg = (g * a + dg * da * (1.0f - a)) / oa;
    db = (b * a + db * da * (1.0f - a)) / oa;
    *pixel = (uint32_t)(oa * 255.0f + 0.5f) << 24 | (uint32_t)(dr * 255.0f + 0.5f) << 16 |
             (uint32_t)(dg * 255.0f + 0.5f) << 8 | (uint32_t)(db * 255.0f + 0.5f);
}

/* --- the glass ------------------------------------------------------------------ */

typedef float (*ShapeFn)(const void *shape, float x, float y);
typedef float (*LitFn)(const void *shape, float x, float y); /* 0-1: how lit the glass is there */

/* The glass of a control: the shadow below, a dark tint (so it reads over a
 * bright picture), a white veil lighter towards the top, and a rim of light,
 * brighter along the top edge. */
static void glass(Sprite *sprite, ShapeFn shape, const void *data, LitFn lit, float base_lit, float unit)
{
    int i, j;
    float rim = unit * 0.022f, blur = unit * 0.06f, drop = unit * 0.035f;
    if (rim < 1.5f) rim = 1.5f;
    for (j = 0; j < sprite->h; j++) {
        for (i = 0; i < sprite->w; i++) {
            float x = (float)i + 0.5f, y = (float)j + 0.5f, d = shape(data, x, y), inside = coverage(d), light, ny;
            uint32_t *pixel = &sprite->pixels[j * sprite->w + i];
            float shadow_d = shape(data, x, y - drop);
            if (shadow_d > -blur && inside < 1.0f) {
                float s = shadow_d < 0 ? 1.0f : expf(-shadow_d / blur);
                over(pixel, 0, 0, 0, 0.30f * s * (1.0f - inside));
            }
            if (inside <= 0.0f) continue;
            light = clampf(base_lit + (lit ? lit(data, x, y) : 0.0f), 0.0f, 1.0f);
            ny = (float)j / (float)(sprite->h > 1 ? sprite->h - 1 : 1);
            over(pixel, 0.05f, 0.05f, 0.07f, 0.30f * inside);
            over(pixel, 1, 1, 1, (0.13f + 0.10f * (1.0f - ny) + 0.22f * light) * inside);
            /* A gloss over the top third, as light on curved glass. */
            if (ny < 0.45f) over(pixel, 1, 1, 1, 0.10f * (1.0f - ny / 0.45f) * inside);
            /* The rim: a band just inside the edge, bright along the top. */
            {
                float band = coverage(fabsf(d + rim * 0.5f) - rim * 0.5f);
                over(pixel, 1, 1, 1, band * (0.28f + 0.42f * (1.0f - ny) + 0.25f * light));
            }
        }
    }
}

/* A white stroke along a distance field, for the symbols. */
static void stroke(Sprite *sprite, float (*field)(const void *, float, float), const void *data, float width,
                   float alpha)
{
    int i, j;
    for (j = 0; j < sprite->h; j++)
        for (i = 0; i < sprite->w; i++) {
            float c = coverage(field(data, (float)i + 0.5f, (float)j + 0.5f) - width * 0.5f);
            if (c > 0.0f) over(&sprite->pixels[j * sprite->w + i], 1, 1, 1, c * alpha);
        }
}

/* --- shapes of the controls ----------------------------------------------- */

typedef struct { float cx, cy, hw, hh, r; } Box;
static float box_shape(const void *data, float x, float y)
{
    const Box *b = data;
    return rounded_box(x, y, b->cx, b->cy, b->hw, b->hh, b->r);
}

typedef struct { float cx, cy, r; } Circle;
static float circle_shape(const void *data, float x, float y)
{
    const Circle *c = data;
    float dx = x - c->cx, dy = y - c->cy;
    return sqrtf(dx * dx + dy * dy) - c->r;
}

typedef struct { float cx, cy, arm, half_w, r, k, unit; unsigned held; } Cross;
static float cross_shape(const void *data, float x, float y)
{
    const Cross *c = data;
    float a = rounded_box(x, y, c->cx, c->cy, c->arm, c->half_w, c->r);
    float b = rounded_box(x, y, c->cx, c->cy, c->half_w, c->arm, c->r);
    return smooth_min(a, b, c->k);
}
/* Held arrows light their arm of the cross. */
static float cross_lit(const void *data, float x, float y)
{
    const Cross *c = data;
    float dx = x - c->cx, dy = y - c->cy, edge = c->unit * 0.45f, soft = c->unit * 0.12f, l = 0.0f;
    if (c->held & 1) l += clampf((-dy - edge) / soft, 0, 1); /* up */
    if (c->held & 2) l += clampf((dy - edge) / soft, 0, 1);  /* down */
    if (c->held & 4) l += clampf((-dx - edge) / soft, 0, 1); /* left */
    if (c->held & 8) l += clampf((dx - edge) / soft, 0, 1);  /* right */
    return clampf(l, 0, 1);
}

/* Symbols: a list of segments (a polyline's pieces), or a ring. */
typedef struct { int n; float s[8][4]; } Lines;
static float lines_field(const void *data, float x, float y)
{
    const Lines *l = data;
    float best = 1e9f;
    int i;
    for (i = 0; i < l->n; i++) {
        float d = segment(x, y, l->s[i][0], l->s[i][1], l->s[i][2], l->s[i][3]);
        if (d < best) best = d;
    }
    return best;
}
static float ring_field(const void *data, float x, float y) { return fabsf(circle_shape(data, x, y)); }
static float box_outline_field(const void *data, float x, float y) { return fabsf(box_shape(data, x, y)); }

static void add_line(Lines *l, float ax, float ay, float bx, float by)
{
    l->s[l->n][0] = ax;
    l->s[l->n][1] = ay;
    l->s[l->n][2] = bx;
    l->s[l->n][3] = by;
    l->n++;
}

/* A chevron pointing (dx, dy) at cx, cy. */
static void chevron(Lines *l, float cx, float cy, float dx, float dy, float size)
{
    float tipx = cx + dx * size * 0.5f, tipy = cy + dy * size * 0.5f;
    float bx = cx - dx * size * 0.5f, by = cy - dy * size * 0.5f, px = -dy * size, py = dx * size;
    add_line(l, bx + px, by + py, tipx, tipy);
    add_line(l, bx - px, by - py, tipx, tipy);
}

/* --- making the sprites --------------------------------------------------- */

static int make(Sprite *sprite, int x, int y, int w, int h, int pad)
{
    sprite->x = x - pad;
    sprite->y = y - pad;
    sprite->w = w + 2 * pad;
    sprite->h = h + 2 * pad;
    sprite->pixels = calloc((size_t)sprite->w * (size_t)sprite->h, sizeof(uint32_t));
    return sprite->pixels != NULL;
}

static void make_face(Sprite *sprite, TouchButton button, int held, int unit)
{
    int x, y, w, h, pad = unit / 8;
    Circle c;
    float r, width;
    if (!TouchPad_Rect(button, &x, &y, &w, &h) || !make(sprite, x, y, w, h, pad)) return;
    c.cx = (float)sprite->w * 0.5f;
    c.cy = (float)sprite->h * 0.5f;
    c.r = (float)(w < h ? w : h) * 0.46f;
    glass(sprite, circle_shape, &c, NULL, held ? 1.0f : 0.0f, (float)unit);
    r = c.r;
    width = r * 0.085f;
    if (button == TOUCH_CIRCLE) {
        Circle ring = {c.cx, c.cy, r * 0.33f};
        stroke(sprite, ring_field, &ring, width, held ? 1.0f : 0.88f);
    } else if (button == TOUCH_SQUARE) {
        Box square = {c.cx, c.cy, r * 0.30f, r * 0.30f, r * 0.05f};
        stroke(sprite, box_outline_field, &square, width, held ? 1.0f : 0.88f);
    } else {
        Lines l = {0};
        if (button == TOUCH_CROSS) {
            float k = r * 0.27f;
            add_line(&l, c.cx - k, c.cy - k, c.cx + k, c.cy + k);
            add_line(&l, c.cx - k, c.cy + k, c.cx + k, c.cy - k);
        } else { /* Triangle, its middle a little low so it looks centred */
            float R = r * 0.40f, cy = c.cy + R * 0.12f;
            float ax = c.cx, ay = cy - R, bx = c.cx - R * 0.866f, by = cy + R * 0.5f, dx = c.cx + R * 0.866f;
            add_line(&l, ax, ay, bx, by);
            add_line(&l, bx, by, dx, by);
            add_line(&l, dx, by, ax, ay);
        }
        stroke(sprite, lines_field, &l, width, held ? 1.0f : 0.88f);
    }
}

static void make_pill(Sprite *sprite, TouchButton button, int held, int unit)
{
    static const char *labels[TOUCH_BUTTONS] = {
        [TOUCH_L1] = "L1", [TOUCH_L2] = "L2", [TOUCH_R1] = "R1", [TOUCH_R2] = "R2",
        [TOUCH_SELECT] = "SELECT", [TOUCH_START] = "START", [TOUCH_MENU] = "MENU"};
    int x, y, w, h, pad = unit / 8, pixels, text_w, play, left;
    Box b;
    MenuCanvas canvas;
    uint32_t color = held ? 0xffffffffu : 0xffe9e9eeu;
    if (!TouchPad_Rect(button, &x, &y, &w, &h) || !make(sprite, x, y, w, h, pad)) return;
    b.cx = (float)sprite->w * 0.5f;
    b.cy = (float)sprite->h * 0.5f;
    b.hw = (float)w * 0.5f;
    b.hh = (float)h * 0.5f;
    b.r = b.hh;
    glass(sprite, box_shape, &b, NULL, held ? 1.0f : 0.0f, (float)unit);
    memset(&canvas, 0, sizeof(canvas));
    canvas.pixels = sprite->pixels;
    canvas.stride = canvas.width = sprite->w;
    canvas.height = sprite->h;
    canvas.alpha = 1;
    pixels = h * (button == TOUCH_SELECT || button == TOUCH_START || button == TOUCH_MENU ? 40 : 46) / 100;
    text_w = OverlayText_Width(labels[button], pixels);
    play = button == TOUCH_START ? pixels * 7 / 10 : 0; /* START's ▶ after the word */
    left = (sprite->w - text_w - (play ? play + pixels / 3 : 0)) / 2;
    OverlayText_Draw(&canvas, left, sprite->h / 2, sprite->w, labels[button], pixels, color);
    if (play) {
        int i, j;
        float px = (float)(left + text_w + pixels / 3), py = (float)sprite->h * 0.5f, s = (float)play;
        for (j = 0; j < sprite->h; j++)
            for (i = 0; i < sprite->w; i++) {
                /* A triangle pointing right: inside all three edges. */
                float fx = (float)i + 0.5f - px, fy = (float)j + 0.5f - py;
                float e1 = -fx, e2 = (fx * 0.5f + fy * 0.866f) - s * 0.433f, e3 = (fx * 0.5f - fy * 0.866f) - s * 0.433f;
                float d = e1 > e2 ? (e1 > e3 ? e1 : e3) : (e2 > e3 ? e2 : e3);
                float c = coverage(d);
                if (c > 0) over(&sprite->pixels[j * sprite->w + i], (float)(color >> 16 & 255) / 255.0f,
                                (float)(color >> 8 & 255) / 255.0f, (float)(color & 255) / 255.0f, c);
            }
    }
}

static void make_dpad(Sprite *sprite, unsigned held, int unit)
{
    int ux, uy, uw, uh, lx, ly, lw, lh, pad = unit / 8;
    Cross c;
    Lines l = {0};
    float s = (float)unit;
    if (!TouchPad_Rect(TOUCH_UP, &ux, &uy, &uw, &uh) || !TouchPad_Rect(TOUCH_LEFT, &lx, &ly, &lw, &lh) ||
        !make(sprite, lx, uy, 3 * uw, 3 * uh, pad))
        return;
    c.cx = (float)(ux + uw / 2 - sprite->x);
    c.cy = (float)(ly + lh / 2 - sprite->y);
    c.arm = s * 1.30f;
    c.half_w = s * 0.42f;
    c.r = s * 0.20f;
    c.k = s * 0.14f;
    c.unit = s;
    c.held = held;
    glass(sprite, cross_shape, &c, cross_lit, 0.0f, s);
    chevron(&l, c.cx, c.cy - s, 0, -1, s * 0.13f);
    chevron(&l, c.cx, c.cy + s, 0, 1, s * 0.13f);
    chevron(&l, c.cx - s, c.cy, -1, 0, s * 0.13f);
    chevron(&l, c.cx + s, c.cy, 1, 0, s * 0.13f);
    stroke(sprite, lines_field, &l, s * 0.05f, 0.9f);
}

static void forget(void)
{
    int i;
    for (i = 0; i < TOUCH_BUTTONS; i++) {
        free(sprites[i][0].pixels);
        free(sprites[i][1].pixels);
    }
    for (i = 0; i < 16; i++) free(dpad[i].pixels);
    memset(sprites, 0, sizeof(sprites));
    memset(dpad, 0, sizeof(dpad));
    cached_unit = -1;
}

void TouchPadArt_Reset(void) { forget(); }

/* The sprites are drawn for the layout they were made for: a new one (the
 * window turned, the safe area changed) makes them again. */
static void check_layout(void)
{
    int i, now[TOUCH_BUTTONS][4], up_w;
    for (i = 0; i < TOUCH_BUTTONS; i++)
        if (!TouchPad_Rect((TouchButton)i, &now[i][0], &now[i][1], &now[i][2], &now[i][3])) return;
    up_w = now[TOUCH_UP][2];
    if (cached_unit == up_w && !memcmp(now, cached_layout, sizeof(now))) return;
    forget();
    cached_unit = up_w;
    memcpy(cached_layout, now, sizeof(now));
}

static void blit(MenuCanvas *canvas, const Sprite *sprite, int *x0, int *y0, int *x1, int *y1)
{
    int i, j;
    if (!sprite->pixels) return;
    for (j = 0; j < sprite->h; j++) {
        int y = sprite->y + j;
        if (y < 0 || y >= canvas->height) continue;
        for (i = 0; i < sprite->w; i++) {
            uint32_t p = sprite->pixels[j * sprite->w + i];
            int x = sprite->x + i;
            if (!(p >> 24) || x < 0 || x >= canvas->width) continue;
            OverlayText_Blend(canvas, x, y, p & 0xffffffu, p >> 24);
        }
    }
    if (sprite->x < *x0) *x0 = sprite->x;
    if (sprite->y < *y0) *y0 = sprite->y;
    if (sprite->x + sprite->w > *x1) *x1 = sprite->x + sprite->w;
    if (sprite->y + sprite->h > *y1) *y1 = sprite->y + sprite->h;
}

void TouchPadArt_Draw(MenuCanvas *canvas, int *x, int *y, int *w, int *h)
{
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30), i;
    unsigned arrows;
    *x = *y = *w = *h = 0;
    if (!TouchPad_Shown()) return;
    check_layout();
    if (cached_unit <= 0) return;
    arrows = (TouchPad_Held(TOUCH_UP) ? 1u : 0u) | (TouchPad_Held(TOUCH_DOWN) ? 2u : 0u) |
             (TouchPad_Held(TOUCH_LEFT) ? 4u : 0u) | (TouchPad_Held(TOUCH_RIGHT) ? 8u : 0u);
    if (!dpad[arrows].pixels) make_dpad(&dpad[arrows], arrows, cached_unit);
    blit(canvas, &dpad[arrows], &x0, &y0, &x1, &y1);
    for (i = TOUCH_TRIANGLE; i < TOUCH_BUTTONS; i++) {
        int held = TouchPad_Held((TouchButton)i);
        Sprite *sprite = &sprites[i][held];
        if (!sprite->pixels) {
            if (i <= TOUCH_SQUARE) make_face(sprite, (TouchButton)i, held, cached_unit);
            else make_pill(sprite, (TouchButton)i, held, cached_unit);
        }
        blit(canvas, sprite, &x0, &y0, &x1, &y1);
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > canvas->width) x1 = canvas->width;
    if (y1 > canvas->height) y1 = canvas->height;
    if (x1 <= x0 || y1 <= y0) return;
    *x = x0;
    *y = y0;
    *w = x1 - x0;
    *h = y1 - y0;
}
#endif

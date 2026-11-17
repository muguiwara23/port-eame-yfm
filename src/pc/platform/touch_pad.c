/* The on-screen controller's layout, fingers and bits (touch_pad.h). */
#include "touch_pad.h"
#include <math.h>
#include <string.h>

#define FINGERS 10

typedef enum { ZONE_NONE, ZONE_DPAD, ZONE_FACE, ZONE_BUTTON } Zone;
typedef struct {
    int used;
    uint64_t id;
    Zone zone;
    TouchButton button; /* ZONE_BUTTON's */
    uint16_t bits;
    int x, y; /* where it is now */
} Finger;

/* PS1 pad bits, as Platform_Pad reports them. */
static const uint16_t bits_of[TOUCH_BUTTONS] = {
    [TOUCH_UP] = 0x0010, [TOUCH_RIGHT] = 0x0020, [TOUCH_DOWN] = 0x0040, [TOUCH_LEFT] = 0x0080,
    [TOUCH_TRIANGLE] = 0x1000, [TOUCH_CIRCLE] = 0x2000, [TOUCH_CROSS] = 0x4000, [TOUCH_SQUARE] = 0x8000,
    [TOUCH_L2] = 0x0100, [TOUCH_R2] = 0x0200, [TOUCH_L1] = 0x0400, [TOUCH_R1] = 0x0800,
    [TOUCH_SELECT] = 0x0001, [TOUCH_START] = 0x0008};

static int mode, touched, blocked, width, height, top;
static float density; /* window pixels per dp, 0 unknown (TouchPad_SetDensity) */
static int rects[TOUCH_BUTTONS][4];
static int unit, dpad_x, dpad_y, face_x, face_y;
static Finger fingers[FINGERS];
static uint16_t held, tapped, presses;
static int menu_down, menu_taps;

static void lay_out(void);

uint16_t TouchPad_Bit(TouchButton button) { return button < TOUCH_BUTTONS ? bits_of[button] : 0; }

int TouchPad_Shown(void)
{
    return width > 0 && height > 0 && !blocked && (mode == TOUCH_PAD_SHOW || (mode == TOUCH_PAD_AUTO && touched));
}

static void place(TouchButton button, int x, int y, int w, int h)
{
    rects[button][0] = x;
    rects[button][1] = y;
    rects[button][2] = w;
    rects[button][3] = h;
}

/* A button is about a thumb's width: 13% of the window's short side, and
 * where the density is known 48 to 80 dp of it (the smallest target a
 * finger hits, and no bigger than one, so a tablet's buttons stay at the
 * edges), but never above 16% of the short side. The D-pad and the face
 * buttons sit in the middle of the left and right sides (where the 4:3
 * picture leaves black bars on a phone), the shoulders above them, SELECT
 * and START below; MENU under L2/L1, the D-pad's reach (two buttons from
 * its middle) starting below MENU's. */
#ifdef MEMORIES_IOS
/* Port/eame: the iPhone's cutouts and rounded corners (the window's safe
 * area insets, TouchPad_SetInsets). */
static int inset_left, inset_right, inset_bottom;

int TouchPad_SetInsets(int left, int right, int bottom)
{
    if (left == inset_left && right == inset_right && bottom == inset_bottom) return 0;
    inset_left = left;
    inset_right = right;
    inset_bottom = bottom;
    if (width > 0 && height > 0) lay_out();
    return TouchPad_Shown();
}

/* Port/eame's iOS layout (touch_pad_art_ios.c draws it): the D-pad and the
 * face buttons as on the other platforms, but kept clear of the notch; the
 * shoulders as pills in the top corners, L2 L1 and R1 R2; MENU a pill under
 * L2/L1; SELECT and START pills under the D-pad and the face buttons. */
static void lay_out_ios(int s, int m)
{
    int left = m, right = m, pill_w, pill_h, gap, y0, menu_y, bottom, low, bar, fit;
    if (inset_left * 2 / 3 > left) left = inset_left * 2 / 3;
    if (inset_right * 2 / 3 > right) right = inset_right * 2 / 3;
    /* On a phone the D-pad and the face buttons keep to the black bars the
     * 4:3 picture leaves (each about 2.9 buttons wide as drawn), down to
     * Apple's 44 pt target; a tablet's picture fills the screen and they go
     * over it. */
    bar = (width - (height - top) * 4 / 3) / 2;
    fit = (bar - (left > right ? left : right)) * 100 / 290;
    if (fit < s && fit >= (int)(44 * (density > 0 ? density : 1) + 0.5f)) {
        s = fit;
        m = s * 35 / 100;
    }
    pill_w = s * 6 / 5;
    pill_h = s * 11 / 20;
    gap = s / 6;
    unit = s;
    /* The middles one button and a half (the cross's arm as drawn: 1.3) in. */
    dpad_x = left + s * 13 / 10;
    face_x = width - right - s * 14 / 10;
    y0 = top + m / 2;
    place(TOUCH_L2, left, y0, pill_w, pill_h);
    place(TOUCH_L1, left + pill_w + gap, y0, pill_w, pill_h);
    place(TOUCH_R1, width - right - 2 * pill_w - gap, y0, pill_w, pill_h);
    place(TOUCH_R2, width - right - pill_w, y0, pill_w, pill_h);
    menu_y = y0 + pill_h + gap;
    place(TOUCH_MENU, left, menu_y, pill_w * 5 / 4, pill_h * 9 / 10);
    dpad_y = face_y = top + (height - top) * 56 / 100;
    if (dpad_y - s * 3 / 2 < menu_y + pill_h + gap) dpad_y = face_y = menu_y + pill_h + gap + s * 3 / 2;
    place(TOUCH_UP, dpad_x - s / 2, dpad_y - s * 3 / 2, s, s);
    place(TOUCH_DOWN, dpad_x - s / 2, dpad_y + s / 2, s, s);
    place(TOUCH_LEFT, dpad_x - s * 3 / 2, dpad_y - s / 2, s, s);
    place(TOUCH_RIGHT, dpad_x + s / 2, dpad_y - s / 2, s, s);
    place(TOUCH_TRIANGLE, face_x - s / 2, face_y - s * 3 / 2, s, s);
    place(TOUCH_CROSS, face_x - s / 2, face_y + s / 2, s, s);
    place(TOUCH_SQUARE, face_x - s * 3 / 2, face_y - s / 2, s, s);
    place(TOUCH_CIRCLE, face_x + s / 2, face_y - s / 2, s, s);
    low = inset_bottom / 2 > m / 2 ? inset_bottom / 2 : m / 2;
    bottom = height - low;
    place(TOUCH_SELECT, dpad_x - s * 3 / 4, bottom - pill_h, s * 3 / 2, pill_h);
    place(TOUCH_START, face_x - s * 3 / 4, bottom - pill_h, s * 3 / 2, pill_h);
}
#endif

static void lay_out(void)
{
    int shorter = width < height ? width : height, s = shorter * 13 / 100, m, bottom, menu_y;
    if (density > 0) {
        int least = (int)(48 * density + 0.5f), most = (int)(80 * density + 0.5f);
        if (s < least) s = least;
        if (s > most) s = most;
        if (s > shorter * 16 / 100) s = shorter * 16 / 100;
    }
    m = s * 35 / 100;
#ifdef MEMORIES_IOS
    lay_out_ios(s, m);
    return;
#endif
    unit = s;
    dpad_x = m + s * 3 / 2;
    face_x = width - m - s * 3 / 2;
    dpad_y = face_y = top + (height - top) * 56 / 100;
    menu_y = top + m / 2 + s + m / 2;
    if (dpad_y - 2 * s < menu_y + s) dpad_y = face_y = menu_y + 3 * s;
    place(TOUCH_MENU, m, menu_y + s / 4, s * 4 / 3, s / 2);
    place(TOUCH_UP, dpad_x - s / 2, dpad_y - s * 3 / 2, s, s);
    place(TOUCH_DOWN, dpad_x - s / 2, dpad_y + s / 2, s, s);
    place(TOUCH_LEFT, dpad_x - s * 3 / 2, dpad_y - s / 2, s, s);
    place(TOUCH_RIGHT, dpad_x + s / 2, dpad_y - s / 2, s, s);
    place(TOUCH_TRIANGLE, face_x - s / 2, face_y - s * 3 / 2, s, s);
    place(TOUCH_CROSS, face_x - s / 2, face_y + s / 2, s, s);
    place(TOUCH_SQUARE, face_x - s * 3 / 2, face_y - s / 2, s, s);
    place(TOUCH_CIRCLE, face_x + s / 2, face_y - s / 2, s, s);
    place(TOUCH_L2, m, top + m / 2, s, s);
    place(TOUCH_L1, m + s * 23 / 20, top + m / 2, s, s);
    place(TOUCH_R1, width - m - s * 43 / 20, top + m / 2, s, s);
    place(TOUCH_R2, width - m - s, top + m / 2, s, s);
    bottom = height - m / 2;
    place(TOUCH_SELECT, dpad_x - s, bottom - s * 3 / 5, s * 2, s / 2);
    place(TOUCH_START, face_x - s * 4 / 5, bottom - s * 4 / 5, s * 8 / 5, s * 4 / 5);
}

int TouchPad_SetDensity(float pixels_per_dp)
{
    if (pixels_per_dp == density) return 0;
    density = pixels_per_dp;
    lay_out();
    return TouchPad_Shown();
}

int TouchPad_Layout(int window_w, int window_h, int bar)
{
    if (window_w == width && window_h == height && bar == top) return 0;
    width = window_w;
    height = window_h;
    top = bar;
    lay_out();
    return TouchPad_Shown();
}

int TouchPad_Rect(TouchButton button, int *x, int *y, int *w, int *h)
{
    if (!TouchPad_Shown() || button >= TOUCH_BUTTONS) return 0;
    *x = rects[button][0];
    *y = rects[button][1];
    *w = rects[button][2];
    *h = rects[button][3];
    return 1;
}

int TouchPad_Held(TouchButton button)
{
    if (button == TOUCH_MENU) return menu_down;
    return button < TOUCH_BUTTONS && (held & bits_of[button]) != 0;
}

static long distance2(int x, int y, int cx, int cy) { return (long)(x - cx) * (x - cx) + (long)(y - cy) * (y - cy); }

/* A button's rectangle, a little larger to aim at; the short ones (SELECT,
 * START, MENU) as tall as a button to tap. */
static int in_button(TouchButton button, int x, int y)
{
    int grow = unit * 15 / 100, top = rects[button][1], h = rects[button][3];
    if (h < unit) {
        top -= (unit - h) / 2;
        h = unit;
    }
    return x >= rects[button][0] - grow && y >= top - grow && x < rects[button][0] + rects[button][2] + grow &&
           y < top + h + grow;
}

static Zone zone_at(int x, int y, TouchButton *button)
{
    TouchButton b;
    long reach = (long)unit * unit * 4; /* two buttons from the middle */
    if (distance2(x, y, dpad_x, dpad_y) <= reach) return ZONE_DPAD;
    if (distance2(x, y, face_x, face_y) <= reach) return ZONE_FACE;
    for (b = TOUCH_L1; b < TOUCH_BUTTONS; b++) {
        if (in_button(b, x, y)) {
            *button = b;
            return ZONE_BUTTON;
        }
    }
    return ZONE_NONE;
}

/* What a finger of its zone presses at x, y. The D-pad goes by the angle
 * from its middle, so the diagonals between two arrows press both; the
 * face buttons by the nearest one. */
static uint16_t press_at(const Finger *finger, int x, int y)
{
    int dx = x - dpad_x, dy = y - dpad_y, i, nearest = -1;
    long best = 0;
    double length, c, s;
    static const TouchButton face[] = {TOUCH_TRIANGLE, TOUCH_CIRCLE, TOUCH_CROSS, TOUCH_SQUARE};
    uint16_t out = 0;
    switch (finger->zone) {
    case ZONE_DPAD:
        length = sqrt((double)dx * dx + (double)dy * dy);
        if (length < unit * 0.3) return 0; /* the middle presses nothing */
        c = dx / length;
        s = dy / length;
        if (c > 0.383) out |= bits_of[TOUCH_RIGHT]; /* 67.5 degrees each way */
        if (c < -0.383) out |= bits_of[TOUCH_LEFT];
        if (s > 0.383) out |= bits_of[TOUCH_DOWN];
        if (s < -0.383) out |= bits_of[TOUCH_UP];
        return out;
    case ZONE_FACE:
        if (distance2(x, y, face_x, face_y) > (long)unit * unit * 9) return 0; /* slid well away */
        for (i = 0; i < 4; i++) {
            const int *r = rects[face[i]];
            long d = distance2(x, y, r[0] + r[2] / 2, r[1] + r[3] / 2);
            if (nearest < 0 || d < best) {
                nearest = i;
                best = d;
            }
        }
        return bits_of[face[nearest]];
    case ZONE_BUTTON:
        return in_button(finger->button, x, y) ? bits_of[finger->button] : 0;
    default:
        return 0;
    }
}

static int refresh(void)
{
    uint16_t now = 0;
    int i, menu = 0;
    for (i = 0; i < FINGERS; i++) {
        if (fingers[i].used) now |= fingers[i].bits;
        if (fingers[i].used && fingers[i].zone == ZONE_BUTTON && fingers[i].button == TOUCH_MENU)
            menu |= in_button(TOUCH_MENU, fingers[i].x, fingers[i].y);
    }
    presses |= now & ~held;
    tapped |= now & ~held;
    if (now == held && menu == menu_down) return 0;
    held = now;
    menu_down = menu;
    return 1;
}

static void release_all(void)
{
    memset(fingers, 0, sizeof(fingers));
    refresh();
}

int TouchPad_SetMode(int wanted)
{
    int was = TouchPad_Shown();
    if (wanted == mode) return 0;
    mode = wanted;
    if (!TouchPad_Shown()) release_all();
    return was != TouchPad_Shown();
}

int TouchPad_Block(int wanted)
{
    int was = TouchPad_Shown();
    wanted = !!wanted;
    if (wanted == blocked) return 0;
    blocked = wanted;
    if (blocked) release_all();
    return was != TouchPad_Shown();
}

int TouchPad_TakeMenu(void)
{
    int taps = menu_taps;
    menu_taps = 0;
    return taps != 0;
}

int TouchPad_FreeSpan(int *left, int *right)
{
    int i, grow = unit * 15 / 100;
    if (!TouchPad_Shown()) return 0;
    *left = dpad_x + 2 * unit; /* the D-pad's and the face buttons' reach (zone_at) */
    *right = face_x - 2 * unit;
    for (i = 0; i < TOUCH_BUTTONS; i++) {
        const int *r = rects[i];
        if (r[0] + r[2] / 2 < width / 2) {
            if (r[0] + r[2] + grow > *left) *left = r[0] + r[2] + grow;
        } else if (r[0] - grow < *right) {
            *right = r[0] - grow;
        }
    }
    return 1;
}

int TouchPad_OtherInput(void)
{
    if (mode != TOUCH_PAD_AUTO || !touched) return 0;
    touched = 0;
    release_all();
    return 1;
}

int TouchPad_Covers(int x, int y)
{
    TouchButton button;
    return TouchPad_Shown() && zone_at(x, y, &button) != ZONE_NONE;
}

int TouchPad_Finger(int kind, uint64_t id, int x, int y)
{
    Finger *finger = NULL;
    int i;
    for (i = 0; i < FINGERS; i++) {
        if (fingers[i].used && fingers[i].id == id) finger = &fingers[i];
    }
    if (kind == TOUCH_FINGER_DOWN) {
        TouchButton button = TOUCH_UP;
        Zone zone;
        if (mode == TOUCH_PAD_HIDE || blocked) return 0;
        if (!TouchPad_Shown()) {
            touched = 1; /* the first touch shows the pad; it presses nothing */
            return 1;
        }
        zone = zone_at(x, y, &button);
        if (zone == ZONE_NONE || finger) return 0;
        for (i = 0; i < FINGERS && !finger; i++) {
            if (!fingers[i].used) finger = &fingers[i];
        }
        if (!finger) return 0;
        finger->used = 1;
        finger->id = id;
        finger->zone = zone;
        finger->button = button;
        finger->x = x;
        finger->y = y;
        finger->bits = press_at(finger, x, y);
        if (zone == ZONE_BUTTON && button == TOUCH_MENU) menu_taps++;
        return refresh();
    }
    if (!finger) return 0;
    if (kind == TOUCH_FINGER_MOVE) {
        finger->x = x;
        finger->y = y;
        finger->bits = press_at(finger, x, y);
    } else {
        memset(finger, 0, sizeof(*finger));
    }
    return refresh();
}

uint16_t TouchPad_Update(void)
{
    uint16_t out = held | tapped;
    tapped = 0;
    return out;
}

uint16_t TouchPad_TakePresses(void)
{
    uint16_t out = presses;
    presses = 0;
    return out;
}

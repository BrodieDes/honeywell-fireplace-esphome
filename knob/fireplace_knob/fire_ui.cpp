// Fireplace knob screen, ported from the design canvas (Screen.dc.html, v2: drawn from
// a photo of the real fireplace - arched charcoal surround with fluted pilasters, copper
// lattice grilles, brick firebox, logs on a grate, stacked-stone wall and hearth).
//
// Three canvases, all in PSRAM:
//   bg   - the whole static scene, drawn once at start-up.
//   fx   - the firebox (glow, flames, pilot, embers, front logs), redrawn every frame.
//   air  - the area above the crown (warm air lines, top grille glow), redrawn only
//          while the fan is on or fading.
// Each animated frame starts by copying its rectangle out of bg, then draws on top.
// Text, the runtime ring, the level pips and the mode tabs are ordinary LVGL widgets.

#include "fire_ui.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "esp_heap_caps.h"

#define FRAME_MS 40

struct Cv { lv_obj_t *obj; lv_color_t *buf; int x, y, w, h; };
static Cv bg = {nullptr, nullptr, 30, 46, 300, 194};    // screen x 30..330, y 46..240
static Cv fx = {nullptr, nullptr, 122, 124, 116, 74};   // glass opening, y 124..198
static Cv air = {nullptr, nullptr, 116, 46, 128, 78};   // above the crown, y 46..124

static lv_obj_t *arc, *lbl_timer, *lbl_mode, *lbl_value, *lbl_hint, *pips[6], *tabs[3];
static lv_obj_t *temp_lbls[2][2];  // thermostat [target, current][bold pair], beside the fireplace
static FireView view = {};
static float shown_scale = 0, shown_glow = 0, shown_ember = 0.12f, shown_grille = 0;
static float air_opa[6] = {};
static float air_phase = 0;
static bool air_dirty = true, fx_dirty = true;
static uint32_t last_frame = 0;

static inline lv_color_t C(uint32_t hex) { return lv_color_hex(hex); }
static inline lv_coord_t R(float v) { return (lv_coord_t) lroundf(v); }

// ---------------------------------------------------------------- drawing helpers
// All take screen coordinates and translate into the canvas.

static void d_rect(Cv &c, float x, float y, float w, float h, int r, uint32_t col, lv_opa_t opa = LV_OPA_COVER)
{
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.radius = r;
  d.bg_color = C(col);
  d.bg_opa = opa;
  lv_canvas_draw_rect(c.obj, R(x - c.x), R(y - c.y), R(w), R(h), &d);
}

// LVGL 8 uses one draw mask per polygon edge and has only 16 mask slots
// (_LV_MASK_MAX_NUM): a polygon with more points overruns them and crashes. Keep to 14.
#define MAX_POLY_POINTS 14
static void d_poly(Cv &c, const float (*p)[2], int n, uint32_t col, lv_opa_t opa = LV_OPA_COVER)
{
  if (n > MAX_POLY_POINTS) n = MAX_POLY_POINTS;
  lv_point_t pts[MAX_POLY_POINTS];
  for (int i = 0; i < n; i++) pts[i] = {R(p[i][0] - c.x), R(p[i][1] - c.y)};
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = C(col);
  d.bg_opa = opa;
  lv_canvas_draw_polygon(c.obj, pts, n, &d);
}

static void d_line(Cv &c, const float (*p)[2], int n, uint32_t col, int width, lv_opa_t opa = LV_OPA_COVER,
                   bool round = false)
{
  lv_point_t pts[48];
  for (int i = 0; i < n && i < 48; i++) pts[i] = {R(p[i][0] - c.x), R(p[i][1] - c.y)};
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = C(col);
  d.width = width;
  d.opa = opa;
  d.round_start = d.round_end = round;
  lv_canvas_draw_line(c.obj, pts, n, &d);
}

static void d_seg(Cv &c, float x0, float y0, float x1, float y1, uint32_t col, int width, bool round = false)
{
  const float p[2][2] = {{x0, y0}, {x1, y1}};
  d_line(c, p, 2, col, width, LV_OPA_COVER, round);
}

static void d_ellipse(Cv &c, float cx, float cy, float rx, float ry, uint32_t col, lv_opa_t opa = LV_OPA_COVER)
{
  d_rect(c, cx - rx, cy - ry, 2 * rx, 2 * ry, LV_RADIUS_CIRCLE, col, opa);
}

// Rotates (x, y) about (cx, cy) by deg (clockwise on screen, like SVG rotate()).
static void rot(float &x, float &y, float cx, float cy, float deg)
{
  float a = deg * (float) M_PI / 180, ca = cosf(a), sa = sinf(a), dx = x - cx, dy = y - cy;
  x = cx + dx * ca - dy * sa;
  y = cy + dx * sa + dy * ca;
}

// A log: a rounded bar of length len and thickness th, centred on (cx, cy), rotated.
static void d_log(Cv &c, float cx, float cy, float len, float th, float deg, uint32_t col)
{
  float r = th / 2, hw = len / 2 - r;
  float p[4][2] = {{cx - hw, cy - r}, {cx + hw, cy - r}, {cx + hw, cy + r}, {cx - hw, cy + r}};
  for (auto &q : p) rot(q[0], q[1], cx, cy, deg);
  d_poly(c, p, 4, col);
  float ex[2] = {cx - hw, cx + hw};
  for (float x : ex) {
    float y = cy;
    rot(x, y, cx, cy, deg);
    d_ellipse(c, x, y, r, r, col);
  }
}

// Quadratic arches used by the surround, all of the form y = y0 - k * t * (1 - t).
static float arch(float x, float x0, float x1, float y0, float k)
{
  float t = (x - x0) / (x1 - x0);
  return y0 - k * t * (1 - t);
}
static float crown_top(float x) { return arch(x, 88, 272, 92, 60); }
static float crown_bot(float x) { return arch(x, 88, 272, 106, 56); }
static float grille_top(float x) { return arch(x, 116, 244, 112, 36); }
static float glass_top(float x) { return arch(x, 122, 238, 138, 40); }
static float body_top(float x) { return arch(x, 96, 264, 102, 44); }

// A curved band between two arch functions, as thin quads.
static void d_band(Cv &c, float x0, float x1, float (*top)(float), float (*bot)(float), uint32_t col,
                   lv_opa_t opa = LV_OPA_COVER)
{
  const float step = 4;
  for (float x = x0; x < x1; x += step) {
    float xe = fminf(x + step, x1);
    const float p[4][2] = {{x, top(x)}, {xe, top(xe)}, {xe, bot(xe)}, {x, bot(x)}};
    d_poly(c, p, 4, col, opa);
  }
}
static float grille_bot(float x) { return grille_top(x) + 10; }
static float flat_103(float) { return 103; }

// ---------------------------------------------------------------- flame shape

// The design's tongue path "M0 0 C -15 -18 -11 -44 0 -68 C 11 -44 15 -18 0 0", sampled.
#define TONGUE_SAMPLES 8
static float tongue_x[TONGUE_SAMPLES], tongue_y[TONGUE_SAMPLES];

static void make_tongue(void)
{
  for (int i = 0; i < TONGUE_SAMPLES; i++) {
    float t = (float) i / (TONGUE_SAMPLES - 1), u = 1 - t;
    tongue_x[i] = 3 * u * u * t * -15 + 3 * u * t * t * -11;
    tongue_y[i] = 3 * u * u * t * -18 + 3 * u * t * t * -44 + t * t * t * -68;
  }
}

static void d_tongue(Cv &c, float ox, float oy, float sx, float sy, uint32_t col, lv_opa_t opa = LV_OPA_COVER)
{
  float p[TONGUE_SAMPLES * 2 - 2][2];
  int n = 0;
  for (int i = 0; i < TONGUE_SAMPLES; i++) { p[n][0] = ox + tongue_x[i] * sx; p[n][1] = oy + tongue_y[i] * sy; n++; }
  for (int i = TONGUE_SAMPLES - 2; i > 0; i--) { p[n][0] = ox - tongue_x[i] * sx; p[n][1] = oy + tongue_y[i] * sy; n++; }
  d_poly(c, p, n, col, opa);
}

struct Tongue { float x, w, h, dur, begin; };
static const Tongue TONGUES[5] = {
  {-27, 0.75f, 0.62f, 1.35f, 0.2f},
  {-13, 0.95f, 0.86f, 0.95f, 0.5f},
  {0, 1.10f, 1.00f, 1.15f, 0.0f},
  {14, 0.95f, 0.82f, 1.05f, 0.7f},
  {28, 0.75f, 0.60f, 1.45f, 0.35f},
};

// ---------------------------------------------------------------- scene parts

static void d_lattice_band(Cv &c)
{
  d_band(c, 116, 244, grille_top, grille_bot, 0x140D0A);
  for (float x = 112; x <= 248; x += 7) {
    float y = grille_top(x);
    if (x + 5 <= 244 && x >= 116) d_seg(c, x, y + 0.5f, x + 5, grille_top(x + 5) + 9.5f, 0xA8724C, 1);
    if (x - 5 >= 116 && x <= 244) d_seg(c, x, y + 0.5f, x - 5, grille_top(x - 5) + 9.5f, 0xA8724C, 1);
  }
}

static void d_back_log(Cv &c) { d_log(c, 181, 173, 70, 10, 3, 0x4A3A2F); }

static void d_front(Cv &c)
{
  d_log(c, 180, 185, 40, 8, 4, 0x4E3C30);
  // Left front log, cut end lighter.
  d_log(c, 160, 184, 52, 12, -8, 0x5C4838);
  {
    float bark[2][4] = {{142, 182, 160, 182}, {150, 186, 176, 186}};
    for (auto &b : bark) {
      rot(b[0], b[1], 160, 184, -8); rot(b[2], b[3], 160, 184, -8);
      d_seg(c, b[0], b[1], b[2], b[3], 0x3E3026, 1);
    }
    float x = 137, y = 184; rot(x, y, 160, 184, -8);
    d_ellipse(c, x, y, 3.5f, 6, 0x8C7464);
  }
  // Right front log, angled up to the right.
  d_log(c, 201, 184, 54, 12, -16, 0x625040);
  {
    float bark[2][4] = {{182, 182, 204, 182}, {190, 186, 218, 186}};
    for (auto &b : bark) {
      rot(b[0], b[1], 201, 184, -16); rot(b[2], b[3], 201, 184, -16);
      d_seg(c, b[0], b[1], b[2], b[3], 0x3E3026, 1);
    }
    float x = 225, y = 184; rot(x, y, 201, 184, -16);
    d_ellipse(c, x, y, 3.5f, 6, 0x9A8272);
  }
  // Grate.
  d_seg(c, 140, 195, 220, 195, 0x110C0A, 3, true);
  d_seg(c, 146, 189, 146, 197, 0x110C0A, 3, true);
  d_seg(c, 214, 189, 214, 197, 0x110C0A, 3, true);
}

static void d_glass_frame(Cv &c)
{
  float p[40][2];
  int n = 0;
  p[n][0] = 122; p[n][1] = 198; n++;
  for (float x = 122; x <= 238.01f; x += 4) { p[n][0] = x; p[n][1] = glass_top(x); n++; }
  p[n][0] = 238; p[n][1] = 198; n++;
  p[n][0] = 122; p[n][1] = 198; n++;
  d_line(c, p, n, 0x1C1E21, 3);
}

static void draw_static(void)
{
  Cv &c = bg;
  lv_canvas_fill_bg(c.obj, C(0x0B0908), LV_OPA_COVER);

  // Stacked stone wall, fading out toward the screen edge.
  static const int widths[12] = {34, 22, 46, 28, 38, 52, 26, 40, 30, 44, 24, 36};
  static const uint32_t tones[4] = {0x1D1815, 0x201A17, 0x1A1513, 0x221C18};
  int k = 0;
  for (int row = 0; row * 14 + 52 < 224; row++) {
    int y = row * 14 + 52;
    for (int x = 36 - (row * 17) % 30; x < 324; k++) {
      int w = widths[k % 12];
      float cx = x + w / 2.0f, cy = y + 6;
      float d = sqrtf(powf((cx - 180) / 142, 2) + powf((cy - 148) / 98, 2));
      float f = d < 0.55f ? 1 : d > 1 ? 0 : 1 - (d - 0.55f) / 0.45f;
      if (f > 0.02f) d_rect(c, x, y, w, 12, 2, tones[(k * 7 + row) % 4], (lv_opa_t) (f * 255));
      x += w + 2;
    }
  }

  // Stone hearth.
  d_rect(c, 66, 224, 228, 11, 2, 0x4B4540);
  d_rect(c, 66, 224, 228, 1.5f, 0, 0x5E5751);
  d_rect(c, 66, 233.5f, 228, 1.5f, 0, 0x2F2B28);

  // Charcoal surround: body (arched top), fluted pilasters, bases.
  d_band(c, 96, 264, body_top, flat_103, 0x2C2E32);
  d_rect(c, 96, 102, 168, 122, 0, 0x2C2E32);
  d_rect(c, 96, 100, 18, 124, 0, 0x36383D);
  d_rect(c, 246, 100, 18, 124, 0, 0x36383D);
  const float flutes[6] = {100, 104.5f, 109, 251, 255.5f, 260};
  for (float x : flutes) d_rect(c, x - 0.6f, 108, 1.3f, 110, 0, 0x25272B);
  d_rect(c, 94, 218, 22, 6, 0, 0x3C3E43);
  d_rect(c, 244, 218, 22, 6, 0, 0x3C3E43);

  // Arched crown.
  d_band(c, 88, 272, crown_top, crown_bot, 0x3A3C41);
  {
    float p[48][2];
    int n = 0;
    for (float x = 89; x <= 271.01f; x += 6) { p[n][0] = x; p[n][1] = crown_top(x) + 0.5f; n++; }
    d_line(c, p, n, 0x4C4F55, 1);
    n = 0;
    for (float x = 100; x <= 260.01f; x += 8) { p[n][0] = x; p[n][1] = arch(x, 100, 260, 98, 52); n++; }
    d_line(c, p, n, 0x2A2C30, 2);
  }

  // Firebox interior: brick back wall, shading, side walls, floor.
  d_rect(c, 122, 116, 116, 82, 0, 0x3E281F);
  for (int r = 12; r * 9 < 198; r++) {
    float ya = r * 9 + 0.5f, yb = r * 9 + 5;
    if (ya >= 116) d_rect(c, 122, ya - 0.5f, 116, 1, 0, 0x24160F);
    if (yb >= 116 && yb < 198) d_rect(c, 122, yb - 0.5f, 116, 1, 0, 0x24160F);
    for (int j = 6; j < 240; j += 18) {
      if (j >= 122 && j < 238 && ya >= 116) d_rect(c, j - 0.5f, ya, 1, 4.5f, 0, 0x24160F);
      if (j + 9 >= 122 && j + 9 < 238 && yb < 198) d_rect(c, j + 8.5f, yb, 1, 4, 0, 0x24160F);
    }
  }
  for (int i = 0; i < 6; i++)
    d_rect(c, 122, 116 + i * 10, 116, 10, 0, 0x000000, (lv_opa_t) (153 * (1 - i / 6.0f)));
  {
    const float lw[4][2] = {{122, 124}, {142, 140}, {142, 190}, {122, 198}};
    const float rw[4][2] = {{238, 124}, {218, 140}, {218, 190}, {238, 198}};
    const float fl[4][2] = {{122, 198}, {142, 190}, {218, 190}, {238, 198}};
    d_poly(c, lw, 4, 0x000000, 97);
    d_poly(c, rw, 4, 0x000000, 97);
    d_poly(c, fl, 4, 0x1A110D);
  }
  d_back_log(c);
  d_front(c);
  // Body colour above the glass arch (thin strips, since that region is concave).
  for (float x = 120; x < 240; x += 2) {
    float y = glass_top(fminf(fmaxf(x + 1, 122), 238));
    d_rect(c, x, 112, 2, y - 112, 0, 0x2C2E32);
  }
  d_lattice_band(c);
  d_glass_frame(c);

  // Lower ledge, bottom grille, base.
  d_rect(c, 118, 200, 124, 5, 0, 0x3A3C41);
  d_rect(c, 122, 208, 116, 7, 0, 0x140D0A);
  for (float x = 122; x <= 238; x += 7) {
    if (x + 3.5f <= 238) d_seg(c, x, 208.5f, x + 3.5f, 214.5f, 0xA8724C, 1);
    if (x - 3.5f >= 122) d_seg(c, x, 208.5f, x - 3.5f, 214.5f, 0xA8724C, 1);
  }
  d_rect(c, 116, 217, 128, 7, 0, 0x34363A);
}

// Copies a canvas's rectangle out of the static background.
static void copy_bg(Cv &c)
{
  for (int row = 0; row < c.h; row++)
    memcpy(c.buf + row * c.w, bg.buf + (c.y - bg.y + row) * bg.w + (c.x - bg.x), c.w * sizeof(lv_color_t));
}

// ---------------------------------------------------------------- one frame

static float ease(float cur, float target, float k) { return cur + (target - cur) * k; }

static void draw_frame(lv_timer_t *)
{
  uint32_t now = lv_tick_get();
  float dt = last_frame ? (now - last_frame) / 1000.0f : 0;
  last_frame = now;
  float t = now / 1000.0f;

  bool lit = view.have_state && view.on;
  float k = 1 - expf(-dt * 5.0f);
  float scale_t = lit ? 0.45f + (view.flame - 1) * 0.11f : 0;
  float glow_t = lit ? 0.3f + view.flame * 0.1f : 0;
  float ember_t = lit ? 0.9f : 0.12f;
  int fan = lit ? view.fan : 0;
  float grille_t = fan ? 0.08f + fan * 0.035f : 0;
  bool settling = fabsf(shown_scale - scale_t) > 0.002f || fabsf(shown_glow - glow_t) > 0.002f ||
                  fabsf(shown_ember - ember_t) > 0.005f;
  shown_scale = ease(shown_scale, scale_t, k);
  shown_glow = ease(shown_glow, glow_t, k);
  shown_ember = ease(shown_ember, ember_t, k);

  // ---- firebox: always animating while lit; once settled while off, leave it.
  if (lit || settling || fx_dirty) {
    fx_dirty = false;
    Cv &c = fx;
    copy_bg(c);
    if (shown_glow > 0.02f) {
      const float rx[4] = {70, 52, 36, 22}, ry[4] = {44, 33, 23, 14};
      for (int i = 0; i < 4; i++)
        d_ellipse(c, 180, 186, rx[i], ry[i], 0xFF8A30, (lv_opa_t) (fminf(shown_glow, 1) * 48));
      d_back_log(c);
    }
    const float ox = 181, oy = 186, S = shown_scale * 0.78f;
    if (S > 0.02f) {
      for (const Tongue &tg : TONGUES) {
        float p = 2 * (float) M_PI * (t + tg.begin) / tg.dur;
        float fx_ = 1 - 0.06f * sinf(p) + 0.02f * sinf(2.3f * p);
        float fy = 1 + 0.07f * sinf(p) + 0.02f * sinf(1.7f * p + 1);
        float bx = ox + S * tg.x, sx = S * tg.w * fx_, sy = S * tg.h * fy;
        d_tongue(c, bx, oy, sx, sy, 0xFF7A1A);
        d_tongue(c, bx, oy, sx * 0.62f, sy * 0.65f, 0xFFB43F);
        d_tongue(c, bx, oy, sx * 0.28f, sy * 0.32f, 0xFFE9A8);
        d_ellipse(c, bx, oy - 4 * sy, 8 * sx, 6 * sy, 0x3F6BFF, 204);
      }
    }
    if (shown_scale < 0.2f)
      d_tongue(c, ox, oy, 0.208f, 0.172f, 0x4DA3FF, (lv_opa_t) (255 * (1 - shown_scale / 0.2f)));
    d_front(c);
    static const float embers[7][3] = {{150, 194, 1.2f}, {158, 196, 1}, {171, 193, 1.4f}, {184, 196, 1.1f},
                                       {196, 194, 1.3f}, {205, 196, 1}, {212, 193.5f, 1.2f}};
    for (auto &e : embers) d_ellipse(c, e[0], e[1], e[2] + 0.4f, e[2] + 0.4f, 0xFF6A2A, (lv_opa_t) (shown_ember * 255));
    const float refl[4][2] = {{152, 128}, {162, 126}, {136, 198}, {126, 198}};
    d_poly(c, refl, 4, 0xFFFFFF, 10);
  }

  // ---- warm air above the crown and the grille glow.
  bool air_active = fan > 0 || shown_grille > 0.003f;
  for (int i = 0; i < 6; i++) if (air_opa[i] > 0.01f) air_active = true;
  if (air_active || air_dirty) {
    air_dirty = false;
    float ka = 1 - expf(-dt * 4.0f);
    for (int i = 0; i < 6; i++) air_opa[i] = ease(air_opa[i], i < fan ? 0.85f : 0, ka);
    shown_grille = ease(shown_grille, grille_t, ka);
    if (fan) air_phase += dt * 16.0f / (1.8f - fan * 0.2f);
    Cv &c = air;
    copy_bg(c);
    if (shown_grille > 0.003f) d_band(c, 116, 244, grille_top, grille_bot, 0xFF9A50, (lv_opa_t) (shown_grille * 255));
    static const int slots[6] = {180, 164, 196, 148, 212, 132};
    const float len = 28, period = 16, on_len = 8;
    for (int i = 0; i < 6; i++) {
      if (air_opa[i] < 0.02f) continue;
      float x0 = slots[i], y0 = crown_top(x0) - 3;
      for (float d0 = fmodf(air_phase, period) - period; d0 < len; d0 += period) {
        float a = fmaxf(d0, 0), b = fminf(d0 + on_len, len);
        if (b - a < 1) continue;
        float p[6][2];
        for (int n = 0; n < 6; n++) {
          float s = a + (b - a) * n / 5;
          p[n][0] = x0 - 3 * sinf(s * 2 * (float) M_PI / 14);
          p[n][1] = y0 - s;
        }
        d_line(c, p, 6, 0xEAD6C2, 3, (lv_opa_t) (air_opa[i] * 255), true);
      }
    }
  }
}

// ---------------------------------------------------------------- widgets

static void make_canvas(Cv &c)
{
  c.buf = (lv_color_t *) heap_caps_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(c.w, c.h), MALLOC_CAP_SPIRAM);
  c.obj = lv_canvas_create(lv_scr_act());
  lv_canvas_set_buffer(c.obj, c.buf, c.w, c.h, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(c.obj, c.x, c.y);
}

static lv_obj_t *label(const lv_font_t *font, int y, int w, uint32_t color)
{
  lv_obj_t *l = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, C(color), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(l, w);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
  lv_label_set_text(l, "");
  return l;
}

void fire_ui_build(void)
{
  make_tongue();
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, C(0x0B0908), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  make_canvas(bg);
  draw_static();
  make_canvas(fx);
  make_canvas(air);
  copy_bg(fx);
  copy_bg(air);

  // Runtime ring (created after the canvases so it draws on top of them).
  arc = lv_arc_create(scr);
  lv_obj_set_size(arc, 353, 353);
  lv_obj_center(arc);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_range(arc, 0, 1000);
  lv_arc_set_value(arc, 0);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_pad_all(arc, 0, 0);
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
  lv_obj_set_style_arc_width(arc, 5, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, C(0x1F1916), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 5, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, C(0xFF8A2A), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

  lbl_timer = label(&lv_font_montserrat_14, 25, 230, 0xA69A91);
  lbl_mode = label(&lv_font_montserrat_14, 240, 240, 0xA69A91);
  lv_obj_set_style_text_letter_space(lbl_mode, 4, 0);
  lbl_value = label(&lv_font_montserrat_40, 256, 240, 0xF5EEE8);
  lbl_hint = label(&lv_font_montserrat_12, 300, 200, 0xA69A91);
  lv_label_set_text(lbl_hint, "turn left off " LV_SYMBOL_BULLET " right on");

  // Thermostat: target on the left of the fireplace, room temperature on the right,
  // centred between the pilasters and the screen edge, a little below the middle.
  // Bold: the built-in fonts have no bold weight, so each number is drawn twice, 1 px apart.
  for (int side = 0; side < 2; side++) {
    int dx = side ? 128 : -128;
    for (int k = 0; k < 2; k++) {
      lv_obj_t *v = label(&lv_font_montserrat_28, 150, 80, 0xF5EEE8);
      lv_obj_align(v, LV_ALIGN_TOP_MID, dx + k, 150);
      temp_lbls[side][k] = v;
    }
    lv_obj_t *cap = label(&lv_font_montserrat_12, 184, 80, 0xA69A91);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, dx, 184);
    lv_label_set_text(cap, side ? "Current" : "Target");
  }

  // Level pips: 6 bars, 16 x 5, 5 apart.
  for (int i = 0; i < 6; i++) {
    lv_obj_t *p = lv_obj_create(scr);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, 16, 5);
    lv_obj_set_pos(p, 119 + i * 21, 305);
    lv_obj_set_style_radius(p, 3, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(p, C(0x2E2622), 0);
    pips[i] = p;
  }

  // Mode tabs: FIRE FLAME FAN, the current one underlined in orange.
  lv_obj_t *row = lv_obj_create(scr);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, 200, LV_SIZE_CONTENT);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 316);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 14, 0);
  static const char *names[3] = {"FIRE", "FLAME", "FAN"};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, names[i]);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_set_style_border_side(l, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(l, 2, 0);
    lv_obj_set_style_pad_bottom(l, 2, 0);
    tabs[i] = l;
  }

  lv_timer_create(draw_frame, FRAME_MS, NULL);
}

void fire_ui_show(const FireView &v)
{
  view = v;
  char buf[64];
  int hh = v.max_runtime_hh;
  char maxtxt[8];
  if (hh & 1) snprintf(maxtxt, sizeof(maxtxt), "%d.5h", hh / 2);
  else snprintf(maxtxt, sizeof(maxtxt), "%dh", hh / 2);

  // Timer line and ring.
  uint32_t total = (uint32_t) hh * 1800;
  int ring = (v.have_state && v.on && total) ? (int) ((uint64_t) 1000 * (v.seconds_left < total ? v.seconds_left : total) / total) : 0;
  lv_arc_set_value(arc, ring);
  lv_obj_set_style_arc_opa(arc, ring ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_INDICATOR);
  if (v.status) {
    lv_label_set_text(lbl_timer, v.status);
    lv_obj_set_style_text_color(lbl_timer, C(0xFF8A2A), 0);
  } else {
    if (v.on)
      snprintf(buf, sizeof(buf), "%luh %02lum left " LV_SYMBOL_BULLET " max %s", (unsigned long) (v.seconds_left / 3600),
               (unsigned long) (v.seconds_left % 3600 / 60), maxtxt);
    else
      snprintf(buf, sizeof(buf), "Max runtime %s", maxtxt);
    lv_label_set_text(lbl_timer, buf);
    lv_obj_set_style_text_color(lbl_timer, C(0xA69A91), 0);
  }

  // Mode name, big value, pips or hint.
  static const char *names[3] = {"FIRE", "FLAME", "FAN"};
  lv_label_set_text(lbl_mode, names[v.mode]);
  lv_obj_set_style_text_color(lbl_mode, C(v.on ? 0xFF8A2A : 0xA69A91), 0);
  uint32_t value_color = 0xF5EEE8;
  if (!v.have_state) snprintf(buf, sizeof(buf), "--");
  else if (v.mode == 0) { snprintf(buf, sizeof(buf), v.on ? "ON" : "OFF"); if (v.on) value_color = 0xFF9A3C; }
  else if (v.mode == 1) snprintf(buf, sizeof(buf), "%d", v.flame);
  else if (v.fan) snprintf(buf, sizeof(buf), "%d", v.fan);
  else snprintf(buf, sizeof(buf), "Off");
  lv_label_set_text(lbl_value, buf);
  lv_obj_set_style_text_color(lbl_value, C(value_color), 0);

  // Thermostat temperatures: one decimal, "--" when unknown.
  int16_t temp_val[2] = {v.target_c10, v.current_c10};
  for (int i = 0; i < 2; i++) {
    int t = temp_val[i];
    if (!v.have_state || t == INT16_MIN) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%s%d.%d\xC2\xB0", t < 0 ? "-" : "", abs(t) / 10, abs(t) % 10);
    lv_label_set_text(temp_lbls[i][0], buf);
    lv_label_set_text(temp_lbls[i][1], buf);
  }

  bool show_pips = v.have_state && v.mode != 0;
  int level = v.mode == 1 ? v.flame : v.fan;
  uint32_t lit = v.mode == 1 ? 0xFF8A2A : 0xA8CDEA;
  for (int i = 0; i < 6; i++) {
    if (show_pips) lv_obj_clear_flag(pips[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(pips[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(pips[i], C(i < level ? lit : 0x2E2622), 0);
  }
  if (v.have_state && v.mode == 0) lv_obj_clear_flag(lbl_hint, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(lbl_hint, LV_OBJ_FLAG_HIDDEN);

  for (int i = 0; i < 3; i++) {
    bool cur = i == v.mode;
    lv_obj_set_style_text_color(tabs[i], C(cur ? 0xF5EEE8 : 0x8E837B), 0);
    lv_obj_set_style_border_color(tabs[i], C(0xFF8A2A), 0);
    lv_obj_set_style_border_opa(tabs[i], cur ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  }
}

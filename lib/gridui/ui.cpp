// Main screen: header, animated scene and mix bar, plus the region picker.
// Coordinates follow the design mockup (480 x 272; scene is 480 x 160 starting at y = 40).
#include "gridui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "raster.h"
#include "ui_common.h"


namespace gridui {

using namespace gridcore;

struct SkyColors { uint32_t top, bottom, hill, ground, text, sub, smoke; };
static const SkyColors SKIES[3] = {
  {0x6EC3F0, 0xCFEFFC, 0x8CC071, 0x69A156, 0x0B1B2B, 0x1E3346, 0x9AA0A8},   // day
  {0x3B3768, 0xF2A06B, 0x6C7F4E, 0x4D6B43, 0xFFFFFF, 0xF3E9F2, 0x6F6A72},   // dusk
  {0x0A1430, 0x263A6B, 0x2B4A33, 0x1E3826, 0xFFFFFF, 0xDCE4F2, 0x5C6270},   // night
};

// A line whose object covers only its own small box (so redraws stay small).
struct Spoke {
  lv_obj_t *obj;
  lv_point_precise_t pts[2];
  float cx, cy, len;   // in the parent's coordinates
};
static void spoke_init(Spoke &s, lv_obj_t *parent, float cx, float cy, float len, float width, uint32_t color) {
  s.cx = cx; s.cy = cy; s.len = len;
  s.obj = lv_line_create(parent);
  const int pad = (int)ceilf(len + width);
  lv_obj_set_pos(s.obj, (int)floorf(cx) - pad, (int)floorf(cy) - pad);
  lv_obj_set_size(s.obj, pad * 2 + 1, pad * 2 + 1);
  lv_obj_set_style_line_width(s.obj, (int32_t)lroundf(width), 0);
  lv_obj_set_style_line_color(s.obj, hex(color), 0);
  lv_obj_set_style_line_rounded(s.obj, true, 0);
}
static void spoke_set(Spoke &s, float deg, float inner = 0) {
  const int pad = (int)ceilf(s.len + 3);
  const float ox = s.cx - (floorf(s.cx) - pad), oy = s.cy - (floorf(s.cy) - pad);
  const float a = deg * (float)M_PI / 180;
  s.pts[0].x = ox + sinf(a) * inner; s.pts[0].y = oy - cosf(a) * inner;
  s.pts[1].x = ox + sinf(a) * s.len; s.pts[1].y = oy - cosf(a) * s.len;
  lv_line_set_points(s.obj, s.pts, 2);
}

// ---------------------------------------------------------------- widgets

static Callbacks cbs;
static Model cur;
static bool bar_fat = true;

static lv_obj_t *scr;
// header
static lv_obj_t *hdr_left, *lbl_region, *lbl_clock, *clock_dot, *chev;
static lv_point_precise_t chev_pts[3] = {{0, 0}, {4, 4}, {8, 0}};
static lv_obj_t *rate_row, *lbl_your, *lbl_rate, *lbl_unit, *pill_period, *lbl_noplan;
// scene
static lv_obj_t *scene, *static_img, *lbl_pct, *lbl_pct_sub, *pill_advice;
static Raster art;
static lv_obj_t *stars[8], *sun_disc, *moon_disc, *moon_shade;
static Spoke rays[8];
static lv_obj_t *smoke_box, *puffs[6], *vap_box, *vapours[3], *flame_img, *water_box, *streaks[5], *foams[3];
static lv_point_precise_t streak_pts[2] = {{0, 0}, {0, 46}};
static lv_obj_t *cells[3], *glints[4];
static lv_point_precise_t glint_pts[4][2];
static Spoke blades[3][3];
static lv_obj_t *hubs[3];
static Raster flame_art;
// bottom
static lv_obj_t *bar, *chips, *thin_bar, *details_btn;
// picker
static lv_obj_t *picker, *opt_btn[2], *opt_title[2], *opt_sub[2], *opt_freq[2], *opt_ago[2], *opt_ring[2], *opt_dot[2], *opt_fresh[2];

// animation state
static float rotor_deg[3] = {0, 40, 80}, rotor_period[3] = {4, 4, 4}, ray_deg = 0;
static bool rotors_on = true;
static uint32_t last_tick = 0;

// ---------------------------------------------------------------- scene artwork

static float ghost(Source s) { return cur.has_data && cur.snap.mw[s] > 0 ? 1.0f : 0.2f; }

// Transmission line for imports (Britain only): wires come in over the left hill, across two pylons.
// Each wire is three sagging spans (start, control, end); sparks run along the upper wire.
struct Span { Pt a, c, b; };
// Big pylon at x 36, smaller one further back at x 100; the last span sags down behind the crest.
static const Span WIRE_TOP[3] = {{{-6, 82}, {12, 88}, {28, 76}}, {{44, 76}, {70, 90}, {95, 92}}, {{105, 92}, {124, 99}, {142, 118}}};
static const Span WIRE_LOW[3] = {{{-6, 88}, {14, 94}, {30, 80}}, {{42, 80}, {70, 95}, {97, 95}}, {{103, 95}, {124, 103}, {142, 121}}};
// Height of the left hill's top edge (same curve as in draw_static_art)
static float hill_y(float x) {
  const float t = x / 180, u = 1 - t;
  return 118 * u * u + 200 * t * u + 116 * t * t;
}
static Pt span_at(const Span &s, float t) {
  const float u = 1 - t;
  return {u * u * s.a.x + 2 * u * t * s.c.x + t * t * s.b.x, u * u * s.a.y + 2 * u * t * s.c.y + t * t * s.b.y};
}
static void draw_wire(const Span *w, uint32_t color) {
  for (int k = 0; k < 3; k++) {
    Path p;
    p.move(w[k].a.x, w[k].a.y).quad(w[k].c.x, w[k].c.y, w[k].b.x, w[k].b.y);
    art.stroke(p.p, p.n, false, 1, color);
  }
}
static void draw_pylon(float x, float top, float base, float half_base, float arm, uint32_t steel) {
  const Pt l[] = {{x - half_base, base}, {x - 1.5f, top}}, r[] = {{x + half_base, base}, {x + 1.5f, top}};
  art.stroke(l, 2, false, 1.2f, steel);
  art.stroke(r, 2, false, 1.2f, steel);
  // lattice: zigzag braces between the legs
  const int n = 5;
  for (int i = 0; i < n; i++) {
    const float y0 = base - (base - top) * i / n, y1 = base - (base - top) * (i + 1) / n;
    const float w0 = half_base - (half_base - 1.5f) * i / n, w1 = half_base - (half_base - 1.5f) * (i + 1) / n;
    const Pt br[] = {{x - w0, y0}, {x + w1, y1}};
    art.stroke(br, 2, false, 0.8f, steel);
  }
  // two cross-arms with pink insulators at the tips
  for (int k = 0; k < 2; k++) {
    const float y = top + 4 + k * (arm * 0.55f), a = arm - k * 2;
    const Pt bar[] = {{x - a, y}, {x + a, y}};
    art.stroke(bar, 2, false, 1.2f, steel);
    art.rect(x - a - 1, y, 2, 3, 0xE07BA0);
    art.rect(x + a - 1, y, 2, 3, 0xE07BA0);
  }
}

static void draw_static_art() {
  const SkyColors &sky = SKIES[cur.sky];
  art.clear();
  art.opa = 1;
  Path p;

  // transmission line (imports, Britain only). Drawn before the hill, so the pylons stand behind
  // it and the line drops away over the crest instead of just stopping.
  if (cur.show_imports) {
    art.opa = ghost(IMPORTS);
    const uint32_t steel = cur.sky == SKY_DAY ? 0x4A525C : 0x9AA3AD;
    draw_wire(WIRE_TOP, steel);
    draw_wire(WIRE_LOW, steel);
    draw_pylon(36, 72, 128, 8, 8, steel);
    draw_pylon(100, 88, 120, 4, 5, steel);
    art.opa = 1;
  }

  // hills
  p.move(0, 118).quad(90, 100, 180, 116).quad(270, 132, 360, 112).quad(450, 92, 480, 116).line(480, 160).line(0, 160);
  art.fill(p, sky.hill);

  // coal plant
  art.opa = ghost(COAL);
  art.rect(222, 100, 54, 30, 0x5B524C);
  const Pt roof[] = {{222, 100}, {222, 92}, {232, 100}, {232, 92}, {242, 100}, {242, 92}, {252, 100}};
  art.fill(roof, 7, 0x4A423D);
  art.rect(228, 110, 6, 6, 0xF2C46B);
  art.rect(240, 110, 6, 6, 0xF2C46B);
  art.rect(252, 110, 6, 6, 0xF2C46B);
  art.rect(244, 74, 8, 28, 0x7A6E66);
  art.rect(260, 60, 10, 42, 0x7A6E66);
  art.rect(260, 66, 10, 4, 0xC8553D);

  // gas plant (orange door and trim)
  art.opa = ghost(GAS);
  art.rect(286, 108, 36, 22, 0x6E7781);
  art.rect(286, 106, 36, 3, 0xF07A3A);
  art.rect(292, 114, 10, 16, 0xF07A3A);
  art.rect(310, 84, 6, 26, 0x8A939C);

  // nuclear cooling tower (green stripe)
  art.opa = ghost(NUCLEAR);
  p.move(336, 130).quad(348, 106, 344, 86).line(370, 86).quad(366, 106, 378, 130);
  art.fill(p, 0xD9E1E7);
  art.stroke(p.p, p.n, true, 1, 0xB6C2CC);
  art.ellipse(357, 86, 13, 2.5f, 0xAEBAC4);
  const Pt stripe[] = {{344.7f, 93}, {369.3f, 93}, {369.6f, 98}, {344.4f, 98}};
  art.fill(stripe, 4, 0x3BB273);

  // hydro dam
  art.opa = ghost(HYDRO);
  const Pt bank_l[] = {{380, 130}, {382, 104}, {389, 93}, {397, 97}, {398, 130}};
  const Pt bank_r[] = {{424, 130}, {425, 97}, {433, 91}, {441, 101}, {442, 130}};
  art.fill(bank_l, 5, 0x7D8A7A);
  art.fill(bank_r, 5, 0x7D8A7A);
  const Pt lake[] = {{392, 99}, {393, 94}, {429, 94}, {430, 99}};
  art.fill(lake, 4, 0x2E6FD8);
  const Pt wall[] = {{394, 100}, {428, 100}, {431, 130}, {391, 130}};
  art.fill(wall, 4, 0xB4BDC4);
  art.rect(392, 97.5f, 38, 3, 0xD5DBE0, 1, 1);
  const Pt e1[] = {{397.5f, 101}, {396, 130}}, e2[] = {{424.5f, 101}, {426, 130}};
  art.stroke(e1, 2, false, 1, 0x9AA5AE);
  art.stroke(e2, 2, false, 1, 0x9AA5AE);
  if (cur.snap.mw[HYDRO] > 0) art.rect(401, 100, 20, 30, 0x4F9BF0);

  // battery box (cells are animated separately)
  art.opa = ghost(BATTERY);
  art.rect(455, 96, 10, 4, 0x9B7BEA);
  art.rect(446, 100, 28, 30, 0x1B1F27, 1, 3);
  art.rect_outline(446, 100, 28, 30, 2, 0x9B7BEA, 3);

  // ground
  art.opa = 1;
  p.move(0, 128).quad(60, 120, 120, 128).quad(180, 136, 240, 128).quad(300, 120, 360, 126).quad(420, 132, 480, 128).line(480, 160).line(0, 160);
  art.fill(p, sky.ground);

  // river below the dam
  art.opa = ghost(HYDRO);
  p.move(400, 130).line(422, 130).quad(431, 142, 444, 160).line(410, 160).quad(407, 142, 400, 130);
  art.fill(p, 0x3F86DB);

  // solar panels (yellow frames)
  art.opa = ghost(SOLAR);
  for (int i = 0; i < 4; i++) {
    const float x = 16 + i * 22;
    const Pt panel[] = {{x, 130}, {x + 17, 130}, {x + 21, 120}, {x + 4, 120}};
    art.fill(panel, 4, 0x1E3A5F);
    art.stroke(panel, 4, true, 1.3f, 0xF5B800);
  }

  // wind towers (cyan bands near the base)
  art.opa = ghost(WIND);
  const float tx[3] = {156, 184, 210}, ty[3] = {72, 56, 80}, tw[3] = {2.5f, 2.5f, 2};
  for (int i = 0; i < 3; i++) {
    const Pt tower[] = {{tx[i] - 1, ty[i]}, {tx[i] + 1, ty[i]}, {tx[i] + tw[i], 131}, {tx[i] - tw[i], 131}};
    art.fill(tower, 4, 0xE9EEF2);
    art.rect(tx[i] - 3, 122, 6, 3, 0x4FB3D9);
  }
  art.opa = 1;

  lv_image_cache_drop(art.image());
  lv_image_set_src(static_img, art.image());
  lv_obj_invalidate(static_img);
}

// ---------------------------------------------------------------- animations

struct Puff { float x, y, r, dx, dy, grow, phase; };
static Puff puff_cfg[6] = {
  {265, 55, 6, 18, -46, 1.9f, 0}, {265, 55, 6, 18, -46, 1.9f, 333}, {265, 55, 6, 18, -46, 1.9f, 667},
  {248, 70, 5, 18, -46, 1.9f, 167}, {248, 70, 5, 18, -46, 1.9f, 500}, {248, 70, 5, 18, -46, 1.9f, 833},
};
static Puff vap_cfg[3] = {{357, 80, 7, -6, -40, 2.1f, 0}, {357, 80, 7, -6, -40, 2.1f, 333}, {357, 80, 7, -6, -40, 2.1f, 667}};

static void puff_exec(void *var, int32_t v) {
  lv_obj_t *o = (lv_obj_t *)var;
  const Puff *p = (const Puff *)lv_obj_get_user_data(o);
  const float t = (float)(((int)v + (int)p->phase) % 1000) / 1000.0f;
  const float scale = 0.4f + (p->grow - 0.4f) * t;
  const float r = p->r * scale;
  const float peak = p->grow > 2 ? 0.2f : 0.15f;
  const float a = t < peak ? t / peak * 0.9f : 0.9f * (1 - (t - peak) / (1 - peak));
  const int d = (int)lroundf(r * 2);
  lv_obj_set_size(o, d, d);
  lv_obj_set_pos(o, (int)lroundf(p->x + p->dx * t - r), (int)lroundf(p->y + p->dy * t - r));
  lv_obj_set_style_opa(o, (lv_opa_t)(a * 255), 0);
}

static void anim(lv_obj_t *obj, lv_anim_exec_xcb_t cb, uint32_t ms) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, cb);
  lv_anim_set_values(&a, 0, 999);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_linear);
  lv_anim_start(&a);
}

static int phase_of(lv_obj_t *o) { return (int)(intptr_t)lv_obj_get_user_data(o); }

static void flicker_exec(void *var, int32_t v) {
  const float t = v / 1000.0f * 4;   // 4 keyframes
  static const float sx[5] = {1, 0.9f, 1.05f, 0.95f, 1}, sy[5] = {1, 1.25f, 0.85f, 1.15f, 1};
  const int i = (int)t;
  const float f = t - i;
  lv_obj_t *o = (lv_obj_t *)var;
  lv_image_set_scale_x(o, (uint32_t)(256 * (sx[i] + (sx[i + 1] - sx[i]) * f)));
  lv_image_set_scale_y(o, (uint32_t)(256 * (sy[i] + (sy[i + 1] - sy[i]) * f)));
}

static void streak_exec(void *var, int32_t v) {
  lv_obj_set_y((lv_obj_t *)var, -8 + (int)(((v + phase_of((lv_obj_t *)var)) % 1000) * 8 / 1000));
}

static void foam_exec(void *var, int32_t v) {
  lv_obj_t *o = (lv_obj_t *)var;
  const float t = ((v + phase_of(o)) % 1000) / 1000.0f;
  const float k = 0.5f - 0.5f * cosf(t * 2 * (float)M_PI);   // 0..1..0
  lv_obj_set_style_transform_scale(o, (int32_t)(256 * (0.75f + 0.5f * k)), 0);
  lv_obj_set_style_opa(o, (lv_opa_t)(255 * (0.55f + 0.45f * k)), 0);
}

static void pulse_exec(void *var, int32_t v) {   // cells and stars
  lv_obj_t *o = (lv_obj_t *)var;
  const float t = ((v + phase_of(o)) % 1000) / 1000.0f;
  const float k = 0.5f - 0.5f * cosf(t * 2 * (float)M_PI);
  lv_obj_set_style_opa(o, (lv_opa_t)(255 * (0.3f + 0.7f * k)), 0);
}

// Sparks travelling along the upper wire while power is coming in
static lv_obj_t *sparks[3];
static void spark_exec(void *var, int32_t v) {
  lv_obj_t *o = (lv_obj_t *)var;
  const float t = ((v + phase_of(o)) % 1000) / 1000.0f * 3;   // 0..3 across the three spans
  const int k = t >= 3 ? 2 : (int)t;
  const Pt p = span_at(WIRE_TOP[k], t - k);
  lv_obj_set_pos(o, (int)(p.x - 2), (int)(p.y - 2));
  const float f = t - k;   // brighter mid-span, a little flicker; gone once the wire drops behind the hill
  const bool behind = p.y > hill_y(p.x) - 1;
  lv_obj_set_style_opa(o, behind ? 0 : (lv_opa_t)(150 + 105 * sinf(f * 3.1416f) * (0.7f + 0.3f * sinf(v * 0.07f))), 0);
}

static void glint_exec(void *var, int32_t v) {
  lv_obj_t *o = (lv_obj_t *)var;
  const float t = ((v + phase_of(o)) % 1000) / 1000.0f;
  float a = 0;
  if (t > 0.7f && t <= 0.8f) a = (t - 0.7f) / 0.1f * 0.95f;
  else if (t > 0.8f) a = 0.95f * (1 - (t - 0.8f) / 0.2f);
  lv_obj_set_style_opa(o, (lv_opa_t)(255 * a), 0);
}

static void spin_timer(lv_timer_t *) {
  const uint32_t now = lv_tick_get();
  const float dt = lv_tick_diff(now, last_tick) / 1000.0f;
  last_tick = now;
  if (screens_open()) return;   // covered by the detail or tariff screen
  if (rotors_on) {
    for (int r = 0; r < 3; r++) {
      rotor_deg[r] = fmodf(rotor_deg[r] + 360 * dt / rotor_period[r], 360);
      for (int b = 0; b < 3; b++) spoke_set(blades[r][b], rotor_deg[r] + b * 120);
    }
  }
  if (!lv_obj_is_hidden(sun_disc)) {
    ray_deg = fmodf(ray_deg + 360 * dt / 18, 360);
    for (int i = 0; i < 8; i++) spoke_set(rays[i], ray_deg + i * 45, 19);
  }
}

// ---------------------------------------------------------------- build

static void on_header(lv_event_t *) { lv_obj_set_hidden(picker, false); }
static void on_close(lv_event_t *) { lv_obj_set_hidden(picker, true); }
static void on_option(lv_event_t *e) {
  const bool country = lv_event_get_user_data(e) != nullptr;
  lv_obj_set_hidden(picker, true);
  if (cbs.view_changed) cbs.view_changed(country);
}
static void on_bar(lv_event_t *) {
  bar_fat = !bar_fat;
  update(cur);
}
static void on_scene(lv_event_t *) {
  show_detail();
  if (cbs.scene_tapped) cbs.scene_tapped();
}
static void on_rate(lv_event_t *) { show_tariff(); }
static void on_details(lv_event_t *) { show_detail(); }


static void build_header() {
  hdr_left = row(scr, 6);
  lv_obj_set_pos(hdr_left, 4, 0);
  lv_obj_set_height(hdr_left, 40);
  lv_obj_set_style_pad_hor(hdr_left, 10, 0);
  clickable(hdr_left, on_header);
  lbl_region = label(hdr_left, &gm_grotesk_15, 0xFFFFFF);
  chev = lv_line_create(hdr_left);
  lv_line_set_points(chev, chev_pts, 3);
  lv_obj_set_style_line_width(chev, 2, 0);
  lv_obj_set_style_line_color(chev, hex(MUTED), 0);
  lv_obj_set_style_line_rounded(chev, true, 0);
  lbl_clock = label(hdr_left, &gm_inter_12, MUTED);
  clock_dot = circle(hdr_left, 0, 0, 3.5f, 0x3BB273);   // how up to date the data is

  lv_obj_t *right = lv_obj_create(scr);
  lv_obj_remove_style_all(right);
  lv_obj_set_scrollable(right, false);
  clickable(right, on_rate);   // opens the tariff screen
  lv_obj_set_ext_click_area(right, 8);
  lv_obj_set_size(right, LV_SIZE_CONTENT, 40);
  lv_obj_align(right, LV_ALIGN_TOP_RIGHT, -12, 0);
  lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_row(right, 1, 0);

  rate_row = row(right, 5);
  lbl_your = label_tr(rate_row, &gm_inter_11, MUTED, "Your rate");
  lbl_rate = label(rate_row, &gm_grotesk_15, 0xFFFFFF);
  lbl_unit = label(rate_row, &gm_inter_11, 0xC9D0D9, "/kWh");
  pill_period = pill(rate_row, &gm_inter_11);
  lbl_noplan = label_tr(right, &gm_inter_11, MUTED, "Add your power plan at gridmix.local");

}

static void build_scene() {
  scene = box(scr, 0, 40, 480, 160, 0x6EC3F0);
  lv_obj_set_style_bg_grad_dir(scene, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_clip_corner(scene, false, 0);
  clickable(scene, on_scene);

  for (int i = 0; i < 8; i++) {
    static const float sx[8] = {232, 282, 330, 372, 392, 470, 256, 306}, sy[8] = {14, 30, 12, 40, 16, 56, 48, 52};
    stars[i] = circle(scene, sx[i], sy[i], 1.2f, 0xFFFFFF);
    lv_obj_set_user_data(stars[i], (void *)(intptr_t)(i * 137 % 1000));
    anim(stars[i], pulse_exec, 2400);
  }
  for (int i = 0; i < 8; i++) spoke_init(rays[i], scene, 432, 30, 25, 2.5f, 0xFFD54A);
  sun_disc = circle(scene, 432, 30, 13, 0xFFD54A);
  moon_disc = circle(scene, 432, 30, 11, 0xECE7D2);
  moon_shade = circle(scene, 438, 25, 10, 0x0A1430);

  art.init(480, 160);
  static_img = lv_image_create(scene);
  lv_obj_set_pos(static_img, 0, 0);

  // coal smoke and nuclear vapour
  smoke_box = box(scene, 0, 0, 480, 160, 0, LV_OPA_TRANSP);
  for (int i = 0; i < 6; i++) {
    puffs[i] = circle(smoke_box, puff_cfg[i].x, puff_cfg[i].y, puff_cfg[i].r, 0x9AA0A8);
    lv_obj_set_user_data(puffs[i], &puff_cfg[i]);
    anim(puffs[i], puff_exec, 3000);
  }
  vap_box = box(scene, 0, 0, 480, 160, 0, LV_OPA_TRANSP);
  for (int i = 0; i < 3; i++) {
    vapours[i] = circle(vap_box, vap_cfg[i].x, vap_cfg[i].y, vap_cfg[i].r, 0xF4F7FA);
    lv_obj_set_user_data(vapours[i], &vap_cfg[i]);
    anim(vapours[i], puff_exec, 3600);
  }

  // gas flame: drawn once, then squashed and stretched
  flame_art.init(14, 22);
  flame_art.ox = 306; flame_art.oy = 63;
  Path f;
  f.move(313, 84).quad(307, 78, 310, 72).line(313, 64).line(316, 72).quad(319, 78, 313, 84);
  flame_art.fill(f, 0xFF8A3D);
  f.move(313, 84).quad(310, 80, 311, 76).line(313, 71).line(315, 76).quad(316, 80, 313, 84);
  flame_art.fill(f, 0xFFD166);
  flame_img = lv_image_create(scene);
  lv_image_set_src(flame_img, flame_art.image());
  lv_obj_set_pos(flame_img, 306, 63);
  lv_image_set_pivot(flame_img, 7, 21);
  anim(flame_img, flicker_exec, 450);

  // water pouring over the dam, clipped to the spillway
  water_box = box(scene, 401, 100, 20, 30, 0, LV_OPA_TRANSP);
  static const float wx[5] = {3, 7, 11, 15, 18};
  for (int i = 0; i < 5; i++) {
    streaks[i] = lv_line_create(water_box);
    lv_line_set_points(streaks[i], streak_pts, 2);
    lv_obj_set_pos(streaks[i], (int)wx[i], -8);
    lv_obj_set_style_line_width(streaks[i], 1, 0);
    lv_obj_set_style_line_color(streaks[i], hex(0xDDEEFF), 0);
    lv_obj_set_style_line_dash_width(streaks[i], 5, 0);
    lv_obj_set_style_line_dash_gap(streaks[i], 3, 0);
    lv_obj_set_user_data(streaks[i], (void *)(intptr_t)(i * 290 % 1000));
    anim(streaks[i], streak_exec, 500);
  }
  static const float fx[3] = {404, 411, 418}, fr[3] = {3.5f, 4, 3.5f};
  for (int i = 0; i < 3; i++) {
    foams[i] = circle(scene, fx[i], 130, fr[i], 0xEEF7FF);
    lv_obj_set_user_data(foams[i], (void *)(intptr_t)(i * 333));
    anim(foams[i], foam_exec, 900);
  }

  // battery cells
  for (int i = 0; i < 3; i++) {
    cells[i] = box(scene, 450, 122 - i * 8, 20, 5, 0x9B7BEA);
    lv_obj_set_style_radius(cells[i], 1, 0);
    lv_obj_set_user_data(cells[i], (void *)(intptr_t)(i * 125));
    anim(cells[i], pulse_exec, 1600);
  }

  // solar glints
  for (int i = 0; i < 4; i++) {
    const float x = 23 + i * 22;
    glints[i] = lv_line_create(scene);   // (sparks for the transmission line are made after this loop)
    glint_pts[i][0] = {0, 7};
    glint_pts[i][1] = {8, 0};
    lv_line_set_points(glints[i], glint_pts[i], 2);
    lv_obj_set_pos(glints[i], (int)x, 121);
    lv_obj_set_style_line_width(glints[i], 2, 0);
    lv_obj_set_style_line_color(glints[i], hex(0xFFFFFF), 0);
    lv_obj_set_style_line_rounded(glints[i], true, 0);
    lv_obj_set_user_data(glints[i], (void *)(intptr_t)(i * 250));
    anim(glints[i], glint_exec, 4000);
  }  for (int i = 0; i < 3; i++) {
    sparks[i] = circle(scene, 0, 0, 2, 0xFFE3EE);
    lv_obj_set_style_shadow_width(sparks[i], 6, 0);
    lv_obj_set_style_shadow_color(sparks[i], hex(0xE07BA0), 0);
    lv_obj_set_style_shadow_opa(sparks[i], LV_OPA_80, 0);
    lv_obj_set_user_data(sparks[i], (void *)(intptr_t)(i * 333));
    anim(sparks[i], spark_exec, 2400);
  }


  // wind turbine blades and hubs
  static const float hx[3] = {156, 184, 210}, hy[3] = {72, 56, 80}, bl[3] = {20, 24, 16}, bw[3] = {2.6f, 2.8f, 2.4f};
  for (int r = 0; r < 3; r++) {
    for (int b = 0; b < 3; b++) {
      spoke_init(blades[r][b], scene, hx[r], hy[r], bl[r], bw[r], 0xF7F9FB);
      spoke_set(blades[r][b], rotor_deg[r] + b * 120);
    }
    hubs[r] = circle(scene, hx[r], hy[r], 3.2f, 0x4FB3D9);
    lv_obj_set_style_border_width(hubs[r], 1, 0);
    lv_obj_set_style_border_color(hubs[r], hex(0xF7F9FB), 0);
  }

  // text over the scene
  lbl_pct = label(scene, &gm_grotesk_42, 0x0B1B2B);
  lv_obj_set_pos(lbl_pct, 14, -2);
  lbl_pct_sub = label_tr(scene, &gm_inter_12, 0x1E3346, "renewable right now");
  lv_obj_set_pos(lbl_pct_sub, 15, 46);
  pill_advice = pill(scene, &gm_inter_11);
  lv_obj_set_style_radius(pill_advice, 10, 0);
  lv_obj_set_style_pad_hor(pill_advice, 9, 0);
  lv_obj_set_style_pad_ver(pill_advice, 3, 0);
  lv_obj_align(pill_advice, LV_ALIGN_BOTTOM_LEFT, 14, -6);

  last_tick = lv_tick_get();
  lv_timer_create(spin_timer, 33, nullptr);
}

static void build_bottom() {
  bar = box(scr, 16, 208, 448, 32, 0x1B1F27);
  lv_obj_set_style_radius(bar, 8, 0);
  lv_obj_set_style_clip_corner(bar, true, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(bar, 2, 0);
  clickable(bar, on_bar);

  thin_bar = box(scr, 16, 212, 448, 10, 0x1B1F27);
  lv_obj_set_style_radius(thin_bar, 5, 0);
  lv_obj_set_style_clip_corner(thin_bar, true, 0);
  lv_obj_set_flex_flow(thin_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(thin_bar, 2, 0);
  lv_obj_set_ext_click_area(thin_bar, 8);
  clickable(thin_bar, on_bar);

  // "Details ›" in the bottom-right corner (tapping the scene does the same)
  details_btn = box(scr, 380, 240, 100, 32, 0, LV_OPA_TRANSP);
  clickable(details_btn, on_details);
  lv_obj_t *dl = label_tr(details_btn, &gm_inter_12, TEXT, "Details \xE2\x80\xBA");
  lv_obj_align(dl, LV_ALIGN_RIGHT_MID, -16, 0);

  chips = row(scr, 12);
  lv_obj_set_pos(chips, 16, 244);
  lv_obj_set_height(chips, 20);
}

static void build_picker() {
  picker = box(scr, 0, 40, 480, 232, BG);
  lv_obj_set_hidden(picker, true);
  lv_obj_set_clickable(picker, true);   // swallow taps behind it
  lv_obj_set_style_border_side(picker, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(picker, 1, 0);
  lv_obj_set_style_border_color(picker, hex(0x1F242D), 0);
  lv_obj_t *t = label_tr(picker, &gm_inter_12, MUTED, "Show the grid for");
  lv_obj_set_pos(t, 14, 12);
  lv_obj_t *close = box(picker, 426, 2, 44, 36, 0, LV_OPA_TRANSP);
  clickable(close, on_close);
  static lv_point_precise_t xs[2][2] = {{{0, 0}, {10, 10}}, {{10, 0}, {0, 10}}};
  for (auto *pts : xs) {
    lv_obj_t *l = lv_line_create(close);
    lv_line_set_points(l, pts, 2);
    lv_obj_set_pos(l, 17, 13);
    lv_obj_set_style_line_width(l, 2, 0);
    lv_obj_set_style_line_color(l, hex(0xC9D0D9), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
  }
  for (int i = 0; i < 2; i++) {
    lv_obj_t *b = box(picker, 16, 40 + i * 62, 448, 54, 0x13171D);
    opt_btn[i] = b;
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    clickable(b, on_option, i ? (void *)1 : nullptr);
    opt_ring[i] = circle(b, 21, 27, 8, 0x13171D);
    lv_obj_set_style_border_width(opt_ring[i], 2, 0);
    opt_dot[i] = circle(b, 21, 27, 4, 0x4FB3D9);
    opt_title[i] = label(b, &gm_inter_13, 0xFFFFFF);
    lv_obj_set_pos(opt_title[i], 40, 9);
    opt_sub[i] = label(b, &gm_inter_11, MUTED);
    lv_obj_set_pos(opt_sub[i], 40, 29);
    opt_freq[i] = label(b, &gm_inter_11, TEXT);
    lv_obj_align(opt_freq[i], LV_ALIGN_TOP_RIGHT, -12, 10);
    opt_ago[i] = label(b, &gm_inter_11, MUTED);
    lv_obj_align(opt_ago[i], LV_ALIGN_TOP_RIGHT, -12, 29);
    opt_fresh[i] = circle(b, 0, 0, 3, 0x3BB273);
  }
}

// Fixed labels and the English they were made from, so a change of language can re-label them
struct Translated { lv_obj_t *obj; const char *en; };
static Translated translated[48];
static int translated_n = 0;
void remember_translation(lv_obj_t *l, const char *en) {
  if (translated_n < (int)(sizeof translated / sizeof translated[0])) translated[translated_n++] = {l, en};
}

void set_language(Lang l) {
  if (l == current_lang()) return;
  set_lang(l);
  for (int i = 0; i < translated_n; i++) lv_label_set_text(translated[i].obj, T(translated[i].en));
}

void create(const Callbacks &cb) {
  cbs = cb;
  scr = lv_screen_active();
  lv_obj_remove_style_all(scr);
  lv_obj_set_style_bg_color(scr, hex(BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(scr, false);
  build_header();
  build_scene();
  build_bottom();
  build_picker();
  screens_build(scr);
}

// ---------------------------------------------------------------- update

static void set_hidden(lv_obj_t *o, bool hidden) { lv_obj_set_hidden(o, hidden); }



static int chips_used = 0;   // width taken in the chip row, which must stay clear of "Details ›"
static void add_chip(lv_obj_t *parent, Source s, int pct) {
  char t[8];
  snprintf(t, sizeof t, "%d%%", pct);
  const int w = 8 + 5 + text_width(source_name(s), &gm_inter_12) + 5 + text_width(t, &gm_grotesk_13) + (chips_used ? 12 : 0);
  if (chips_used + w > 360) return;
  chips_used += w;
  lv_obj_t *c = row(parent, 5);
  lv_obj_t *sw = box(c, 0, 0, 8, 8, SRC_STYLE[s].color);
  lv_obj_set_style_radius(sw, 2, 0);
  label(c, &gm_inter_12, 0xC9D0D9, source_name(s));
  char buf[8];
  snprintf(buf, sizeof buf, "%d%%", pct);
  label(c, &gm_grotesk_13, 0xFFFFFF, buf);
}

static void update_bottom() {
  lv_obj_clean(bar);
  lv_obj_clean(thin_bar);
  lv_obj_clean(chips);
  set_hidden(bar, !bar_fat);
  set_hidden(thin_bar, bar_fat);
  lv_obj_set_y(chips, bar_fat ? 244 : 232);
  chips_used = 0;
  lv_obj_set_y(details_btn, bar_fat ? 238 : 226);
  if (!cur.has_data) return;

  // sources, biggest first
  int order[SOURCE_COUNT], n = 0;
  for (int i = 0; i < SOURCE_COUNT; i++) if (cur.snap.share_pct((Source)i) >= 0.5f) order[n++] = i;
  for (int i = 1; i < n; i++) {
    const int v = order[i];
    int j = i - 1;
    while (j >= 0 && cur.snap.share_pct((Source)order[j]) < cur.snap.share_pct((Source)v)) { order[j + 1] = order[j]; j--; }
    order[j + 1] = v;
  }
  const float bar_w = 448 - (n - 1) * 2;
  int chips_shown = 0;
  for (int k = 0; k < n; k++) {
    const Source s = (Source)order[k];
    const float share = cur.snap.share_pct(s);
    const int pct = (int)lroundf(share);
    lv_obj_t *seg = box(bar_fat ? bar : thin_bar, 0, 0, 1, LV_PCT(100), SRC_STYLE[s].color);
    lv_obj_set_flex_grow(seg, (uint8_t)(share * 2 + 1));
    if (!bar_fat) {
      if (chips_shown < 4) { add_chip(chips, s, pct); chips_shown++; }
      continue;
    }
    lv_obj_set_flex_flow(seg, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(seg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(seg, 4, 0);
    char buf[8];
    snprintf(buf, sizeof buf, "%d%%", pct);
    const float px = bar_w * share / 100;
    const int name_w = text_width(source_name(s), &gm_inter_12);
    const int pct_w = text_width(buf, &gm_grotesk_13);
    const bool full = name_w + pct_w + 4 + 12 <= px;
    const bool pct_only = pct_w + 8 <= px;
    if (full) label(seg, &gm_inter_12, SRC_STYLE[s].text, source_name(s));
    if (full || pct_only) label(seg, &gm_grotesk_13, SRC_STYLE[s].text, buf);
    if (!full) add_chip(chips, s, pct);
  }
}

void update(const Model &m) {
  cur = m;
  const SkyColors &sky = SKIES[m.sky];

  // header
  lv_label_set_text(lbl_region, m.region);
  lv_label_set_text(lbl_clock, m.clock);
  set_hidden(clock_dot, !m.has_data);
  lv_obj_set_style_bg_color(clock_dot, hex(freshness_color(m.freshness)), 0);
  set_hidden(rate_row, !m.has_rate);
  set_hidden(lbl_noplan, m.has_rate);
  if (m.has_rate) {
    lv_label_set_text(lbl_rate, m.rate);
    lv_label_set_text(pill_period, m.period);
    uint32_t bg, fg;
    band_colors(m.band, m.flat_rate, bg, fg);
    lv_obj_set_style_bg_color(pill_period, hex(bg), 0);
    lv_obj_set_style_text_color(pill_period, hex(fg), 0);
  }

  // sky and celestial bits
  lv_obj_set_style_bg_color(scene, hex(sky.top), 0);
  lv_obj_set_style_bg_grad_color(scene, hex(sky.bottom), 0);
  for (auto *s : stars) set_hidden(s, m.sky == SKY_DAY);
  for (auto *s : stars) lv_obj_set_style_bg_opa(s, m.sky == SKY_NIGHT ? LV_OPA_COVER : LV_OPA_40, 0);
  set_hidden(sun_disc, m.sky != SKY_DAY);
  for (auto &r : rays) set_hidden(r.obj, m.sky != SKY_DAY);
  set_hidden(moon_disc, m.sky != SKY_NIGHT);
  set_hidden(moon_shade, m.sky != SKY_NIGHT);
  lv_obj_set_style_bg_color(moon_shade, hex(sky.top), 0);

  // The artwork only changes with the sky and which sources are running, so skip the redraw otherwise
  static int art_key = -1;
  int key = m.sky * 2 + m.show_imports;
  for (int i = 0; i < SOURCE_COUNT; i++) key = key * 2 + (m.has_data && m.snap.mw[i] > 0);
  if (key != art_key) { art_key = key; draw_static_art(); }

  // moving parts follow the mix
  const float coal = m.has_data ? m.snap.share_pct(COAL) : 0, gas = m.has_data ? m.snap.share_pct(GAS) : 0;
  const float wind = m.has_data ? m.snap.share_pct(WIND) : 0;
  const bool hydro = m.has_data && m.snap.mw[HYDRO] > 0;
  set_hidden(smoke_box, coal <= 0);
  lv_obj_set_style_opa(smoke_box, (lv_opa_t)(255 * fminf(1, 0.35f + coal / 80)), 0);
  for (auto *p : puffs) lv_obj_set_style_bg_color(p, hex(sky.smoke), 0);
  set_hidden(vap_box, !(m.has_data && m.snap.mw[NUCLEAR] > 0));
  set_hidden(flame_img, gas <= 0);
  lv_obj_set_style_opa(flame_img, (lv_opa_t)(255 * fminf(1, 0.55f + gas / 60)), 0);
  set_hidden(water_box, !hydro);
  for (auto *f : foams) set_hidden(f, !hydro);
  const bool batt_active = m.has_data && (m.snap.mw[BATTERY] > 0 || m.snap.battery_charging > 0);
  for (int i = 0; i < 3; i++) {
    const int lit = !m.has_data ? 0 : m.snap.battery_charging > m.snap.mw[BATTERY] ? 2 : m.snap.mw[BATTERY] > 0 ? 3 : 0;
    lv_obj_set_style_bg_color(cells[i], hex(i < lit ? 0x9B7BEA : 0x3A3350), 0);
    lv_obj_set_style_opa(cells[i], batt_active ? LV_OPA_COVER : (lv_opa_t)(255 * ghost(BATTERY)), 0);
    if (!batt_active) lv_anim_delete(cells[i], pulse_exec);
    else if (!lv_anim_get(cells[i], pulse_exec)) anim(cells[i], pulse_exec, 1600);
  }
  for (auto *gl : glints) set_hidden(gl, !(m.has_data && m.snap.mw[SOLAR] > 0 && m.sky != SKY_NIGHT));
  for (auto *sp : sparks) set_hidden(sp, !(m.show_imports && m.has_data && m.snap.mw[IMPORTS] > 0));
  rotors_on = wind > 0;
  const float base = wind > 0 ? fmaxf(0.9f, 5 - wind * 0.06f) : 4;
  rotor_period[0] = base; rotor_period[1] = base * 1.15f; rotor_period[2] = base * 0.9f;
  for (int r = 0; r < 3; r++) {
    for (auto &b : blades[r]) lv_obj_set_style_opa(b.obj, (lv_opa_t)(255 * ghost(WIND)), 0);
    lv_obj_set_style_opa(hubs[r], (lv_opa_t)(255 * ghost(WIND)), 0);
  }

  // text over the scene
  char buf[16];
  if (m.has_data) snprintf(buf, sizeof buf, "%d%%", (int)lroundf(m.snap.renewable_pct()));
  else snprintf(buf, sizeof buf, "--");
  lv_label_set_text(lbl_pct, buf);
  lv_obj_set_style_text_color(lbl_pct, hex(sky.text), 0);
  lv_obj_set_style_text_color(lbl_pct_sub, hex(sky.sub), 0);
  static const uint32_t TONE_BG[3] = {0xDFF5E8, 0xEEF2F6, 0xFFE3DC}, TONE_FG[3] = {0x10502F, 0x1C2633, 0x7A1E10};
  lv_label_set_text(pill_advice, m.advice);
  lv_obj_set_style_bg_color(pill_advice, hex(TONE_BG[m.tone]), 0);
  lv_obj_set_style_text_color(pill_advice, hex(TONE_FG[m.tone]), 0);

  update_bottom();

  // picker
  const ViewOption *opts[2] = {&m.local, &m.country};
  for (int i = 0; i < 2; i++) {
    const bool on = (i == 1) == m.view_country;
    lv_label_set_text(opt_title[i], opts[i]->title);
    lv_label_set_text(opt_sub[i], opts[i]->sub);
    lv_label_set_text(opt_freq[i], opts[i]->freq);
    lv_label_set_text(opt_ago[i], opts[i]->ago);
    lv_obj_set_style_bg_color(opt_btn[i], hex(on ? 0x18202B : 0x13171D), 0);
    lv_obj_set_style_border_color(opt_btn[i], hex(on ? 0x4FB3D9 : 0x262C36), 0);
    lv_obj_set_style_border_color(opt_ring[i], hex(on ? 0x4FB3D9 : 0x5A6372), 0);
    set_hidden(opt_dot[i], !on);
    lv_obj_update_layout(opt_ago[i]);
    lv_obj_set_pos(opt_fresh[i], lv_obj_get_x(opt_ago[i]) - 10, 33);
    lv_obj_set_style_bg_color(opt_fresh[i], hex(freshness_color(opts[i]->freshness)), 0);
  }

  screens_update(m);
}

}  // namespace gridui

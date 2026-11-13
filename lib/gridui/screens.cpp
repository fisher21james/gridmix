// Detail screen (tap the scene) and tariff screen (tap your rate, top right).
// Layout follows the design mockups (480 x 272, 40 px header).
#include <string.h>
#include "raster.h"
#include "ui_common.h"

namespace gridui {

static const uint32_t LINE = 0x1F242D, CARD = 0x151A21, CARD_LINE = 0x232935, FAINT = 0x8C95A1;
static const int CHART_W = 198, CHART_H = 60;

static lv_obj_t *detail, *tariff_scr, *upd_scr, *upd_msg, *upd_fill, *upd_pct;
// detail
static lv_obj_t *d_region, *d_status, *d_status_dot, *d_status_lbl, *d_rows, *d_waiting, *d_total;
static lv_obj_t *d_chart_img, *d_chart_note, *d_window_val, *d_price_val;
static Raster chart;
// tariff
static lv_obj_t *t_kind, *t_body, *t_empty, *t_now, *t_bar, *t_marker, *t_change, *t_change_lbl, *t_cards, *t_footer;

static void hide_all() {
  lv_obj_set_hidden(detail, true);
  lv_obj_set_hidden(tariff_scr, true);
}
static void on_back(lv_event_t *) { hide_all(); }

void show_detail() { lv_obj_set_hidden(tariff_scr, true); lv_obj_set_hidden(detail, false); }
void show_tariff() { lv_obj_set_hidden(detail, true); lv_obj_set_hidden(tariff_scr, false); }
bool screens_open() { return !lv_obj_is_hidden(detail) || !lv_obj_is_hidden(tariff_scr); }

// Back to the main screen after a minute without a touch
static void idle_timer(lv_timer_t *) {
  if (screens_open() && lv_display_get_inactive_time(nullptr) > 60000) hide_all();
}

static lv_obj_t *page(lv_obj_t *scr, const char *title, lv_obj_t **sub) {
  lv_obj_t *p = box(scr, 0, 0, 480, 272, BG);
  lv_obj_set_clickable(p, true);   // swallow taps meant for the screen underneath
  lv_obj_set_hidden(p, true);
  box(p, 0, 40, 480, 1, LINE);
  lv_obj_t *head = row(p, 8);
  lv_obj_set_pos(head, 0, 0);
  lv_obj_set_height(head, 40);
  lv_obj_set_style_pad_left(head, 6, 0);
  lv_obj_t *back = box(head, 0, 0, 64, 40, 0, LV_OPA_TRANSP);
  clickable(back, on_back);
  lv_obj_t *bl = label(back, &gm_inter_13, TEXT, "\xE2\x80\xB9 Back");
  lv_obj_align(bl, LV_ALIGN_LEFT_MID, 8, 0);
  label(head, &gm_grotesk_15, 0xFFFFFF, title);
  *sub = label(head, &gm_inter_12, MUTED);
  return p;
}

static void thousands(float v, char *out, size_t cap) {
  const long n = lroundf(v);
  if (n >= 1000) snprintf(out, cap, "%ld,%03ld", n / 1000, n % 1000);
  else snprintf(out, cap, "%ld", n);
}

// ---------------------------------------------------------------- detail

static void build_detail(lv_obj_t *scr) {
  detail = page(scr, "Mix now", &d_region);

  d_status = row(detail, 5);
  lv_obj_align(d_status, LV_ALIGN_TOP_RIGHT, -14, 9);
  lv_obj_set_style_bg_opa(d_status, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(d_status, 11, 0);
  lv_obj_set_style_pad_hor(d_status, 10, 0);
  lv_obj_set_style_pad_ver(d_status, 4, 0);
  d_status_dot = box(d_status, 0, 0, 7, 7, 0xFFFFFF);
  lv_obj_set_style_radius(d_status_dot, 4, 0);
  d_status_lbl = label(d_status, &gm_inter_12, 0xFFFFFF);

  d_rows = lv_obj_create(detail);
  lv_obj_remove_style_all(d_rows);
  lv_obj_set_scrollable(d_rows, false);
  lv_obj_set_clickable(d_rows, false);
  lv_obj_set_pos(d_rows, 14, 52);
  lv_obj_set_size(d_rows, 238, 190);
  d_waiting = label(detail, &gm_inter_12, MUTED, "Waiting for data\xE2\x80\xA6");
  lv_obj_set_pos(d_waiting, 14, 56);
  d_total = label(detail, &gm_inter_11, MUTED);
  lv_obj_set_pos(d_total, 14, 248);

  const int x = 268;
  lv_obj_t *t = label(detail, &gm_inter_11, MUTED, "Renewables \xC2\xB7 last 24 h");
  lv_obj_set_pos(t, x, 50);
  chart.init(CHART_W, CHART_H);
  d_chart_img = lv_image_create(detail);
  lv_obj_set_pos(d_chart_img, x, 68);
  d_chart_note = label(detail, &gm_inter_11, FAINT, "Builds up over the first day");
  lv_obj_set_pos(d_chart_note, x + 8, 92);
  lv_obj_t *l0 = label(detail, &gm_inter_11, FAINT, "24 h ago");
  lv_obj_set_pos(l0, x, 130);
  lv_obj_t *l1 = label(detail, &gm_inter_11, FAINT, "now");
  lv_obj_set_pos(l1, x + CHART_W - 22, 130);

  lv_obj_t *card = box(detail, x, 150, CHART_W, 46, 0x16231C);
  lv_obj_set_style_radius(card, 8, 0);
  lv_obj_t *ct = label(card, &gm_inter_11, 0x9FD8B5, "Cheapest time for big loads");
  lv_obj_set_pos(ct, 10, 6);
  d_window_val = label(card, &gm_grotesk_15, 0xE9FFF3);
  lv_obj_set_pos(d_window_val, 10, 22);

  lv_obj_t *pl = label(detail, &gm_inter_11, MUTED, "Wholesale price");
  lv_obj_set_pos(pl, x, 206);
  d_price_val = label(detail, &gm_grotesk_13, 0xFFFFFF);
  lv_obj_align(d_price_val, LV_ALIGN_TOP_RIGHT, -14, 204);
}

static void chart_point(int i, float v, float &px, float &py) {
  px = 4 + i * (CHART_W - 8) / (float)(HISTORY_POINTS - 1);
  py = CHART_H - 3 - v * (CHART_H - 8) / 100.0f;
}

static void draw_chart(const Model &m) {
  chart.clear();
  chart.opa = 1;
  for (int x = 0; x < CHART_W; x += 6) chart.rect((float)x, CHART_H / 2.0f, 3, 1, 0x232935);   // 50% line
  int valid = 0;
  for (int i = 0; i < HISTORY_POINTS; i++) valid += !isnan(m.history[i]);
  if (valid >= 2) {
    // each unbroken run: shaded area, then the line
    for (int i = 0; i < HISTORY_POINTS;) {
      if (isnan(m.history[i])) { i++; continue; }
      int j = i;
      while (j + 1 < HISTORY_POINTS && !isnan(m.history[j + 1])) j++;
      if (j > i) {
        Pt pts[HISTORY_POINTS + 2];
        int n = 0;
        for (int k = i; k <= j; k++) { chart_point(k, m.history[k], pts[n].x, pts[n].y); n++; }
        pts[n] = {pts[n - 1].x, (float)CHART_H};
        pts[n + 1] = {pts[0].x, (float)CHART_H};
        chart.opa = 0.18f;
        chart.fill(pts, n + 2, 0x3BB273);
        chart.opa = 1;
        chart.stroke(pts, n, false, 2, 0x3BB273);
      }
      i = j + 1;
    }
    const float last = m.history[HISTORY_POINTS - 1];
    if (!isnan(last)) {
      float px, py;
      chart_point(HISTORY_POINTS - 1, last, px, py);
      chart.ellipse(px, py, 4.5f, 4.5f, 0x3BB273);
      chart.ellipse(px, py, 2.5f, 2.5f, 0xFFFFFF);
    }
  }
  lv_obj_set_hidden(d_chart_note, valid >= 2);
  lv_image_cache_drop(chart.image());
  lv_image_set_src(d_chart_img, chart.image());
  lv_obj_invalidate(d_chart_img);
}

static void update_detail(const Model &m) {
  lv_label_set_text(d_region, m.region);

  const GridStatus g = m.has_data ? grid_status(m.snap) : GRID_UNKNOWN;
  lv_obj_set_hidden(d_status, g == GRID_UNKNOWN);
  if (g != GRID_UNKNOWN) {
    static const uint32_t FG[5] = {0, 0x7EE2A8, 0xC9D0D9, 0xF2B35A, 0xFF8A78}, BGC[5] = {0, 0x16231C, 0x1B222B, 0x2A2114, 0x2A1714};
    lv_label_set_text_fmt(d_status_lbl, "Grid: %s", grid_status_label(g));
    lv_obj_set_style_text_color(d_status_lbl, hex(FG[g]), 0);
    lv_obj_set_style_bg_color(d_status_dot, hex(FG[g]), 0);
    lv_obj_set_style_bg_color(d_status, hex(BGC[g]), 0);
  }

  // one row per source, biggest first
  lv_obj_clean(d_rows);
  lv_obj_set_hidden(d_waiting, m.has_data);
  int order[SOURCE_COUNT], n = 0;
  if (m.has_data)
    for (int i = 0; i < SOURCE_COUNT; i++) if (m.snap.share_pct((Source)i) >= 0.5f) order[n++] = i;
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && m.snap.share_pct((Source)order[j]) > m.snap.share_pct((Source)order[j - 1]); j--) {
      const int tmp = order[j]; order[j] = order[j - 1]; order[j - 1] = tmp;
    }
  if (n > 7) n = 7;
  const int pitch = n > 6 ? 26 : 29;
  for (int k = 0; k < n; k++) {
    const Source s = (Source)order[k];
    const float share = m.snap.share_pct(s);
    lv_obj_t *r = box(d_rows, 0, k * pitch, 238, 24, 0, LV_OPA_TRANSP);
    lv_obj_t *sw = box(r, 0, 3, 8, 8, SRC_STYLE[s].color);
    lv_obj_set_style_radius(sw, 2, 0);
    lv_obj_t *name = label(r, &gm_inter_12, TEXT, source_name(s));
    lv_obj_set_pos(name, 14, 0);
    const char *note = "";
    if (s == SOLAR && m.snap.estimated) note = "incl. rooftop (est.)";
    if (s == BATTERY && m.snap.battery_charging > 0) note = "some charging";
    if (*note) {
      lv_obj_t *nl = label(r, &gm_inter_11, MUTED, note);
      lv_obj_set_pos(nl, 20 + text_width(source_name(s), &gm_inter_12), 1);
    }
    char pct[8];
    snprintf(pct, sizeof pct, "%d%%", (int)lroundf(share));
    lv_obj_t *pl = label(r, &gm_grotesk_13, 0xFFFFFF, pct);
    lv_obj_align(pl, LV_ALIGN_TOP_RIGHT, 0, -1);
    lv_obj_t *track = box(r, 0, 17, 238, 6, 0x1B1F27);
    lv_obj_set_style_radius(track, 3, 0);
    const int w = (int)lroundf(238 * share / 100);
    lv_obj_t *fill = box(track, 0, 0, w < 3 ? 3 : w, 6, SRC_STYLE[s].color);
    lv_obj_set_style_radius(fill, 3, 0);
  }

  char total[80] = "", mw[24];
  if (m.has_data) {
    thousands(m.snap.total(), mw, sizeof mw);
    int len = snprintf(total, sizeof total, "Total %s MW", mw);
    if (m.snap.battery_charging >= 1) {
      thousands(m.snap.battery_charging, mw, sizeof mw);
      snprintf(total + len, sizeof total - len, " \xC2\xB7 batteries charging %s MW", mw);
    }
  }
  lv_label_set_text(d_total, total);

  draw_chart(m);

  char win[40];
  if (!m.has_tariff) {
    lv_obj_set_style_text_font(d_window_val, &gm_inter_12, 0);
    lv_label_set_text(d_window_val, "Add your plan at gridmix.local");
  } else {
    lv_obj_set_style_text_font(d_window_val, &gm_grotesk_15, 0);
    if (tariff_cheapest_window(m.tariff, m.weekday, m.minute, win, sizeof win)) lv_label_set_text(d_window_val, win);
    else lv_label_set_text(d_window_val, "Any time (flat rate)");
  }

  if (m.has_data && m.snap.has_price) {
    const float c = m.snap.price / 10.0f;   // $/MWh -> c/kWh
    char buf[24];
    snprintf(buf, sizeof buf, "%s%.1f\xC2\xA2/kWh", c < 0 ? "\xE2\x88\x92" : "", fabsf(c));
    lv_label_set_text(d_price_val, buf);
    static const uint32_t FG[5] = {0xFFFFFF, 0x7EE2A8, 0xFFFFFF, 0xF2B35A, 0xFF8A78};
    lv_obj_set_style_text_color(d_price_val, hex(FG[g]), 0);
    lv_obj_set_style_text_font(d_price_val, &gm_grotesk_13, 0);
  } else {
    lv_label_set_text(d_price_val, "Not available here");
    lv_obj_set_style_text_color(d_price_val, hex(FAINT), 0);
    lv_obj_set_style_text_font(d_price_val, &gm_inter_11, 0);
  }
}

// ---------------------------------------------------------------- tariff

static void build_tariff(lv_obj_t *scr) {
  tariff_scr = page(scr, "Your tariff", &t_kind);

  t_empty = box(tariff_scr, 0, 41, 480, 231, BG);
  lv_obj_t *e1 = label(t_empty, &gm_grotesk_17, 0xFFFFFF, "No power plan yet");
  lv_obj_align(e1, LV_ALIGN_CENTER, 0, -14);
  lv_obj_t *e2 = label(t_empty, &gm_inter_12, MUTED, "Add your rates at gridmix.local from your phone");
  lv_obj_align(e2, LV_ALIGN_CENTER, 0, 12);

  t_body = box(tariff_scr, 14, 50, 452, 216, 0, LV_OPA_TRANSP);
  lv_obj_t *today = label(t_body, &gm_inter_11, MUTED, "Today");
  lv_obj_set_pos(today, 0, 0);
  t_now = label(t_body, &gm_inter_11, MUTED);
  lv_obj_align(t_now, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_label_set_recolor(t_now, true);

  t_bar = box(t_body, 0, 20, 452, 24, 0x1B1F27);
  lv_obj_set_style_radius(t_bar, 6, 0);
  lv_obj_set_style_clip_corner(t_bar, true, 0);
  t_marker = box(t_body, 0, 15, 5, 34, 0xFFFFFF);
  lv_obj_set_style_radius(t_marker, 2, 0);
  lv_obj_set_style_border_width(t_marker, 1, 0);
  lv_obj_set_style_border_color(t_marker, hex(BG), 0);

  static const char *HOURS[5] = {"12am", "6am", "12pm", "6pm", "12am"};
  for (int i = 0; i < 5; i++) {
    lv_obj_t *h = label(t_body, &gm_inter_11, FAINT, HOURS[i]);
    const int w = text_width(HOURS[i], &gm_inter_11);
    int x = i * 113 - w / 2;
    x = x < 0 ? 0 : x + w > 452 ? 452 - w : x;
    lv_obj_set_pos(h, x, 50);
  }

  t_change = box(t_body, 0, 70, 452, 28, 0x2A1714);
  lv_obj_set_style_radius(t_change, 8, 0);
  t_change_lbl = label(t_change, &gm_inter_12, 0xFFD9D2);
  lv_obj_align(t_change_lbl, LV_ALIGN_LEFT_MID, 10, 0);

  t_cards = box(t_body, 0, 106, 452, 64, 0, LV_OPA_TRANSP);

  t_footer = label(t_body, &gm_inter_11, MUTED);
  lv_obj_set_pos(t_footer, 0, 192);
  lv_obj_t *edit = label(t_body, &gm_inter_11, MUTED, "Edit at #4FB3D9 gridmix.local#");
  lv_label_set_recolor(edit, true);
  lv_obj_align(edit, LV_ALIGN_TOP_RIGHT, 0, 192);
}

static void update_tariff(const Model &m) {
  lv_obj_set_hidden(t_empty, m.has_tariff);
  lv_obj_set_hidden(t_body, !m.has_tariff);
  if (!m.has_tariff) { lv_label_set_text(t_kind, ""); return; }
  const Tariff &t = m.tariff;
  RateNow now;
  const bool have_now = tariff_rate_now(t, m.weekday, m.minute, now);
  const bool flat = have_now && now.minutes_to_change < 0;
  lv_label_set_text(t_kind, flat ? "Flat rate" : "Time of use");

  char rate[12] = "", clock[12];
  if (have_now) format_rate(now.cents, rate, sizeof rate);
  format_clock(m.minute, clock, sizeof clock);
  lv_label_set_text_fmt(t_now, "Now %s \xC2\xB7 #FFFFFF %s/kWh#", clock, rate);

  // today's bands
  lv_obj_clean(t_bar);
  DaySegment seg[24];
  const int ns = tariff_day_segments(t, m.weekday, seg, 24);
  for (int k = 0; k < ns; k++) {
    const int x0 = seg[k].start * 452 / 1440, x1 = seg[k].end * 452 / 1440;
    const int w = x1 - x0 - (seg[k].end < 1440 ? 2 : 0);
    if (w <= 0 || seg[k].index < 0) continue;
    const TariffPeriod &p = t.periods[seg[k].index];
    uint32_t bg, fg;
    band_colors(tariff_band(t, p.cents), flat, bg, fg);
    lv_obj_t *b = box(t_bar, x0, 0, w, 24, bg);
    if (text_width(p.name, &gm_inter_11) + 8 <= w) {
      lv_obj_t *l = label(b, &gm_inter_11, fg, p.name);
      lv_obj_center(l);
    }
  }
  lv_obj_set_x(t_marker, m.minute * 452 / 1440 - 2);

  // what changes next
  uint32_t bg = 0x1B222B, fg = TEXT;
  char msg[96];
  if (!have_now) snprintf(msg, sizeof msg, "No rate set for this time \xC2\xB7 check your plan at gridmix.local");
  else if (flat) snprintf(msg, sizeof msg, "Same rate all day \xC2\xB7 %s/kWh", rate);
  else {
    const TariffPeriod &next = t.periods[now.next_index];
    char when[12], nrate[12];
    format_clock((m.minute + now.minutes_to_change) % 1440, when, sizeof when);
    format_rate(next.cents, nrate, sizeof nrate);
    const bool up = next.cents > now.cents;
    snprintf(msg, sizeof msg, "%s starts at %s \xC2\xB7 %s/kWh, %s", next.name, when, nrate,
             up ? "run big loads before then" : "wait for it if you can");
    bg = up ? 0x2A1714 : 0x16231C;
    fg = up ? 0xFFD9D2 : 0xC9F2D9;
  }
  lv_obj_set_style_bg_color(t_change, hex(bg), 0);
  lv_obj_set_style_text_color(t_change_lbl, hex(fg), 0);
  lv_label_set_text(t_change_lbl, msg);

  // one card per rate
  lv_obj_clean(t_cards);
  RateCard cards[4];
  const int nc = tariff_cards(t, m.weekday, cards, 4);
  const int gap = 8, cw = nc ? (452 - gap * (nc - 1)) / nc : 452;
  for (int i = 0; i < nc; i++) {
    const TariffPeriod &p = t.periods[cards[i].index];
    const bool is_now = have_now && p.cents == now.cents && !strcmp(p.name, t.periods[now.index].name);
    uint32_t cbg, cfg;
    band_colors(tariff_band(t, p.cents), flat, cbg, cfg);
    lv_obj_t *c = box(t_cards, i * (cw + gap), 0, cw, 64, CARD);
    lv_obj_set_style_radius(c, 8, 0);
    lv_obj_set_style_border_width(c, is_now ? 2 : 1, 0);
    lv_obj_set_style_border_color(c, hex(is_now ? cbg : CARD_LINE), 0);
    lv_obj_t *sw = box(c, 8, 10, 8, 8, cbg);
    lv_obj_set_style_radius(sw, 2, 0);
    lv_obj_t *nl = label(c, &gm_inter_11, TEXT, p.name);
    lv_obj_set_pos(nl, 21, 6);
    char r[12];
    format_rate(p.cents, r, sizeof r);
    lv_obj_t *rl = label(c, &gm_grotesk_17, 0xFFFFFF, r);
    lv_obj_set_pos(rl, 8, 22);
    lv_obj_t *hl = label(c, &gm_inter_11, MUTED, cards[i].hours);
    lv_label_set_long_mode(hl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(hl, cw - 14);
    lv_obj_set_pos(hl, 8, 44);
  }

  char foot[64] = "";
  if (t.supply_cents_per_day > 0 || t.feed_in_cents > 0)
    snprintf(foot, sizeof foot, "Supply %.1f\xC2\xA2/day \xC2\xB7 Feed-in %.1f\xC2\xA2/kWh", t.supply_cents_per_day, t.feed_in_cents);
  lv_label_set_text(t_footer, foot);
}

// ---------------------------------------------------------------- entry points

void show_update(int percent, const char *message) {
  if (percent < 0) { lv_obj_set_hidden(upd_scr, true); return; }
  lv_obj_set_hidden(upd_scr, false);
  lv_label_set_text(upd_msg, message);
  lv_obj_set_width(upd_fill, 3 + percent * 317 / 100);
  lv_label_set_text_fmt(upd_pct, "%d%%", percent);
}

static void build_update(lv_obj_t *scr) {
  upd_scr = box(scr, 0, 0, 480, 272, BG);
  lv_obj_set_clickable(upd_scr, true);
  lv_obj_set_hidden(upd_scr, true);
  lv_obj_t *t = label(upd_scr, &gm_grotesk_17, 0xFFFFFF, "Updating Grid Mix");
  lv_obj_align(t, LV_ALIGN_CENTER, 0, -40);
  upd_msg = label(upd_scr, &gm_inter_12, MUTED);
  lv_obj_align(upd_msg, LV_ALIGN_CENTER, 0, -14);
  lv_obj_t *track = box(upd_scr, 80, 150, 320, 8, 0x1B1F27);
  lv_obj_set_style_radius(track, 4, 0);
  upd_fill = box(track, 0, 0, 3, 8, 0x4FB3D9);
  lv_obj_set_style_radius(upd_fill, 4, 0);
  upd_pct = label(upd_scr, &gm_inter_12, TEXT);
  lv_obj_align(upd_pct, LV_ALIGN_CENTER, 0, 34);
  lv_obj_t *n = label(upd_scr, &gm_inter_11, FAINT, "Keep it plugged in. It restarts by itself when done.");
  lv_obj_align(n, LV_ALIGN_BOTTOM_MID, 0, -20);
}

void screens_build(lv_obj_t *scr) {
  build_detail(scr);
  build_tariff(scr);
  build_update(scr);
  lv_timer_create(idle_timer, 1000, nullptr);
}

void screens_update(const Model &m) {
  update_detail(m);
  update_tariff(m);
}

}  // namespace gridui

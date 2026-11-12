// Grid Mix Display — bring-up firmware (step 1)
//
// What this does:
//   - Starts the 480x272 QSPI display and the GT911 touch panel
//   - Runs LVGL 9 with an FPS/CPU overlay in the corner
//   - Draws a test wind turbine; drag the slider to change its speed (tests touch + animation load)
//   - Listens for Wi-Fi details from the browser flasher (Improv) and reconnects on later boots
//
// The real screens get built on top of this once the hardware checks out.

#include <Arduino.h>
#include <WiFi.h>
#include <math.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <ImprovWiFiLibrary.h>
#include "board_config.h"

#if HAS_GT911_TOUCH
#include <Wire.h>
#include <TAMC_GT911.h>
#endif

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// ---------- Display ----------
Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
Arduino_GFX *gfx = new Arduino_NV3041A(bus, GFX_NOT_DEFINED /* no reset pin */, 0 /* rotation */, true /* IPS */);

#if HAS_GT911_TOUCH
TAMC_GT911 touch(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, LCD_WIDTH, LCD_HEIGHT);
#endif

// ---------- Wi-Fi setup from the browser flasher ----------
ImprovWiFi improv(&Serial);

// ---------- Test scene ----------
static const int HUB_X = 240, HUB_Y = 120, BLADE_LEN = 70;
static lv_obj_t *blades[3];
static lv_point_precise_t blade_pts[3][2];
static float angle_deg = 0;
static float speed_deg_per_frame = 3.0f;   // set by the slider
static lv_obj_t *wifi_label;

static uint32_t tick_cb() { return millis(); }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  const uint32_t w = lv_area_get_width(area);
  const uint32_t h = lv_area_get_height(area);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
  lv_display_flush_ready(disp);
}

#if HAS_GT911_TOUCH
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  touch.read();
  if (touch.isTouched) {
    data->point.x = touch.points[0].x;
    data->point.y = touch.points[0].y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}
#endif

static void spin_timer_cb(lv_timer_t *) {
  angle_deg += speed_deg_per_frame;
  if (angle_deg >= 360) angle_deg -= 360;
  for (int i = 0; i < 3; i++) {
    const float a = (angle_deg + i * 120.0f) * (float)M_PI / 180.0f;
    blade_pts[i][0] = {(lv_value_precise_t)HUB_X, (lv_value_precise_t)HUB_Y};
    blade_pts[i][1] = {(lv_value_precise_t)(HUB_X + sinf(a) * BLADE_LEN),
                       (lv_value_precise_t)(HUB_Y - cosf(a) * BLADE_LEN)};
    lv_line_set_points(blades[i], blade_pts[i], 2);
  }
}

static void slider_cb(lv_event_t *e) {
  lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
  // 0..100 "wind share" maps to 0..12 degrees per frame
  speed_deg_per_frame = lv_slider_get_value(slider) * 0.12f;
}

static void wifi_timer_cb(lv_timer_t *) {
  if (WiFi.status() == WL_CONNECTED) {
    lv_label_set_text_fmt(wifi_label, "Wi-Fi: %s  %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  } else {
    lv_label_set_text(wifi_label, "Wi-Fi: not set - use the web flasher to connect");
  }
}

static void build_test_screen() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0E1116), 0);

  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "Grid Mix  bring-up  v" FW_VERSION);
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 10);

  // Tower
  static lv_point_precise_t tower_pts[2] = {{HUB_X, HUB_Y}, {HUB_X, 230}};
  lv_obj_t *tower = lv_line_create(scr);
  lv_line_set_points(tower, tower_pts, 2);
  lv_obj_set_style_line_width(tower, 6, 0);
  lv_obj_set_style_line_color(tower, lv_color_hex(0xE9EEF2), 0);
  lv_obj_set_style_line_rounded(tower, true, 0);

  // Blades
  for (int i = 0; i < 3; i++) {
    blades[i] = lv_line_create(scr);
    lv_obj_set_style_line_width(blades[i], 5, 0);
    lv_obj_set_style_line_color(blades[i], lv_color_hex(0xF7F9FB), 0);
    lv_obj_set_style_line_rounded(blades[i], true, 0);
  }

  // Hub, in the wind colour
  lv_obj_t *hub = lv_obj_create(scr);
  lv_obj_set_size(hub, 14, 14);
  lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(hub, lv_color_hex(0x4FB3D9), 0);
  lv_obj_set_style_border_width(hub, 0, 0);
  lv_obj_set_pos(hub, HUB_X - 7, HUB_Y - 7);

  // Speed slider (touch test)
  lv_obj_t *slider = lv_slider_create(scr);
  lv_obj_set_width(slider, 300);
  lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -14);
  lv_slider_set_value(slider, 25, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0x4FB3D9), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0x4FB3D9), LV_PART_KNOB);
  lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

  wifi_label = lv_label_create(scr);
  lv_obj_set_style_text_color(wifi_label, lv_color_hex(0xA9B1BC), 0);
  lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_12, 0);
  lv_obj_align(wifi_label, LV_ALIGN_TOP_LEFT, 12, 32);

  lv_timer_create(spin_timer_cb, 16, NULL);
  lv_timer_create(wifi_timer_cb, 1000, NULL);
  wifi_timer_cb(NULL);
}

void setup() {
  Serial.begin(115200);

  // Backlight on
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  if (!gfx->begin()) {
    Serial.println("Display init failed - check pins in board_config.h");
  }
  gfx->fillScreen(0x0000);

#if HAS_GT911_TOUCH
  touch.begin();
  touch.setRotation(ROTATION_NORMAL);   // if taps land mirrored, try ROTATION_INVERTED
#endif

  // LVGL
  lv_init();
  lv_tick_set_cb(tick_cb);

  const size_t buf_px = LCD_WIDTH * 40;  // 40 lines per chunk
  uint8_t *buf1 = (uint8_t *)heap_caps_malloc(buf_px * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  uint8_t *buf2 = (uint8_t *)heap_caps_malloc(buf_px * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  lv_display_t *disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_buffers(disp, buf1, buf2, buf_px * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);

#if HAS_GT911_TOUCH
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_read_cb);
#endif

  // Wi-Fi: reuse saved details if we have them; the browser flasher can send new ones
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  improv.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32_S3, "GridMix", FW_VERSION,
                       "Grid Mix Display", "http://{LOCAL_IPV4}");

  build_test_screen();
}

void loop() {
  improv.handleSerial();
  lv_timer_handler();
  delay(2);
}

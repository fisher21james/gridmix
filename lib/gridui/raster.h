// Tiny anti-aliased polygon renderer used to draw the scene's artwork at start-up.
// Drawing in code (instead of storing images) keeps the firmware small and lets
// colours and "ghosting" change at run time. Output is an LVGL ARGB8888 image.
#pragma once
#include <stdint.h>
#include "lvgl.h"

namespace gridui {

struct Pt { float x, y; };

// A list of points, built up from straight lines and curves.
struct Path {
  static const int MAX = 160;
  Pt p[MAX];
  int n = 0;
  Path &move(float x, float y) { n = 0; return line(x, y); }
  Path &line(float x, float y) { if (n < MAX) p[n++] = {x, y}; return *this; }
  Path &quad(float cx, float cy, float x, float y, int steps = 12);   // quadratic curve to x,y
};

class Raster {
 public:
  bool init(int w, int h);
  void clear();
  int width() const { return w_; }
  int height() const { return h_; }

  float opa = 1.0f;   // multiplies everything drawn (used to "ghost" plants that aren't running)
  float ox = 0, oy = 0;   // drawing offset, so artwork can use full-scene coordinates

  void fill(const Pt *pts, int n, uint32_t rgb, float a = 1.0f);
  void fill(const Path &path, uint32_t rgb, float a = 1.0f) { fill(path.p, path.n, rgb, a); }
  void stroke(const Pt *pts, int n, bool closed, float width, uint32_t rgb, float a = 1.0f);
  void rect(float x, float y, float w, float h, uint32_t rgb, float a = 1.0f, float radius = 0);
  void rect_outline(float x, float y, float w, float h, float width, uint32_t rgb, float radius = 0);
  void ellipse(float cx, float cy, float rx, float ry, uint32_t rgb, float a = 1.0f);

  const lv_image_dsc_t *image() const { return &dsc_; }

 private:
  void blend(int x, int y, uint32_t rgb, float a);
  int w_ = 0, h_ = 0;
  uint8_t *buf_ = nullptr;   // B,G,R,A per pixel (LVGL ARGB8888, little-endian)
  lv_image_dsc_t dsc_;
};

}  // namespace gridui

#include "raster.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace gridui {

Path &Path::quad(float cx, float cy, float x, float y, int steps) {
  if (n == 0) return line(x, y);
  const Pt s = p[n - 1];
  for (int i = 1; i <= steps; i++) {
    const float t = (float)i / steps, u = 1 - t;
    line(u * u * s.x + 2 * u * t * cx + t * t * x, u * u * s.y + 2 * u * t * cy + t * t * y);
  }
  return *this;
}

bool Raster::init(int w, int h) {
  if (buf_ && w == w_ && h == h_) { clear(); return true; }
  free(buf_);
  buf_ = (uint8_t *)malloc((size_t)w * h * 4);
  if (!buf_) return false;
  w_ = w;
  h_ = h;
  memset(&dsc_, 0, sizeof dsc_);
  dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc_.header.cf = LV_COLOR_FORMAT_ARGB8888;
  dsc_.header.w = w;
  dsc_.header.h = h;
  dsc_.header.stride = w * 4;
  dsc_.data_size = (uint32_t)w * h * 4;
  dsc_.data = buf_;
  clear();
  return true;
}

void Raster::clear() {
  if (buf_) memset(buf_, 0, (size_t)w_ * h_ * 4);
}

void Raster::blend(int x, int y, uint32_t rgb, float a) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_ || a <= 0) return;
  uint8_t *px = buf_ + ((size_t)y * w_ + x) * 4;
  const float sr = (rgb >> 16) & 255, sg = (rgb >> 8) & 255, sb = rgb & 255;
  const float da = px[3] / 255.0f;
  const float oa = a + da * (1 - a);
  if (oa <= 0) return;
  const float k = da * (1 - a);
  px[2] = (uint8_t)((sr * a + px[2] * k) / oa + 0.5f);
  px[1] = (uint8_t)((sg * a + px[1] * k) / oa + 0.5f);
  px[0] = (uint8_t)((sb * a + px[0] * k) / oa + 0.5f);
  px[3] = (uint8_t)(oa * 255 + 0.5f);
}

// Even-odd scanline fill with 4 sub-rows per pixel and exact horizontal coverage.
void Raster::fill(const Pt *pts, int n, uint32_t rgb, float a) {
  if (!buf_ || n < 3) return;
  a *= opa;
  float miny = 1e9f, maxy = -1e9f, minx = 1e9f, maxx = -1e9f;
  for (int i = 0; i < n; i++) {
    const float x = pts[i].x - ox, y = pts[i].y - oy;
    if (y < miny) miny = y;
    if (y > maxy) maxy = y;
    if (x < minx) minx = x;
    if (x > maxx) maxx = x;
  }
  int y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
  if (y0 < 0) y0 = 0;
  if (y1 > h_) y1 = h_;
  int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx);
  if (x0 < 0) x0 = 0;
  if (x1 > w_) x1 = w_;
  const int span = x1 - x0;
  if (span <= 0) return;
  float *cov = (float *)calloc(span, sizeof(float));
  if (!cov) return;
  float xs[64];
  const int SUB = 4;
  for (int y = y0; y < y1; y++) {
    memset(cov, 0, span * sizeof(float));
    for (int s = 0; s < SUB; s++) {
      const float sy = y + (s + 0.5f) / SUB;
      int k = 0;
      for (int i = 0, j = n - 1; i < n; j = i++) {
        const float ay = pts[i].y - oy, by = pts[j].y - oy;
        if ((ay > sy) != (by > sy) && k < 64) {
          const float ax = pts[i].x - ox, bx = pts[j].x - ox;
          xs[k++] = ax + (sy - ay) * (bx - ax) / (by - ay);
        }
      }
      for (int i = 1; i < k; i++) {   // insertion sort, k is small
        const float v = xs[i];
        int j = i - 1;
        while (j >= 0 && xs[j] > v) { xs[j + 1] = xs[j]; j--; }
        xs[j + 1] = v;
      }
      for (int i = 0; i + 1 < k; i += 2) {
        const float l = xs[i], r = xs[i + 1];
        int pl = (int)floorf(l), pr = (int)ceilf(r);
        if (pl < x0) pl = x0;
        if (pr > x1) pr = x1;
        for (int px = pl; px < pr; px++) {
          float c = fminf(r, px + 1.0f) - fmaxf(l, (float)px);
          if (c > 0) cov[px - x0] += c / SUB;
        }
      }
    }
    for (int i = 0; i < span; i++)
      if (cov[i] > 0.002f) blend(x0 + i, y, rgb, a * fminf(cov[i], 1.0f));
  }
  free(cov);
}

void Raster::stroke(const Pt *pts, int n, bool closed, float width, uint32_t rgb, float a) {
  const float hw = width / 2;
  const int segs = closed ? n : n - 1;
  for (int i = 0; i < segs; i++) {
    const Pt p = pts[i], q = pts[(i + 1) % n];
    const float dx = q.x - p.x, dy = q.y - p.y, len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-3f) continue;
    const float nx = -dy / len * hw, ny = dx / len * hw;
    const Pt quad[4] = {{p.x + nx, p.y + ny}, {q.x + nx, q.y + ny}, {q.x - nx, q.y - ny}, {p.x - nx, p.y - ny}};
    fill(quad, 4, rgb, a);
  }
  for (int i = 0; i < n; i++) {
    if (!closed && (i == 0 || i == n - 1)) continue;
    ellipse(pts[i].x, pts[i].y, hw, hw, rgb, a);
  }
}

static int rounded_rect_path(Pt *out, float x, float y, float w, float h, float r) {
  if (r <= 0) {
    out[0] = {x, y}; out[1] = {x + w, y}; out[2] = {x + w, y + h}; out[3] = {x, y + h};
    return 4;
  }
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  int n = 0;
  const float cx[4] = {x + w - r, x + w - r, x + r, x + r};
  const float cy[4] = {y + r, y + h - r, y + h - r, y + r};
  const float start[4] = {-90, 0, 90, 180};
  for (int c = 0; c < 4; c++)
    for (int i = 0; i <= 6; i++) {
      const float ang = (start[c] + i * 15) * (float)M_PI / 180;
      out[n++] = {cx[c] + cosf(ang) * r, cy[c] + sinf(ang) * r};
    }
  return n;
}

void Raster::rect(float x, float y, float w, float h, uint32_t rgb, float a, float radius) {
  Pt p[28];
  const int n = rounded_rect_path(p, x, y, w, h, radius);
  fill(p, n, rgb, a);
}

void Raster::rect_outline(float x, float y, float w, float h, float width, uint32_t rgb, float radius) {
  Pt p[28];
  const int n = rounded_rect_path(p, x, y, w, h, radius);
  stroke(p, n, true, width, rgb);
}

void Raster::ellipse(float cx, float cy, float rx, float ry, uint32_t rgb, float a) {
  Pt p[40];
  const int n = 40;
  for (int i = 0; i < n; i++) {
    const float t = i * 2 * (float)M_PI / n;
    p[i] = {cx + cosf(t) * rx, cy + sinf(t) * ry};
  }
  fill(p, n, rgb, a);
}

}  // namespace gridui

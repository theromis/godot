/**************************************************************************/
/*  rasterizer_canvas_software.cpp                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "rasterizer_canvas_software.h"

#include "mesh_storage_software.h"
#include "rasterizer_software.h"

#include "core/os/os.h"
#include "servers/rendering/dummy/storage/mesh_storage.h"
#include "servers/rendering/rendering_server.h"

// The pixel pipeline uses integer math only: texel coordinates are 16.16 fixed point,
// modulation is 8.8 fixed point per channel and pixels are blended without divisions.

// Modulation color, with 256 meaning 1.0. Values above 1.0 (HDR modulation) are kept up to 4.0.
struct FixedColor {
	uint32_t r = 256;
	uint32_t g = 256;
	uint32_t b = 256;
	uint32_t a = 256;

	_FORCE_INLINE_ bool is_white() const { return r == 256 && g == 256 && b == 256 && a == 256; }
};

static _FORCE_INLINE_ uint32_t _to_fixed_channel(float p_value) {
	return (uint32_t)(CLAMP(p_value, 0.0f, 4.0f) * 256.0f + 0.5f);
}

static _FORCE_INLINE_ FixedColor _to_fixed(const Color &p_color) {
	FixedColor color;
	color.r = _to_fixed_channel(p_color.r);
	color.g = _to_fixed_channel(p_color.g);
	color.b = _to_fixed_channel(p_color.b);
	color.a = _to_fixed_channel(p_color.a);
	return color;
}

static _FORCE_INLINE_ uint32_t _modulate(uint32_t p_texel, const FixedColor &p_color) {
	const uint32_t r = MIN(((p_texel & 0xFF) * p_color.r) >> 8, 255u);
	const uint32_t g = MIN((((p_texel >> 8) & 0xFF) * p_color.g) >> 8, 255u);
	const uint32_t b = MIN((((p_texel >> 16) & 0xFF) * p_color.b) >> 8, 255u);
	const uint32_t a = MIN(((p_texel >> 24) * p_color.a) >> 8, 255u);
	return r | (g << 8) | (b << 16) | (a << 24);
}

static _FORCE_INLINE_ void _blend_pixel(uint32_t &r_dst, uint32_t p_src, bool p_transparent) {
	const uint32_t src_a = p_src >> 24;
	if (src_a == 0) {
		return;
	}
	const uint32_t dst = r_dst;
	const uint32_t dst_a = dst >> 24;
	if (src_a == 255) {
		r_dst = (p_src & 0xFFFFFF) | ((p_transparent ? 255u : dst_a) << 24);
		return;
	}

	// Red and blue are blended together in one multiplication; x / 255 is computed as (x + 128 + (x >> 8)) >> 8.
	const uint32_t inv_a = 255 - src_a;
	uint32_t rb = (p_src & 0xFF00FF) * src_a + (dst & 0xFF00FF) * inv_a;
	rb = ((rb + 0x800080 + ((rb >> 8) & 0xFF00FF)) >> 8) & 0xFF00FF;
	uint32_t g = ((p_src >> 8) & 0xFF) * src_a + ((dst >> 8) & 0xFF) * inv_a;
	g = ((g + 0x80 + (g >> 8)) >> 8) & 0xFF;
	// Opaque targets keep their alpha, like the GL_ZERO, GL_ONE alpha blend of the other renderers.
	uint32_t a = dst_a;
	if (p_transparent) {
		const uint32_t dst_part = dst_a * inv_a;
		a = src_a + ((dst_part + 0x80 + (dst_part >> 8)) >> 8);
	}
	r_dst = rb | (g << 8) | (a << 24);
}

// Linear interpolation of two pixels with p_weight in 0..256, two channels per multiplication.
static _FORCE_INLINE_ uint32_t _lerp_pixel(uint32_t p_a, uint32_t p_b, uint32_t p_weight) {
	const uint32_t inv = 256 - p_weight;
	const uint32_t rb = (((p_a & 0xFF00FF) * inv + (p_b & 0xFF00FF) * p_weight) >> 8) & 0xFF00FF;
	const uint32_t ga = ((((p_a >> 8) & 0xFF00FF) * inv + ((p_b >> 8) & 0xFF00FF) * p_weight) >> 8) & 0xFF00FF;
	return rb | (ga << 8);
}

static _FORCE_INLINE_ int _wrap_texel(int p_coord, int p_size, bool p_repeat) {
	if (p_repeat) {
		p_coord %= p_size;
		return p_coord < 0 ? p_coord + p_size : p_coord;
	}
	return CLAMP(p_coord, 0, p_size - 1);
}

// A Sampler prepared for sampling with 16.16 fixed point texel coordinates.
struct FixedSampler {
	const uint32_t *pixels = nullptr;
	int width = 0;
	int height = 0;
	bool linear = false;
	bool repeat = false;
	bool clamp = false;
	int32_t min_u = 0;
	int32_t max_u = 0;
	int32_t min_v = 0;
	int32_t max_v = 0;
	int level = 0; // Coordinates are in base level texels and shifted down to this mipmap level.

	_FORCE_INLINE_ uint32_t sample(int32_t p_u, int32_t p_v) const {
		if (clamp) {
			p_u = CLAMP(p_u, min_u, max_u);
			p_v = CLAMP(p_v, min_v, max_v);
		}
		p_u >>= level;
		p_v >>= level;
		if (!linear) {
			const int x = _wrap_texel(p_u >> 16, width, repeat);
			const int y = _wrap_texel(p_v >> 16, height, repeat);
			return pixels[y * width + x];
		}
		// Sample between texel centers.
		p_u -= 0x8000;
		p_v -= 0x8000;
		const int x0 = p_u >> 16;
		const int y0 = p_v >> 16;
		const uint32_t fx = (p_u >> 8) & 0xFF;
		const uint32_t fy = (p_v >> 8) & 0xFF;
		const int xa = _wrap_texel(x0, width, repeat);
		const int xb = _wrap_texel(x0 + 1, width, repeat);
		const uint32_t *row_a = pixels + _wrap_texel(y0, height, repeat) * width;
		const uint32_t *row_b = pixels + _wrap_texel(y0 + 1, height, repeat) * width;
		return _lerp_pixel(_lerp_pixel(row_a[xa], row_a[xb], fx), _lerp_pixel(row_b[xa], row_b[xb], fx), fy);
	}
};

// p_texels_per_pixel is how many base level texels one screen pixel covers. Minified linear
// sampling uses a mipmap level: besides avoiding aliasing, reading a large texture sparsely
// misses the cache on almost every sample, which made minified sprites the slowest thing to draw.
static FixedSampler _prepare_sampler(const SoftwareImage *p_image, bool p_linear, bool p_repeat, bool p_use_clip_rect, const Rect2 &p_clip_rect, float p_texels_per_pixel = 1.0f) {
	FixedSampler sampler;
	if (p_linear) {
		while (p_texels_per_pixel >= 2.0f && sampler.level < 15) {
			p_texels_per_pixel *= 0.5f;
			sampler.level++;
		}
	}
	sampler.pixels = p_image->get_level(sampler.level, sampler.width, sampler.height);
	sampler.linear = p_linear;
	sampler.repeat = p_repeat;
	if (p_use_clip_rect) {
		// Keep samples inside the region so neighbors in an atlas don't bleed in.
		const float margin = (p_linear ? 0.5f : 0.001f) * (1 << sampler.level);
		float from_x = p_clip_rect.position.x + margin;
		float to_x = p_clip_rect.get_end().x - margin;
		float from_y = p_clip_rect.position.y + margin;
		float to_y = p_clip_rect.get_end().y - margin;
		if (from_x > to_x) {
			from_x = to_x = p_clip_rect.get_center().x;
		}
		if (from_y > to_y) {
			from_y = to_y = p_clip_rect.get_center().y;
		}
		sampler.clamp = true;
		sampler.min_u = (int32_t)(from_x * 65536.0f);
		sampler.max_u = (int32_t)(to_x * 65536.0f);
		sampler.min_v = (int32_t)(from_y * 65536.0f);
		sampler.max_v = (int32_t)(to_y * 65536.0f);
	}
	return sampler;
}

static _FORCE_INLINE_ int32_t _to_fixed_coord(float p_value) {
	// Clamped so that repeating textures far from the origin can't overflow.
	return (int32_t)CLAMP(p_value * 65536.0f, -2.0e9f, 2.0e9f);
}

static _FORCE_INLINE_ float _edge(const Vector2 &p_a, const Vector2 &p_b, const Vector2 &p_point) {
	return (p_b.x - p_a.x) * (p_point.y - p_a.y) - (p_b.y - p_a.y) * (p_point.x - p_a.x);
}

// Decides which of the two triangles sharing an edge owns the pixels exactly on it,
// so that shared edges are neither skipped nor blended twice.
static _FORCE_INLINE_ bool _edge_owns_ties(const Vector2 &p_a, const Vector2 &p_b) {
	const Vector2 d = p_b - p_a;
	return d.y > 0 || (d.y == 0 && d.x > 0);
}

void RasterizerCanvasSoftware::_draw_triangle(const Target &p_target, const Vertex &p_v0, const Vertex &p_v1, const Vertex &p_v2, const Sampler &p_sampler) {
	const Vertex *v0 = &p_v0;
	const Vertex *v1 = &p_v1;
	const Vertex *v2 = &p_v2;

	float area = _edge(v0->position, v1->position, v2->position);
	if (Math::abs(area) < CMP_EPSILON) {
		return;
	}
	if (area < 0) {
		SWAP(v1, v2);
		area = -area;
	}

	const Vector2 &p0 = v0->position;
	const Vector2 &p1 = v1->position;
	const Vector2 &p2 = v2->position;

	const Rect2i &clip = p_target.clip;
	const int min_x = MAX((int)Math::floor(MIN(p0.x, MIN(p1.x, p2.x))), clip.position.x);
	const int min_y = MAX((int)Math::floor(MIN(p0.y, MIN(p1.y, p2.y))), clip.position.y);
	const int max_x = MIN((int)Math::ceil(MAX(p0.x, MAX(p1.x, p2.x))), clip.get_end().x - 1);
	const int max_y = MIN((int)Math::ceil(MAX(p0.y, MAX(p1.y, p2.y))), clip.get_end().y - 1);
	if (min_x > max_x || min_y > max_y) {
		return;
	}

	const bool owns_ties0 = _edge_owns_ties(p1, p2);
	const bool owns_ties1 = _edge_owns_ties(p2, p0);
	const bool owns_ties2 = _edge_owns_ties(p0, p1);
	// Edge function steps per pixel along x.
	const float step0 = -(p2.y - p1.y);
	const float step1 = -(p0.y - p2.y);
	const float step2 = -(p1.y - p0.y);
	const float inv_area = 1.0f / area;

	// Attributes are affine across the triangle: value(x, y) = value at a corner + gradient * offset.
	auto gradient = [&](float p_a0, float p_a1, float p_a2, float &r_dx, float &r_dy) {
		r_dx = ((p_a1 - p_a0) * (p2.y - p0.y) - (p_a2 - p_a0) * (p1.y - p0.y)) * inv_area;
		r_dy = ((p_a2 - p_a0) * (p1.x - p0.x) - (p_a1 - p_a0) * (p2.x - p0.x)) * inv_area;
	};

	const bool textured = p_sampler.image != nullptr;
	FixedSampler sampler;
	float u_dx = 0, u_dy = 0, v_dx = 0, v_dy = 0;
	Vector2 uv0;
	if (textured) {
		const Vector2 size(p_sampler.image->width, p_sampler.image->height);
		uv0 = v0->uv * size;
		const Vector2 uv1 = v1->uv * size;
		const Vector2 uv2 = v2->uv * size;
		gradient(uv0.x, uv1.x, uv2.x, u_dx, u_dy);
		gradient(uv0.y, uv1.y, uv2.y, v_dx, v_dy);
		const float texels_per_pixel = MAX(Math::sqrt(u_dx * u_dx + v_dx * v_dx), Math::sqrt(u_dy * u_dy + v_dy * v_dy));
		sampler = _prepare_sampler(p_sampler.image, p_sampler.linear, p_sampler.repeat, p_sampler.use_clip_rect, p_sampler.clip_rect, texels_per_pixel);
	}

	// Vertex colors are often all the same (sprites, rects), in which case nothing is interpolated.
	// Otherwise (e.g. antialiased edges) each channel is interpolated as 8.8 fixed point scaled by 65536.
	const bool flat_color = v0->color == v1->color && v0->color == v2->color;
	const FixedColor flat = _to_fixed(v0->color);
	int32_t color_step[4] = {};
	float color_dx[4] = {};
	float color_dy[4] = {};
	if (!flat_color) {
		for (int c = 0; c < 4; c++) {
			gradient(v0->color[c], v1->color[c], v2->color[c], color_dx[c], color_dy[c]);
			color_step[c] = (int32_t)(color_dx[c] * 256.0f * 65536.0f);
		}
	}

	const int32_t u_step = _to_fixed_coord(u_dx);
	const int32_t v_step = _to_fixed_coord(v_dx);
	uint32_t *pixels = p_target.image->pixels.ptr();
	const int stride = p_target.image->width;
	const int columns = max_x - min_x + 1;
	const float steps[3] = { step0, step1, step2 };

	for (int y = min_y; y <= max_y; y++) {
		const Vector2 row_start(min_x + 0.5f, y + 0.5f);
		const float row_w[3] = { _edge(p1, p2, row_start), _edge(p2, p0, row_start), _edge(p0, p1, row_start) };

		// Find the span of the row inside all three edges instead of testing every pixel of the
		// bounding box, which is mostly empty for thin or rotated triangles. The span is widened by
		// one pixel on each side because of rounding; pixels are still tested exactly below.
		int first = 0;
		int last = columns - 1;
		for (int e = 0; e < 3; e++) {
			// Clamped before converting, as nearly vertical edges give huge values.
			if (steps[e] > 0) {
				first = MAX(first, (int)CLAMP(Math::ceil(-row_w[e] / steps[e]), -1.0f, (float)columns) - 1);
			} else if (steps[e] < 0) {
				last = MIN(last, (int)CLAMP(Math::floor(row_w[e] / -steps[e]), -1.0f, (float)columns) + 1);
			} else if (row_w[e] < 0) {
				last = -1;
			}
		}
		first = MAX(first, 0);
		if (first > last) {
			continue;
		}

		const int x_begin = min_x + first;
		float w0 = row_w[0] + step0 * first;
		float w1 = row_w[1] + step1 * first;
		float w2 = row_w[2] + step2 * first;
		const Vector2 offset = Vector2(x_begin + 0.5f, y + 0.5f) - p0;
		int32_t u = textured ? _to_fixed_coord(uv0.x + u_dx * offset.x + u_dy * offset.y) : 0;
		int32_t v = textured ? _to_fixed_coord(uv0.y + v_dx * offset.x + v_dy * offset.y) : 0;
		int32_t color[4] = {};
		if (!flat_color) {
			for (int c = 0; c < 4; c++) {
				color[c] = (int32_t)((v0->color[c] + color_dx[c] * offset.x + color_dy[c] * offset.y) * 256.0f * 65536.0f);
			}
		}
		uint32_t *row = pixels + y * stride;

		for (int x = x_begin; x <= min_x + last; x++, w0 += step0, w1 += step1, w2 += step2, u += u_step, v += v_step) {
			FixedColor modulate = flat;
			if (!flat_color) {
				modulate.r = CLAMP(color[0] >> 16, 0, 1024);
				modulate.g = CLAMP(color[1] >> 16, 0, 1024);
				modulate.b = CLAMP(color[2] >> 16, 0, 1024);
				modulate.a = CLAMP(color[3] >> 16, 0, 1024);
				for (int c = 0; c < 4; c++) {
					color[c] += color_step[c];
				}
			}
			const bool inside = w0 >= 0 && w1 >= 0 && w2 >= 0 && (w0 != 0 || owns_ties0) && (w1 != 0 || owns_ties1) && (w2 != 0 || owns_ties2);
			if (!inside) {
				continue;
			}

			const uint32_t texel = textured ? sampler.sample(u, v) : 0xFFFFFFFF;
			_blend_pixel(row[x], modulate.is_white() ? texel : _modulate(texel, modulate), p_target.transparent);
		}
	}
}

void RasterizerCanvasSoftware::_draw_point(const Target &p_target, const Vertex &p_point, const Sampler &p_sampler) {
	const int x = (int)Math::floor(p_point.position.x);
	const int y = (int)Math::floor(p_point.position.y);
	if (!p_target.clip.has_point(Point2i(x, y))) {
		return;
	}
	uint32_t texel = 0xFFFFFFFF;
	if (p_sampler.image) {
		const FixedSampler sampler = _prepare_sampler(p_sampler.image, p_sampler.linear, p_sampler.repeat, p_sampler.use_clip_rect, p_sampler.clip_rect);
		texel = sampler.sample(_to_fixed_coord(p_point.uv.x * p_sampler.image->width), _to_fixed_coord(p_point.uv.y * p_sampler.image->height));
	}
	_blend_pixel(p_target.image->pixels[y * p_target.image->width + x], _modulate(texel, _to_fixed(p_point.color)), p_target.transparent);
}

void RasterizerCanvasSoftware::_draw_line(const Target &p_target, const Vertex &p_from, const Vertex &p_to, const Sampler &p_sampler) {
	// One pixel wide lines, as with the other renderers' line primitives.
	const Vector2 delta = p_to.position - p_from.position;
	const int steps = MAX((int)Math::ceil(MAX(Math::abs(delta.x), Math::abs(delta.y))), 1);
	for (int i = 0; i <= steps; i++) {
		const float t = (float)i / steps;
		Vertex point;
		point.position = p_from.position + delta * t;
		point.uv = p_from.uv.lerp(p_to.uv, t);
		point.color = p_from.color.lerp(p_to.color, t);
		_draw_point(p_target, point, p_sampler);
	}
}

void RasterizerCanvasSoftware::_draw_rect(const Target &p_target, const Transform2D &p_xform, const Rect2 &p_rect, const Rect2 &p_uv_rect, bool p_transpose, const Color &p_modulate, const Sampler &p_sampler) {
	const bool axis_aligned = p_xform.columns[0].y == 0 && p_xform.columns[1].x == 0;
	if (!axis_aligned) {
		Vertex v[4];
		const Vector2 corners[4] = { Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1) };
		for (int i = 0; i < 4; i++) {
			v[i].position = p_xform.xform(p_rect.position + p_rect.size * corners[i]);
			const Vector2 t = p_transpose ? Vector2(corners[i].y, corners[i].x) : corners[i];
			v[i].uv = p_uv_rect.position + p_uv_rect.size * t;
			v[i].color = p_modulate;
		}
		_draw_triangle(p_target, v[0], v[1], v[2], p_sampler);
		_draw_triangle(p_target, v[0], v[2], v[3], p_sampler);
		return;
	}

	// Fast path for rects that stay axis-aligned on screen, which covers UI and text.
	const Vector2 from = p_xform.xform(p_rect.position);
	const Vector2 to = p_xform.xform(p_rect.get_end());
	if (from.x == to.x || from.y == to.y) {
		return;
	}

	const Rect2i &clip = p_target.clip;
	// Pixels whose centers are inside the rect.
	const int min_x = MAX((int)Math::ceil(MIN(from.x, to.x) - 0.5f), clip.position.x);
	const int max_x = MIN((int)Math::ceil(MAX(from.x, to.x) - 0.5f), clip.get_end().x) - 1;
	const int min_y = MAX((int)Math::ceil(MIN(from.y, to.y) - 0.5f), clip.position.y);
	const int max_y = MIN((int)Math::ceil(MAX(from.y, to.y) - 0.5f), clip.get_end().y) - 1;
	if (min_x > max_x || min_y > max_y) {
		return;
	}

	uint32_t *pixels = p_target.image->pixels.ptr();
	const int stride = p_target.image->width;
	const FixedColor modulate = _to_fixed(p_modulate);
	const bool transparent = p_target.transparent;

	if (!p_sampler.image) {
		const uint32_t pixel = _modulate(0xFFFFFFFF, modulate);
		for (int y = min_y; y <= max_y; y++) {
			uint32_t *row = pixels + y * stride;
			for (int x = min_x; x <= max_x; x++) {
				_blend_pixel(row[x], pixel, transparent);
			}
		}
		return;
	}

	// Texel coordinates are affine in screen space: the rect's local coordinates (0..1) map to the
	// UV rect, swapped when transposed.
	const int base_width = p_sampler.image->width;
	const int base_height = p_sampler.image->height;
	const float inv_w = 1.0f / (to.x - from.x);
	const float inv_h = 1.0f / (to.y - from.y);
	const float texels_u = p_uv_rect.size.x * base_width;
	const float texels_v = p_uv_rect.size.y * base_height;
	const float u_dx = p_transpose ? 0.0f : texels_u * inv_w;
	const float u_dy = p_transpose ? texels_u * inv_h : 0.0f;
	const float v_dx = p_transpose ? texels_v * inv_w : 0.0f;
	const float v_dy = p_transpose ? 0.0f : texels_v * inv_h;
	const float u_origin = p_uv_rect.position.x * base_width;
	const float v_origin = p_uv_rect.position.y * base_height;
	const float texels_per_pixel = MAX(Math::abs(u_dx) + Math::abs(v_dx), Math::abs(u_dy) + Math::abs(v_dy));
	const FixedSampler sampler = _prepare_sampler(p_sampler.image, p_sampler.linear, p_sampler.repeat, p_sampler.use_clip_rect, p_sampler.clip_rect, texels_per_pixel);
	const int32_t u_step = _to_fixed_coord(u_dx);
	const int32_t v_step = _to_fixed_coord(v_dx);
	const bool white = modulate.is_white();

	// Text and unscaled sprites usually map texels to pixels 1:1 with samples at texel centers,
	// where linear filtering gives exactly the nearest texel, so a single texel is read instead of four.
	FixedSampler row_sampler = sampler;
	if (sampler.linear && sampler.level == 0 && !p_transpose && u_step == 0x10000 && v_step == 0 && Math::is_equal_approx(v_dy, 1.0f)) {
		const int32_t u_first = _to_fixed_coord(u_origin + u_dx * (min_x + 0.5f - from.x));
		const int32_t v_first = _to_fixed_coord(v_origin + v_dy * (min_y + 0.5f - from.y));
		auto at_center = [](int32_t p_coord) { return Math::abs((p_coord & 0xFFFF) - 0x8000) <= 16; };
		if (at_center(u_first) && at_center(v_first)) {
			row_sampler.linear = false;
		}
	}

	for (int y = min_y; y <= max_y; y++) {
		const float dx = min_x + 0.5f - from.x;
		const float dy = y + 0.5f - from.y;
		int32_t u = _to_fixed_coord(u_origin + u_dx * dx + u_dy * dy);
		int32_t v = _to_fixed_coord(v_origin + v_dx * dx + v_dy * dy);
		uint32_t *row = pixels + y * stride;
		for (int x = min_x; x <= max_x; x++, u += u_step, v += v_step) {
			const uint32_t texel = row_sampler.sample(u, v);
			_blend_pixel(row[x], white ? texel : _modulate(texel, modulate), transparent);
		}
	}
}

void RasterizerCanvasSoftware::_draw_nine_patch(const Target &p_target, const Transform2D &p_xform, const Item::CommandNinePatch *p_command, const Color &p_modulate, Sampler p_sampler) {
	// TODO: Support the tile axis modes; all of them are drawn as stretch for now.
	const Color modulate = p_command->color * p_modulate;
	if (!p_sampler.image || p_sampler.image->is_empty()) {
		_draw_rect(p_target, p_xform, p_command->rect, Rect2(0, 0, 1, 1), false, modulate, Sampler());
		return;
	}

	const Vector2 texture_size(p_sampler.image->width, p_sampler.image->height);
	Rect2 source = p_command->source;
	if (source.size == Vector2()) {
		source = Rect2(Vector2(), texture_size);
	}
	const Rect2 &rect = p_command->rect;

	const float dst_x[4] = { rect.position.x, rect.position.x + p_command->margin[SIDE_LEFT], rect.get_end().x - p_command->margin[SIDE_RIGHT], rect.get_end().x };
	const float dst_y[4] = { rect.position.y, rect.position.y + p_command->margin[SIDE_TOP], rect.get_end().y - p_command->margin[SIDE_BOTTOM], rect.get_end().y };
	const float src_x[4] = { source.position.x, source.position.x + p_command->margin[SIDE_LEFT], source.get_end().x - p_command->margin[SIDE_RIGHT], source.get_end().x };
	const float src_y[4] = { source.position.y, source.position.y + p_command->margin[SIDE_TOP], source.get_end().y - p_command->margin[SIDE_BOTTOM], source.get_end().y };

	for (int j = 0; j < 3; j++) {
		for (int i = 0; i < 3; i++) {
			if (i == 1 && j == 1 && !p_command->draw_center) {
				continue;
			}
			const Rect2 dst(dst_x[i], dst_y[j], dst_x[i + 1] - dst_x[i], dst_y[j + 1] - dst_y[j]);
			const Rect2 src(src_x[i], src_y[j], src_x[i + 1] - src_x[i], src_y[j + 1] - src_y[j]);
			if (dst.size.x <= 0 || dst.size.y <= 0 || src.size.x <= 0 || src.size.y <= 0) {
				continue;
			}
			p_sampler.use_clip_rect = true;
			p_sampler.clip_rect = src;
			_draw_rect(p_target, p_xform, dst, Rect2(src.position / texture_size, src.size / texture_size), false, modulate, p_sampler);
		}
	}
}

void RasterizerCanvasSoftware::_draw_primitives(const Target &p_target, const Transform2D &p_xform, RSE::PrimitiveType p_primitive, const Primitives &p_primitives, const Color &p_modulate, const Sampler &p_sampler) {
	const int point_count = p_primitives.points.size();
	const bool use_indices = !p_primitives.indices.is_empty();
	const int count = use_indices ? MIN(p_primitives.count, p_primitives.indices.size()) : point_count;

	auto vertex_at = [&](int p_index) {
		const int point = use_indices ? p_primitives.indices[p_index] : p_index;
		Vertex vertex;
		if (point < 0 || point >= point_count) {
			return vertex;
		}
		vertex.position = p_xform.xform(p_primitives.points[point]);
		if (p_primitives.colors.size() == point_count) {
			vertex.color = p_primitives.colors[point] * p_modulate;
		} else if (p_primitives.colors.size() == 1) {
			vertex.color = p_primitives.colors[0] * p_modulate;
		} else {
			vertex.color = p_modulate;
		}
		if (p_primitives.uvs.size() == point_count) {
			vertex.uv = p_primitives.uvs[point];
		}
		return vertex;
	};

	switch (p_primitive) {
		case RSE::PRIMITIVE_POINTS: {
			for (int i = 0; i < count; i++) {
				_draw_point(p_target, vertex_at(i), p_sampler);
			}
		} break;
		case RSE::PRIMITIVE_LINES: {
			for (int i = 0; i + 1 < count; i += 2) {
				_draw_line(p_target, vertex_at(i), vertex_at(i + 1), p_sampler);
			}
		} break;
		case RSE::PRIMITIVE_LINE_STRIP: {
			for (int i = 0; i + 1 < count; i++) {
				_draw_line(p_target, vertex_at(i), vertex_at(i + 1), p_sampler);
			}
		} break;
		case RSE::PRIMITIVE_TRIANGLES: {
			for (int i = 0; i + 2 < count; i += 3) {
				_draw_triangle(p_target, vertex_at(i), vertex_at(i + 1), vertex_at(i + 2), p_sampler);
			}
		} break;
		case RSE::PRIMITIVE_TRIANGLE_STRIP: {
			for (int i = 0; i + 2 < count; i++) {
				_draw_triangle(p_target, vertex_at(i), vertex_at(i + 1), vertex_at(i + 2), p_sampler);
			}
		} break;
		default:
			break;
	}
}

void RasterizerCanvasSoftware::_draw_mesh(const Target &p_target, const Transform2D &p_xform, RID p_mesh, const Color &p_modulate, const Sampler &p_sampler) {
	RendererDummy::MeshStorage *mesh_storage = RendererDummy::MeshStorage::get_singleton();
	if (!mesh_storage->owns_mesh(p_mesh)) {
		return;
	}

	const int surface_count = mesh_storage->mesh_get_surface_count(p_mesh);
	LocalVector<MeshSurfaceCache> &cache = mesh_cache[p_mesh];
	cache.resize(surface_count);

	for (int i = 0; i < surface_count; i++) {
		const RenderingServerTypes::SurfaceData surface = mesh_storage->mesh_get_surface(p_mesh, i);
		MeshSurfaceCache &surface_cache = cache[i];
		if (surface_cache.vertex_data.ptr() != surface.vertex_data.ptr() || surface_cache.vertex_data.size() != surface.vertex_data.size()) {
			// Vector is copy-on-write, so new vertex data means a new buffer.
			surface_cache.vertex_data = surface.vertex_data;
			Primitives &primitives = surface_cache.primitives;
			const Array arrays = RenderingServer::get_singleton()->mesh_create_arrays_from_surface_data(surface);

			primitives.primitive = surface.primitive;
			primitives.points.clear();
			const Variant &vertices = arrays[RSE::ARRAY_VERTEX];
			if (vertices.get_type() == Variant::PACKED_VECTOR2_ARRAY) {
				primitives.points = vertices;
			} else if (vertices.get_type() == Variant::PACKED_VECTOR3_ARRAY) {
				const PackedVector3Array vertices_3d = vertices;
				primitives.points.resize(vertices_3d.size());
				for (int j = 0; j < vertices_3d.size(); j++) {
					primitives.points.write[j] = Vector2(vertices_3d[j].x, vertices_3d[j].y);
				}
			}
			primitives.colors = arrays[RSE::ARRAY_COLOR].get_type() == Variant::PACKED_COLOR_ARRAY ? PackedColorArray(arrays[RSE::ARRAY_COLOR]) : PackedColorArray();
			primitives.uvs = arrays[RSE::ARRAY_TEX_UV].get_type() == Variant::PACKED_VECTOR2_ARRAY ? PackedVector2Array(arrays[RSE::ARRAY_TEX_UV]) : PackedVector2Array();
			primitives.indices = arrays[RSE::ARRAY_INDEX].get_type() == Variant::PACKED_INT32_ARRAY ? PackedInt32Array(arrays[RSE::ARRAY_INDEX]) : PackedInt32Array();
			primitives.count = primitives.indices.is_empty() ? primitives.points.size() : primitives.indices.size();
		}

		_draw_primitives(p_target, p_xform, surface_cache.primitives.primitive, surface_cache.primitives, p_modulate, p_sampler);
	}
}

RendererCanvasRender::PolygonID RasterizerCanvasSoftware::request_polygon(const Vector<int> &p_indices, const Vector<Point2> &p_points, const Vector<Color> &p_colors, const Vector<Point2> &p_uvs, const Vector<int> &p_bones, const Vector<float> &p_weights, int p_count) {
	// TODO: Support skinning with bones and weights.
	Primitives polygon;
	polygon.indices = p_indices;
	polygon.points = p_points;
	polygon.colors = p_colors;
	polygon.uvs = p_uvs;
	polygon.count = p_count < 0 ? p_indices.size() : p_count;

	const PolygonID id = polygon_id_counter++;
	polygons.insert(id, polygon);
	return id;
}

void RasterizerCanvasSoftware::free_polygon(PolygonID p_polygon) {
	polygons.erase(p_polygon);
}

void RasterizerCanvasSoftware::_draw_item(SoftwareImage &p_image, bool p_transparent, const DrawRecord &p_record, const Rect2i &p_limit) {
	TextureStorageSoftware *texture_storage = TextureStorageSoftware::get_singleton();
	const Item *ci = p_record.item;
	const Rect2i item_clip = p_record.clip.intersection(p_limit);

	Target target;
	target.image = &p_image;
	target.clip = item_clip;
	target.transparent = p_transparent;

	auto make_sampler = [&](RID p_texture) {
		Sampler sampler;
		if (p_texture.is_valid()) {
			sampler.image = texture_storage->get_texture_image(p_texture);
			if (sampler.image && sampler.image->is_empty()) {
				sampler.image = nullptr;
			}
		}
		sampler.linear = p_record.linear;
		sampler.repeat = p_record.repeat;
		return sampler;
	};

	const Color &base_color = p_record.modulate;
	Transform2D draw_transform;

	for (const Item::Command *c = ci->commands; c; c = c->next) {
		const Transform2D xform = p_record.transform * draw_transform;
		const uint64_t command_begin_usec = OS::get_singleton()->get_ticks_usec();

		switch (c->type) {
			case Item::Command::TYPE_RECT: {
				const Item::CommandRect *rect = static_cast<const Item::CommandRect *>(c);
				Sampler sampler = make_sampler(rect->texture);

				Rect2 dst_rect = rect->rect;
				if (dst_rect.size.x < 0) {
					dst_rect.position.x += dst_rect.size.x;
					dst_rect.size.x = -dst_rect.size.x;
				}
				if (dst_rect.size.y < 0) {
					dst_rect.position.y += dst_rect.size.y;
					dst_rect.size.y = -dst_rect.size.y;
				}

				Rect2 uv_rect(0, 0, 1, 1);
				if (sampler.image) {
					const Vector2 texture_size(sampler.image->width, sampler.image->height);
					if (rect->flags & CANVAS_RECT_REGION) {
						uv_rect = Rect2(rect->source.position / texture_size, rect->source.size / texture_size);
						if (rect->flags & CANVAS_RECT_CLIP_UV) {
							sampler.use_clip_rect = true;
							sampler.clip_rect = rect->source.abs();
						}
					}
					if (rect->flags & CANVAS_RECT_TILE) {
						sampler.repeat = true;
					}
				}
				if (rect->flags & CANVAS_RECT_FLIP_H) {
					uv_rect.position.x += uv_rect.size.x;
					uv_rect.size.x = -uv_rect.size.x;
				}
				if (rect->flags & CANVAS_RECT_FLIP_V) {
					uv_rect.position.y += uv_rect.size.y;
					uv_rect.size.y = -uv_rect.size.y;
				}

				// TODO: Support MSDF fonts and LCD subpixel antialiasing; they are drawn as regular textures.
				_draw_rect(target, xform, dst_rect, uv_rect, rect->flags & CANVAS_RECT_TRANSPOSE, rect->modulate * base_color, sampler);
			} break;

			case Item::Command::TYPE_NINEPATCH: {
				const Item::CommandNinePatch *nine_patch = static_cast<const Item::CommandNinePatch *>(c);
				_draw_nine_patch(target, xform, nine_patch, base_color, make_sampler(nine_patch->texture));
			} break;

			case Item::Command::TYPE_POLYGON: {
				const Item::CommandPolygon *polygon = static_cast<const Item::CommandPolygon *>(c);
				const Primitives *primitives = polygons.getptr(polygon->polygon.polygon_id);
				if (primitives) {
					_draw_primitives(target, xform, polygon->primitive, *primitives, base_color, make_sampler(polygon->texture));
				}
			} break;

			case Item::Command::TYPE_PRIMITIVE: {
				const Item::CommandPrimitive *primitive = static_cast<const Item::CommandPrimitive *>(c);
				const Sampler sampler = make_sampler(primitive->texture);
				Vertex v[4];
				for (uint32_t i = 0; i < primitive->point_count && i < 4; i++) {
					v[i].position = xform.xform(primitive->points[i]);
					v[i].uv = primitive->uvs[i];
					v[i].color = primitive->colors[i] * base_color;
				}
				switch (primitive->point_count) {
					case 1:
						_draw_point(target, v[0], sampler);
						break;
					case 2:
						_draw_line(target, v[0], v[1], sampler);
						break;
					case 3:
						_draw_triangle(target, v[0], v[1], v[2], sampler);
						break;
					case 4:
						_draw_triangle(target, v[0], v[1], v[2], sampler);
						_draw_triangle(target, v[0], v[2], v[3], sampler);
						break;
					default:
						break;
				}
			} break;

			case Item::Command::TYPE_MESH: {
				// TODO: Support skeletons through mesh instances.
				const Item::CommandMesh *mesh = static_cast<const Item::CommandMesh *>(c);
				_draw_mesh(target, xform * mesh->transform, mesh->mesh, mesh->modulate * base_color, make_sampler(mesh->texture));
			} break;

			case Item::Command::TYPE_MULTIMESH: {
				// Used by CPUParticles2D and MultiMeshInstance2D. Custom data is only read by shaders, so it's ignored.
				const Item::CommandMultiMesh *multimesh_command = static_cast<const Item::CommandMultiMesh *>(c);
				const MeshStorageSoftware::MultiMesh *multimesh = MeshStorageSoftware::get_singleton()->get_multimesh(multimesh_command->multimesh);
				if (multimesh && multimesh->mesh.is_valid()) {
					const Sampler sampler = make_sampler(multimesh_command->texture);
					const int count = multimesh->get_visible_count();
					for (int i = 0; i < count; i++) {
						_draw_mesh(target, xform * multimesh->get_transform_2d(i), multimesh->mesh, base_color * multimesh->get_color(i), sampler);
					}
				}
			} break;

			case Item::Command::TYPE_TRANSFORM: {
				draw_transform = static_cast<const Item::CommandTransform *>(c)->xform;
			} break;

			case Item::Command::TYPE_CLIP_IGNORE: {
				target.clip = static_cast<const Item::CommandClipIgnore *>(c)->ignore ? p_limit : item_clip;
			} break;

			default:
				// TODO: GPU particles and animation slices.
				break;
		}

		DrawCategory category = DRAW_CATEGORY_GEOMETRY;
		if (c->type == Item::Command::TYPE_RECT) {
			category = static_cast<const Item::CommandRect *>(c)->texture.is_valid() ? DRAW_CATEGORY_TEXTURE_RECT : DRAW_CATEGORY_COLOR_RECT;
		} else if (c->type == Item::Command::TYPE_NINEPATCH) {
			category = DRAW_CATEGORY_NINE_PATCH;
		}
		draw_category_usec[category] += OS::get_singleton()->get_ticks_usec() - command_begin_usec;
	}
}

// FNV-1a, used to detect items whose drawing changed since the previous frame.
static _FORCE_INLINE_ uint64_t _hash_bytes(uint64_t p_hash, const void *p_data, size_t p_size) {
	const uint8_t *bytes = static_cast<const uint8_t *>(p_data);
	for (size_t i = 0; i < p_size; i++) {
		p_hash = (p_hash ^ bytes[i]) * 1099511628211ULL;
	}
	return p_hash;
}

template <typename T>
static _FORCE_INLINE_ uint64_t _hash_value(uint64_t p_hash, const T &p_value) {
	return _hash_bytes(p_hash, &p_value, sizeof(T));
}

uint64_t RasterizerCanvasSoftware::_hash_record(const DrawRecord &p_record) const {
	TextureStorageSoftware *texture_storage = TextureStorageSoftware::get_singleton();
	uint64_t h = 14695981039346656037ULL;
	h = _hash_value(h, p_record.transform);
	h = _hash_value(h, p_record.modulate);
	h = _hash_value(h, p_record.clip);
	h = _hash_value(h, p_record.linear);
	h = _hash_value(h, p_record.repeat);

	auto hash_texture = [&](RID p_texture) {
		h = _hash_value(h, p_texture);
		h = _hash_value(h, texture_storage->get_texture_version(p_texture));
	};

	for (const Item::Command *c = p_record.item->commands; c; c = c->next) {
		h = _hash_value(h, c->type);
		switch (c->type) {
			case Item::Command::TYPE_RECT: {
				const Item::CommandRect *rect = static_cast<const Item::CommandRect *>(c);
				h = _hash_value(h, rect->rect);
				h = _hash_value(h, rect->modulate);
				h = _hash_value(h, rect->source);
				h = _hash_value(h, rect->flags);
				hash_texture(rect->texture);
			} break;
			case Item::Command::TYPE_NINEPATCH: {
				const Item::CommandNinePatch *nine_patch = static_cast<const Item::CommandNinePatch *>(c);
				h = _hash_value(h, nine_patch->rect);
				h = _hash_value(h, nine_patch->source);
				h = _hash_bytes(h, nine_patch->margin, sizeof(nine_patch->margin));
				h = _hash_value(h, nine_patch->draw_center);
				h = _hash_value(h, nine_patch->color);
				hash_texture(nine_patch->texture);
			} break;
			case Item::Command::TYPE_POLYGON: {
				// Polygons are immutable; changing one creates a new polygon ID.
				const Item::CommandPolygon *polygon = static_cast<const Item::CommandPolygon *>(c);
				h = _hash_value(h, polygon->polygon.polygon_id);
				h = _hash_value(h, polygon->primitive);
				hash_texture(polygon->texture);
			} break;
			case Item::Command::TYPE_PRIMITIVE: {
				const Item::CommandPrimitive *primitive = static_cast<const Item::CommandPrimitive *>(c);
				h = _hash_value(h, primitive->point_count);
				h = _hash_bytes(h, primitive->points, sizeof(primitive->points));
				h = _hash_bytes(h, primitive->uvs, sizeof(primitive->uvs));
				h = _hash_bytes(h, primitive->colors, sizeof(primitive->colors));
				hash_texture(primitive->texture);
			} break;
			case Item::Command::TYPE_MESH: {
				const Item::CommandMesh *mesh = static_cast<const Item::CommandMesh *>(c);
				h = _hash_value(h, mesh->mesh);
				h = _hash_value(h, mesh->transform);
				h = _hash_value(h, mesh->modulate);
				hash_texture(mesh->texture);
				// Mesh data is replaced, not modified, so the buffer pointers identify the contents.
				RendererDummy::MeshStorage *mesh_storage = RendererDummy::MeshStorage::get_singleton();
				if (mesh_storage->owns_mesh(mesh->mesh)) {
					const int surface_count = mesh_storage->mesh_get_surface_count(mesh->mesh);
					for (int i = 0; i < surface_count; i++) {
						const RenderingServerTypes::SurfaceData surface = mesh_storage->mesh_get_surface(mesh->mesh, i);
						h = _hash_value(h, surface.vertex_data.ptr());
						h = _hash_value(h, surface.attribute_data.ptr());
						h = _hash_value(h, surface.index_data.ptr());
					}
				}
			} break;
			case Item::Command::TYPE_MULTIMESH: {
				const Item::CommandMultiMesh *multimesh_command = static_cast<const Item::CommandMultiMesh *>(c);
				h = _hash_value(h, multimesh_command->multimesh);
				hash_texture(multimesh_command->texture);
				const MeshStorageSoftware::MultiMesh *multimesh = MeshStorageSoftware::get_singleton()->get_multimesh(multimesh_command->multimesh);
				if (multimesh) {
					h = _hash_value(h, multimesh->version);
					h = _hash_value(h, multimesh->mesh);
				}
			} break;
			case Item::Command::TYPE_TRANSFORM: {
				h = _hash_value(h, static_cast<const Item::CommandTransform *>(c)->xform);
			} break;
			case Item::Command::TYPE_CLIP_IGNORE: {
				h = _hash_value(h, static_cast<const Item::CommandClipIgnore *>(c)->ignore);
			} break;
			default:
				break;
		}
	}
	return h;
}

void RasterizerCanvasSoftware::_begin_frame(RID p_render_target) {
	if (pending.render_target == p_render_target) {
		return;
	}
	flush();

	pending.render_target = p_render_target;
	pending.records.clear();
	pending.clear = false;

	TextureStorageSoftware::RenderTarget *rt = TextureStorageSoftware::get_singleton()->get_render_target(p_render_target);
	if (rt && rt->clear_requested) {
		pending.clear = true;
		pending.clear_color = rt->clear_color;
		rt->clear_requested = false;
	}
}

void RasterizerCanvasSoftware::request_clear(RID p_render_target, const Color &p_color) {
	_begin_frame(p_render_target);
	pending.clear = true;
	pending.clear_color = p_color;
}

// Adds p_rect to r_rects, merging it with rects it touches.
static void _add_damage(LocalVector<Rect2i> &r_rects, Rect2i p_rect) {
	if (p_rect.size.x <= 0 || p_rect.size.y <= 0) {
		return;
	}
	for (uint32_t i = 0; i < r_rects.size();) {
		if (r_rects[i].grow(4).intersects(p_rect)) {
			p_rect = p_rect.merge(r_rects[i]);
			r_rects.remove_at_unordered(i);
			i = 0;
		} else {
			i++;
		}
	}
	r_rects.push_back(p_rect);
}

void RasterizerCanvasSoftware::flush(RID p_render_target) {
	if (pending.render_target.is_null() || (p_render_target.is_valid() && p_render_target != pending.render_target)) {
		return;
	}
	const RID rid = pending.render_target;
	pending.render_target = RID();

	TextureStorageSoftware *texture_storage = TextureStorageSoftware::get_singleton();
	TextureStorageSoftware::RenderTarget *rt = texture_storage->get_render_target(rid);
	if (!rt || rt->image.is_empty()) {
		return;
	}

	const uint64_t begin_usec = OS::get_singleton()->get_ticks_usec();
	const Size2i size(rt->image.width, rt->image.height);
	const Rect2i full_rect(Point2i(), size);

	for (DrawRecord &record : pending.records) {
		record.hash = _hash_record(record);
	}

	RenderTargetState &state = render_target_states[rid];
	// Without a clear, items are drawn on top of the previous contents, so everything must be drawn again.
	bool full = !pending.clear || !state.valid || !state.clear || state.clear_color != pending.clear_color || state.size != size || state.transparent != rt->is_transparent;

	LocalVector<Rect2i> damage;
	if (!full) {
		HashMap<const Item *, uint32_t> previous;
		for (uint32_t i = 0; i < state.entries.size(); i++) {
			previous.insert(state.entries[i].item, i);
		}

		int64_t last_index = -1;
		for (const DrawRecord &record : pending.records) {
			const uint32_t *index = previous.getptr(record.item);
			if (!index) {
				_add_damage(damage, record.bounds);
				continue;
			}
			if ((int64_t)*index < last_index) {
				// Drawing order changed; overlapping items would need to be redrawn anyway.
				full = true;
				break;
			}
			last_index = *index;
			const RenderTargetState::Entry &entry = state.entries[*index];
			if (entry.hash != record.hash || entry.bounds != record.bounds) {
				_add_damage(damage, entry.bounds);
				_add_damage(damage, record.bounds);
			}
			previous.erase(record.item);
		}
		for (const KeyValue<const Item *, uint32_t> &E : previous) {
			_add_damage(damage, state.entries[E.value].bounds);
		}

		int64_t damaged_area = 0;
		for (Rect2i &rect : damage) {
			rect = rect.intersection(full_rect);
			damaged_area += rect.get_area();
		}
		// Redrawing most of the screen in pieces costs more than redrawing it at once.
		if (damaged_area * 10 > (int64_t)size.width * size.height * 6) {
			full = true;
		}
	}

	if (full) {
		damage.clear();
		damage.push_back(full_rect);
	}

	RasterStats stats;
	uint64_t phase_usec = OS::get_singleton()->get_ticks_usec();
	stats.track_usec = phase_usec - begin_usec;

	Color clear_color = pending.clear_color;
	if (!rt->is_transparent) {
		clear_color.a = 1.0;
	}
	const uint32_t clear_pixel = clear_color.to_abgr32();

	int64_t drawn_pixels = 0;
	for (const Rect2i &rect : damage) {
		if (rect.size.x <= 0 || rect.size.y <= 0) {
			continue;
		}
		drawn_pixels += rect.get_area();
		if (pending.clear) {
			for (int y = rect.position.y; y < rect.get_end().y; y++) {
				uint32_t *row = rt->image.pixels.ptr() + y * size.width;
				for (int x = rect.position.x; x < rect.get_end().x; x++) {
					row[x] = clear_pixel;
				}
			}
		}
		uint64_t now_usec = OS::get_singleton()->get_ticks_usec();
		stats.clear_usec += now_usec - phase_usec;
		phase_usec = now_usec;

		for (const DrawRecord &record : pending.records) {
			if (full || record.bounds.intersects(rect)) {
				_draw_item(rt->image, rt->is_transparent, record, rect);
				stats.items_drawn++;
			}
		}
		now_usec = OS::get_singleton()->get_ticks_usec();
		stats.draw_usec += now_usec - phase_usec;
		phase_usec = now_usec;
	}

	rt->damaged = drawn_pixels > 0;
	if (rt->damaged) {
		rt->image.invalidate_mipmaps();
		texture_storage->render_target_mark_changed(rid);
	}

	state.valid = true;
	state.clear = pending.clear;
	state.clear_color = pending.clear_color;
	state.size = size;
	state.transparent = rt->is_transparent;
	state.entries.resize(pending.records.size());
	for (uint32_t i = 0; i < pending.records.size(); i++) {
		state.entries[i].item = pending.records[i].item;
		state.entries[i].hash = pending.records[i].hash;
		state.entries[i].bounds = pending.records[i].bounds;
	}
	pending.records.clear();

	if (RasterizerSoftware::get_singleton()) {
		stats.total_usec = OS::get_singleton()->get_ticks_usec() - begin_usec;
		for (int i = 0; i < DRAW_CATEGORY_MAX; i++) {
			stats.category_usec[i] = draw_category_usec[i];
			draw_category_usec[i] = 0;
		}
		stats.drawn_pixels = drawn_pixels;
		stats.total_pixels = (int64_t)size.width * size.height;
		RasterizerSoftware::get_singleton()->report_raster(stats);
	}
}

void RasterizerCanvasSoftware::render_target_freed(RID p_render_target) {
	if (pending.render_target == p_render_target) {
		pending.render_target = RID();
		pending.records.clear();
	}
	render_target_states.erase(p_render_target);
}

void RasterizerCanvasSoftware::canvas_render_items(RID p_to_render_target, Item *p_item_list, const Color &p_modulate, Light *p_light_list, Light *p_directional_list, const Transform2D &p_canvas_transform, RSE::CanvasItemTextureFilter p_default_filter, RSE::CanvasItemTextureRepeat p_default_repeat, bool p_snap_2d_vertices_to_pixel, bool &r_sdf_used, RenderingServerTypes::RenderInfo *r_render_info) {
	TextureStorageSoftware::RenderTarget *render_target = TextureStorageSoftware::get_singleton()->get_render_target(p_to_render_target);
	ERR_FAIL_NULL(render_target);
	if (render_target->image.is_empty()) {
		return;
	}

	_begin_frame(p_to_render_target);

	const Rect2i full_rect(0, 0, render_target->image.width, render_target->image.height);

	for (Item *ci = p_item_list; ci; ci = ci->next) {
		DrawRecord record;
		record.item = ci;
		record.transform = ci->final_transform;
		record.modulate = ci->final_modulate * p_modulate;

		record.clip = full_rect;
		if (ci->final_clip_owner) {
			const Rect2 &clip_rect = ci->final_clip_owner->final_clip_rect;
			const Point2i from(Math::round(clip_rect.position.x), Math::round(clip_rect.position.y));
			const Point2i to(Math::round(clip_rect.get_end().x), Math::round(clip_rect.get_end().y));
			record.clip = full_rect.intersection(Rect2i(from, to - from));
		}

		const RSE::CanvasItemTextureFilter filter = ci->texture_filter == RSE::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT ? p_default_filter : ci->texture_filter;
		const RSE::CanvasItemTextureRepeat repeat = ci->texture_repeat == RSE::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT ? p_default_repeat : ci->texture_repeat;
		record.linear = filter != RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST && filter != RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS && filter != RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS_ANISOTROPIC;
		record.repeat = repeat != RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED;

		for (const Item::Command *c = ci->commands; c; c = c->next) {
			if (c->type == Item::Command::TYPE_CLIP_IGNORE && static_cast<const Item::CommandClipIgnore *>(c)->ignore) {
				record.ignores_clip = true;
			}
		}

		// Grow by a couple of pixels for antialiased edges and linear filtering.
		const Rect2 &global_rect = ci->global_rect_cache;
		const Point2i from(Math::floor(global_rect.position.x) - 2, Math::floor(global_rect.position.y) - 2);
		const Point2i to(Math::ceil(global_rect.get_end().x) + 2, Math::ceil(global_rect.get_end().y) + 2);
		record.bounds = full_rect.intersection(Rect2i(from, to - from));
		if (!record.ignores_clip) {
			record.bounds = record.bounds.intersection(record.clip);
		}

		pending.records.push_back(record);
	}
}

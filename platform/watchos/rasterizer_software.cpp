/**************************************************************************/
/*  rasterizer_software.cpp                                               */
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

#include "rasterizer_software.h"

#include "mesh_storage_software.h"
#include "rasterizer_canvas_software.h"

#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "main/performance.h"
#include "servers/display/display_server.h"
#include "servers/rendering/dummy/storage/mesh_storage.h"
#include "servers/rendering/dummy/storage/utilities.h"

namespace {

// The dummy utilities only know about the dummy texture storage, so freeing textures is redirected here.
class UtilitiesSoftware : public RendererDummy::Utilities {
public:
	bool free(RID p_rid) override {
		TextureStorageSoftware *texture_storage = TextureStorageSoftware::get_singleton();
		if (texture_storage->owns_software_texture(p_rid)) {
			texture_storage->texture_free(p_rid);
			return true;
		}
		return RendererDummy::Utilities::free(p_rid);
	}
};

} // namespace

// Returns true if the screen was resized.
static bool _resize_screen(SoftwareImage &r_screen) {
	const Size2i size = DisplayServer::get_singleton()->window_get_size();
	if (r_screen.width != size.width || r_screen.height != size.height) {
		r_screen.resize(size.width, size.height);
		r_screen.fill(0xFF000000);
		return true;
	}
	return false;
}

// Nearest-neighbor copy of p_source (in normalized coordinates of p_image) into p_dst_rect of r_screen.
static void _copy_to_screen(SoftwareImage &r_screen, const SoftwareImage &p_image, const Rect2 &p_source, const Rect2i &p_dst_rect) {
	const Rect2i dst = p_dst_rect.intersection(Rect2i(0, 0, r_screen.width, r_screen.height));
	if (dst.size.x <= 0 || dst.size.y <= 0 || p_image.is_empty()) {
		return;
	}
	for (int y = dst.position.y; y < dst.get_end().y; y++) {
		const float ty = (y + 0.5f - p_dst_rect.position.y) / p_dst_rect.size.y;
		const int sy = CLAMP((int)((p_source.position.y + p_source.size.y * ty) * p_image.height), 0, p_image.height - 1);
		for (int x = dst.position.x; x < dst.get_end().x; x++) {
			const float tx = (x + 0.5f - p_dst_rect.position.x) / p_dst_rect.size.x;
			const int sx = CLAMP((int)((p_source.position.x + p_source.size.x * tx) * p_image.width), 0, p_image.width - 1);
			r_screen.pixels[y * r_screen.width + x] = p_image.pixels[sy * p_image.width + sx] | 0xFF000000;
		}
	}
}

void RasterizerSoftware::set_boot_image_with_stretch(const Ref<Image> &p_image, const Color &p_color, RSE::SplashStretchMode p_stretch_mode, bool p_use_filter) {
	_resize_screen(screen);
	screen_changed = true;
	Color color = p_color;
	color.a = 1.0;
	screen.fill(color.to_abgr32());

	if (p_image.is_valid() && !p_image->is_empty()) {
		SoftwareImage image;
		image.set_from_image(p_image);
		const Rect2 rect = RenderingServerTypes::get_splash_stretched_screen_rect(p_image->get_size(), Size2(screen.width, screen.height), p_stretch_mode);
		// Blend over the background color, since splash images often have transparency.
		const Rect2i dst = Rect2i(rect).intersection(Rect2i(0, 0, screen.width, screen.height));
		for (int y = dst.position.y; y < dst.get_end().y; y++) {
			const int sy = CLAMP((int)((y + 0.5f - rect.position.y) / rect.size.y * image.height), 0, image.height - 1);
			for (int x = dst.position.x; x < dst.get_end().x; x++) {
				const int sx = CLAMP((int)((x + 0.5f - rect.position.x) / rect.size.x * image.width), 0, image.width - 1);
				const uint32_t src = image.pixels[sy * image.width + sx];
				const uint32_t a = src >> 24;
				uint32_t &dst_pixel = screen.pixels[y * screen.width + x];
				uint32_t out = 0xFF000000;
				for (int shift = 0; shift < 24; shift += 8) {
					const uint32_t s = (src >> shift) & 0xFF;
					const uint32_t d = (dst_pixel >> shift) & 0xFF;
					out |= ((s * a + d * (255 - a) + 127) / 255) << shift;
				}
				dst_pixel = out;
			}
		}
	}

	DisplayServer::get_singleton()->swap_buffers();
	screen_changed = false;
}

void RasterizerSoftware::blit_render_targets_to_screen(DisplayServerEnums::WindowID p_screen, const RenderingServerTypes::BlitToScreen *p_render_targets, int p_amount) {
	RasterizerCanvasSoftware::get_singleton()->flush();
	const bool resized = _resize_screen(screen);
	TextureStorageSoftware *texture_storage = TextureStorageSoftware::get_singleton();
	for (int i = 0; i < p_amount; i++) {
		TextureStorageSoftware::RenderTarget *rt = texture_storage->get_render_target(p_render_targets[i].render_target);
		ERR_CONTINUE(!rt);
		if (!resized && !rt->damaged) {
			continue;
		}
		_copy_to_screen(screen, rt->image, p_render_targets[i].src_rect, p_render_targets[i].dst_rect);
		rt->damaged = false;
		screen_changed = true;
	}
}

void RasterizerSoftware::report_raster(const RasterStats &p_stats) {
	frame_stats.total_usec += p_stats.total_usec;
	frame_stats.track_usec += p_stats.track_usec;
	frame_stats.clear_usec += p_stats.clear_usec;
	frame_stats.draw_usec += p_stats.draw_usec;
	for (int i = 0; i < DRAW_CATEGORY_MAX; i++) {
		frame_stats.category_usec[i] += p_stats.category_usec[i];
	}
	frame_stats.items_drawn += p_stats.items_drawn;
	frame_stats.drawn_pixels += p_stats.drawn_pixels;
	frame_stats.total_pixels += p_stats.total_pixels;
}

void RasterizerSoftware::end_frame(bool p_present) {
	RasterizerCanvasSoftware::get_singleton()->flush();

	uint64_t present_usec = 0;
	if (p_present && screen_changed) {
		const uint64_t begin_usec = OS::get_singleton()->get_ticks_usec();
		DisplayServer::get_singleton()->swap_buffers();
		present_usec = OS::get_singleton()->get_ticks_usec() - begin_usec;
		screen_changed = false;
	}

	// Exponential moving average, so the monitors are readable at 60 FPS.
	constexpr double SMOOTHING = 0.1;
	raster_ms += (frame_stats.total_usec / 1000.0 - raster_ms) * SMOOTHING;
	track_ms += (frame_stats.track_usec / 1000.0 - track_ms) * SMOOTHING;
	clear_ms += (frame_stats.clear_usec / 1000.0 - clear_ms) * SMOOTHING;
	draw_ms += (frame_stats.draw_usec / 1000.0 - draw_ms) * SMOOTHING;
	present_ms += (present_usec / 1000.0 - present_ms) * SMOOTHING;
	const double redrawn = frame_stats.total_pixels > 0 ? 100.0 * frame_stats.drawn_pixels / frame_stats.total_pixels : 0.0;
	redrawn_percent += (redrawn - redrawn_percent) * SMOOTHING;
	items_drawn += (frame_stats.items_drawn - items_drawn) * SMOOTHING;
	for (int i = 0; i < DRAW_CATEGORY_MAX; i++) {
		category_ms[i] += (frame_stats.category_usec[i] / 1000.0 - category_ms[i]) * SMOOTHING;
	}
	frame_stats = RasterStats();
}

double RasterizerSoftware::_get_raster_ms() {
	return singleton ? singleton->raster_ms : 0.0;
}

double RasterizerSoftware::_get_track_ms() {
	return singleton ? singleton->track_ms : 0.0;
}

double RasterizerSoftware::_get_clear_ms() {
	return singleton ? singleton->clear_ms : 0.0;
}

double RasterizerSoftware::_get_draw_ms() {
	return singleton ? singleton->draw_ms : 0.0;
}

double RasterizerSoftware::_get_items_drawn() {
	return singleton ? singleton->items_drawn : 0.0;
}

double RasterizerSoftware::_get_present_ms() {
	return singleton ? singleton->present_ms : 0.0;
}

double RasterizerSoftware::_get_redrawn_percent() {
	return singleton ? singleton->redrawn_percent : 0.0;
}

RasterizerSoftware::RasterizerSoftware() {
	// Delete the dummy implementations before creating the replacements,
	// as their destructors clear the singletons that the replacements set.
	memdelete(canvas);
	canvas = memnew(RasterizerCanvasSoftware);
	memdelete(texture_storage);
	texture_storage = memnew(TextureStorageSoftware);
	memdelete(utilities);
	utilities = memnew(UtilitiesSoftware);
	memdelete(mesh_storage);
	mesh_storage = memnew(MeshStorageSoftware);

	singleton = this;

	Performance *performance = Performance::get_singleton();
	if (performance) {
		for (const KeyValue<StringName, double (*)()> &E : _get_monitors()) {
			if (!performance->has_custom_monitor(E.key)) {
				performance->add_custom_monitor(E.key, callable_mp_static(E.value), Vector<Variant>());
			}
		}
	}
}

HashMap<StringName, double (*)()> RasterizerSoftware::_get_monitors() {
	HashMap<StringName, double (*)()> monitors;
	monitors.insert("software_renderer/raster_ms", &_get_raster_ms);
	monitors.insert("software_renderer/track_ms", &_get_track_ms);
	monitors.insert("software_renderer/clear_ms", &_get_clear_ms);
	monitors.insert("software_renderer/draw_ms", &_get_draw_ms);
	monitors.insert("software_renderer/items_drawn", &_get_items_drawn);
	monitors.insert("software_renderer/draw_texture_rect_ms", &_get_category_ms<DRAW_CATEGORY_TEXTURE_RECT>);
	monitors.insert("software_renderer/draw_color_rect_ms", &_get_category_ms<DRAW_CATEGORY_COLOR_RECT>);
	monitors.insert("software_renderer/draw_geometry_ms", &_get_category_ms<DRAW_CATEGORY_GEOMETRY>);
	monitors.insert("software_renderer/draw_nine_patch_ms", &_get_category_ms<DRAW_CATEGORY_NINE_PATCH>);
	monitors.insert("software_renderer/present_ms", &_get_present_ms);
	monitors.insert("software_renderer/redrawn_percent", &_get_redrawn_percent);
	return monitors;
}

RasterizerSoftware::~RasterizerSoftware() {
	Performance *performance = Performance::get_singleton();
	if (performance) {
		for (const KeyValue<StringName, double (*)()> &E : _get_monitors()) {
			if (performance->has_custom_monitor(E.key)) {
				performance->remove_custom_monitor(E.key);
			}
		}
	}
	singleton = nullptr;
}

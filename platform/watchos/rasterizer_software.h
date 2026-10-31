/**************************************************************************/
/*  rasterizer_software.h                                                 */
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

#pragma once

#include "software_renderer_stats.h"
#include "texture_storage_software.h"

#include "core/string/string_name.h"
#include "core/templates/hash_map.h"
#include "servers/rendering/dummy/rasterizer_dummy.h"

// Renders 2D on the CPU, for platforms without a GPU API that Godot supports.
// 3D, materials and other features fall back to the dummy renderer.
class RasterizerSoftware : public RasterizerDummy {
	static inline RasterizerSoftware *singleton = nullptr;

	// What the platform presents, sized to the main window.
	SoftwareImage screen;
	// Frames are only presented when the screen changed, which saves the copy to the platform.
	bool screen_changed = true;

	// Accumulated during a frame, then smoothed into the values exposed as Performance monitors.
	RasterStats frame_stats;
	double raster_ms = 0.0;
	double track_ms = 0.0;
	double clear_ms = 0.0;
	double draw_ms = 0.0;
	double present_ms = 0.0;
	double redrawn_percent = 0.0;
	double items_drawn = 0.0;
	double category_ms[DRAW_CATEGORY_MAX] = {};

	static double _get_raster_ms();
	static double _get_track_ms();
	static double _get_clear_ms();
	static double _get_draw_ms();
	static double _get_present_ms();
	static double _get_redrawn_percent();
	static double _get_items_drawn();
	template <int C>
	static double _get_category_ms() { return singleton ? singleton->category_ms[C] : 0.0; }
	static HashMap<StringName, double (*)()> _get_monitors();

public:
	static RasterizerSoftware *get_singleton() { return singleton; }

	const SoftwareImage &get_screen() const { return screen; }

	// Called by the canvas renderer after drawing a frame into a render target.
	void report_raster(const RasterStats &p_stats);

	void set_boot_image_with_stretch(const Ref<Image> &p_image, const Color &p_color, RSE::SplashStretchMode p_stretch_mode, bool p_use_filter = true) override;

	void blit_render_targets_to_screen(DisplayServerEnums::WindowID p_screen, const RenderingServerTypes::BlitToScreen *p_render_targets, int p_amount) override;
	void end_frame(bool p_present) override;

	static RendererCompositor *_create_current() {
		return memnew(RasterizerSoftware);
	}

	static void make_current() {
		_create_func = _create_current;
		low_end = true;
	}

	RasterizerSoftware();
	~RasterizerSoftware();
};

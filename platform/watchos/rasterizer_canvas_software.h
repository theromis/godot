/**************************************************************************/
/*  rasterizer_canvas_software.h                                          */
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

#include "core/templates/hash_map.h"
#include "servers/rendering/dummy/rasterizer_canvas_dummy.h"

// Rasterizes canvas items on the CPU into a render target.
// Supports rects (including text), nine-patches, primitives, polygons, 2D meshes and multimeshes,
// with clipping, modulation, alpha blending and nearest/linear filtering.
// Materials, shaders, lights, GPU particles and canvas groups are not supported yet.
//
// Drawing is deferred until all canvases of a render target have been submitted for the frame.
// Each item is then compared with the previous frame, and only the areas that changed are
// cleared and redrawn. A frame where nothing changed doesn't touch the render target at all.
class RasterizerCanvasSoftware : public RasterizerCanvasDummy {
	// Geometry of a polygon or a mesh surface.
	struct Primitives {
		RSE::PrimitiveType primitive = RSE::PRIMITIVE_TRIANGLES; // Only used by mesh surfaces; polygon commands carry their own.
		Vector<int> indices;
		Vector<Point2> points;
		Vector<Color> colors;
		Vector<Point2> uvs;
		int count = 0; // Number of indices, or of points when there are no indices.
	};

	HashMap<PolygonID, Primitives> polygons;
	PolygonID polygon_id_counter = 1;

	// Decoded mesh surfaces. A surface is decoded again when its vertex data changes.
	struct MeshSurfaceCache {
		Vector<uint8_t> vertex_data;
		Primitives primitives;
	};
	HashMap<RID, LocalVector<MeshSurfaceCache>> mesh_cache;

	struct Target {
		SoftwareImage *image = nullptr;
		Rect2i clip;
		bool transparent = false;
	};

	struct Sampler {
		const SoftwareImage *image = nullptr;
		bool linear = false;
		bool repeat = false;
		bool use_clip_rect = false;
		Rect2 clip_rect; // In texels.
	};

	struct Vertex {
		Vector2 position;
		Vector2 uv;
		Color color;
	};

	// The state of an item when it was submitted, since culling for another viewport
	// can overwrite the item's final transform before the frame is drawn.
	struct DrawRecord {
		const Item *item = nullptr;
		Transform2D transform;
		Color modulate;
		Rect2i clip; // Item clip in render target pixels.
		Rect2i bounds; // Pixels the item can touch, already clipped.
		bool ignores_clip = false;
		bool linear = true;
		bool repeat = false;
		uint64_t hash = 0;
	};

	struct PendingFrame {
		RID render_target;
		LocalVector<DrawRecord> records;
		bool clear = false;
		Color clear_color;
	};
	PendingFrame pending;

	// Time spent per kind of command in the current flush, for profiling.
	uint64_t draw_category_usec[DRAW_CATEGORY_MAX] = {};

	// What was drawn into a render target in its last frame.
	struct RenderTargetState {
		struct Entry {
			const Item *item = nullptr;
			uint64_t hash = 0;
			Rect2i bounds;
		};
		LocalVector<Entry> entries;
		bool valid = false;
		bool clear = false;
		Color clear_color;
		Size2i size;
		bool transparent = false;
	};
	HashMap<RID, RenderTargetState> render_target_states;

	void _draw_triangle(const Target &p_target, const Vertex &p_v0, const Vertex &p_v1, const Vertex &p_v2, const Sampler &p_sampler);
	void _draw_line(const Target &p_target, const Vertex &p_from, const Vertex &p_to, const Sampler &p_sampler);
	void _draw_point(const Target &p_target, const Vertex &p_point, const Sampler &p_sampler);
	// Draws p_rect transformed by p_xform. p_uv_rect is in normalized texture coordinates.
	void _draw_rect(const Target &p_target, const Transform2D &p_xform, const Rect2 &p_rect, const Rect2 &p_uv_rect, bool p_transpose, const Color &p_modulate, const Sampler &p_sampler);
	void _draw_nine_patch(const Target &p_target, const Transform2D &p_xform, const Item::CommandNinePatch *p_command, const Color &p_modulate, Sampler p_sampler);
	void _draw_primitives(const Target &p_target, const Transform2D &p_xform, RSE::PrimitiveType p_primitive, const Primitives &p_primitives, const Color &p_modulate, const Sampler &p_sampler);
	void _draw_mesh(const Target &p_target, const Transform2D &p_xform, RID p_mesh, const Color &p_modulate, const Sampler &p_sampler);
	void _draw_item(SoftwareImage &p_image, bool p_transparent, const DrawRecord &p_record, const Rect2i &p_limit);

	uint64_t _hash_record(const DrawRecord &p_record) const;
	void _begin_frame(RID p_render_target);

public:
	static RasterizerCanvasSoftware *get_singleton() { return static_cast<RasterizerCanvasSoftware *>(singleton); }

	// Called by the texture storage when the viewport requests a clear.
	void request_clear(RID p_render_target, const Color &p_color);
	// Draws everything submitted for p_render_target (or for any render target if p_render_target is null).
	void flush(RID p_render_target = RID());
	void render_target_freed(RID p_render_target);

	PolygonID request_polygon(const Vector<int> &p_indices, const Vector<Point2> &p_points, const Vector<Color> &p_colors, const Vector<Point2> &p_uvs = Vector<Point2>(), const Vector<int> &p_bones = Vector<int>(), const Vector<float> &p_weights = Vector<float>(), int p_count = -1) override;
	void free_polygon(PolygonID p_polygon) override;

	void canvas_render_items(RID p_to_render_target, Item *p_item_list, const Color &p_modulate, Light *p_light_list, Light *p_directional_list, const Transform2D &p_canvas_transform, RSE::CanvasItemTextureFilter p_default_filter, RSE::CanvasItemTextureRepeat p_default_repeat, bool p_snap_2d_vertices_to_pixel, bool &r_sdf_used, RenderingServerTypes::RenderInfo *r_render_info = nullptr) override;
};

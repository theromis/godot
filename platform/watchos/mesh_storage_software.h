/**************************************************************************/
/*  mesh_storage_software.h                                               */
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

#include "core/templates/hash_map.h"
#include "servers/rendering/dummy/storage/mesh_storage.h"

// Mesh storage for the software renderer. The dummy storage keeps mesh surfaces but reports empty
// bounds and drops multimesh data, which makes the canvas treat 2D meshes as a single point and
// leaves multimeshes (used by CPUParticles2D and MultiMeshInstance2D) empty.
class MeshStorageSoftware : public RendererDummy::MeshStorage {
public:
	struct MultiMesh {
		int instances = 0;
		RSE::MultimeshTransformFormat transform_format = RSE::MULTIMESH_TRANSFORM_2D;
		bool use_colors = false;
		bool use_custom_data = false;
		int stride = 0; // Floats per instance.
		int visible_instances = -1;
		RID mesh;
		Vector<float> buffer;
		AABB custom_aabb;
		uint64_t version = 0; // Changes whenever the instances change.

		int get_color_offset() const { return transform_format == RSE::MULTIMESH_TRANSFORM_2D ? 8 : 12; }
		int get_visible_count() const { return visible_instances < 0 ? instances : MIN(visible_instances, instances); }
		Transform2D get_transform_2d(int p_index) const;
		Color get_color(int p_index) const;
	};

private:
	static inline MeshStorageSoftware *singleton = nullptr;

	HashMap<RID, AABB> custom_aabbs;
	HashMap<RID, MultiMesh> multimeshes;
	uint64_t version_counter = 0;

public:
	static MeshStorageSoftware *get_singleton() { return singleton; }

	const MultiMesh *get_multimesh(RID p_multimesh) const { return multimeshes.getptr(p_multimesh); }

	/* MESH API */

	void mesh_set_custom_aabb(RID p_mesh, const AABB &p_aabb) override;
	AABB mesh_get_custom_aabb(RID p_mesh) const override;
	AABB mesh_get_aabb(RID p_mesh, RID p_skeleton = RID()) override;
	void mesh_free(RID p_rid) override;

	/* MULTIMESH API */

	void _multimesh_free(RID p_rid) override;
	void _multimesh_allocate_data(RID p_multimesh, int p_instances, RSE::MultimeshTransformFormat p_transform_format, bool p_use_colors = false, bool p_use_custom_data = false, bool p_use_indirect = false) override;
	int _multimesh_get_instance_count(RID p_multimesh) const override;

	void _multimesh_set_mesh(RID p_multimesh, RID p_mesh) override;
	RID _multimesh_get_mesh(RID p_multimesh) const override;

	void _multimesh_instance_set_transform(RID p_multimesh, int p_index, const Transform3D &p_transform) override;
	void _multimesh_instance_set_transform_2d(RID p_multimesh, int p_index, const Transform2D &p_transform) override;
	void _multimesh_instance_set_color(RID p_multimesh, int p_index, const Color &p_color) override;
	void _multimesh_instance_set_custom_data(RID p_multimesh, int p_index, const Color &p_color) override;

	Transform3D _multimesh_instance_get_transform(RID p_multimesh, int p_index) const override;
	Transform2D _multimesh_instance_get_transform_2d(RID p_multimesh, int p_index) const override;
	Color _multimesh_instance_get_color(RID p_multimesh, int p_index) const override;
	Color _multimesh_instance_get_custom_data(RID p_multimesh, int p_index) const override;

	void _multimesh_set_buffer(RID p_multimesh, const Vector<float> &p_buffer) override;
	Vector<float> _multimesh_get_buffer(RID p_multimesh) const override;

	void _multimesh_set_visible_instances(RID p_multimesh, int p_visible) override;
	int _multimesh_get_visible_instances(RID p_multimesh) const override;

	void _multimesh_set_custom_aabb(RID p_multimesh, const AABB &p_aabb) override;
	AABB _multimesh_get_custom_aabb(RID p_multimesh) const override;
	AABB _multimesh_get_aabb(RID p_multimesh) override;

	MeshStorageSoftware();
	~MeshStorageSoftware();
};

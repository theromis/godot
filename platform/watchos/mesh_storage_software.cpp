/**************************************************************************/
/*  mesh_storage_software.cpp                                             */
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

#include "mesh_storage_software.h"

/* MultiMesh */

Transform2D MeshStorageSoftware::MultiMesh::get_transform_2d(int p_index) const {
	const float *data = buffer.ptr() + p_index * stride;
	if (transform_format == RSE::MULTIMESH_TRANSFORM_2D) {
		// Rows of the transform: x.x, y.x, padding, origin.x, then x.y, y.y, padding, origin.y.
		return Transform2D(data[0], data[4], data[1], data[5], data[3], data[7]);
	}
	// 3D transforms are rows of the basis followed by the origin; 2D drawing uses x and y only.
	return Transform2D(data[0], data[4], data[1], data[5], data[3], data[7]);
}

Color MeshStorageSoftware::MultiMesh::get_color(int p_index) const {
	if (!use_colors) {
		return Color(1, 1, 1, 1);
	}
	const float *data = buffer.ptr() + p_index * stride + get_color_offset();
	return Color(data[0], data[1], data[2], data[3]);
}

/* MESH API */

void MeshStorageSoftware::mesh_set_custom_aabb(RID p_mesh, const AABB &p_aabb) {
	if (p_aabb.size != Vector3()) {
		custom_aabbs[p_mesh] = p_aabb;
	} else {
		custom_aabbs.erase(p_mesh);
	}
}

AABB MeshStorageSoftware::mesh_get_custom_aabb(RID p_mesh) const {
	const AABB *aabb = custom_aabbs.getptr(p_mesh);
	return aabb ? *aabb : AABB();
}

AABB MeshStorageSoftware::mesh_get_aabb(RID p_mesh, RID p_skeleton) {
	const AABB *custom = custom_aabbs.getptr(p_mesh);
	if (custom) {
		return *custom;
	}
	AABB aabb;
	const int surface_count = mesh_get_surface_count(p_mesh);
	for (int i = 0; i < surface_count; i++) {
		const AABB surface_aabb = mesh_get_surface(p_mesh, i).aabb;
		aabb = i == 0 ? surface_aabb : aabb.merge(surface_aabb);
	}
	return aabb;
}

void MeshStorageSoftware::mesh_free(RID p_rid) {
	custom_aabbs.erase(p_rid);
	RendererDummy::MeshStorage::mesh_free(p_rid);
}

/* MULTIMESH API */

void MeshStorageSoftware::_multimesh_free(RID p_rid) {
	multimeshes.erase(p_rid);
	RendererDummy::MeshStorage::_multimesh_free(p_rid);
}

void MeshStorageSoftware::_multimesh_allocate_data(RID p_multimesh, int p_instances, RSE::MultimeshTransformFormat p_transform_format, bool p_use_colors, bool p_use_custom_data, bool p_use_indirect) {
	MultiMesh &multimesh = multimeshes[p_multimesh];
	multimesh.instances = MAX(p_instances, 0);
	multimesh.transform_format = p_transform_format;
	multimesh.use_colors = p_use_colors;
	multimesh.use_custom_data = p_use_custom_data;
	multimesh.stride = multimesh.get_color_offset() + (p_use_colors ? 4 : 0) + (p_use_custom_data ? 4 : 0);
	multimesh.buffer.resize(multimesh.instances * multimesh.stride);
	multimesh.buffer.fill(0.0f);
	multimesh.version = ++version_counter;
}

int MeshStorageSoftware::_multimesh_get_instance_count(RID p_multimesh) const {
	const MultiMesh *multimesh = multimeshes.getptr(p_multimesh);
	return multimesh ? multimesh->instances : 0;
}

void MeshStorageSoftware::_multimesh_set_mesh(RID p_multimesh, RID p_mesh) {
	MultiMesh &multimesh = multimeshes[p_multimesh];
	multimesh.mesh = p_mesh;
	multimesh.version = ++version_counter;
}

RID MeshStorageSoftware::_multimesh_get_mesh(RID p_multimesh) const {
	const MultiMesh *multimesh = multimeshes.getptr(p_multimesh);
	return multimesh ? multimesh->mesh : RID();
}

void MeshStorageSoftware::_multimesh_instance_set_transform(RID p_multimesh, int p_index, const Transform3D &p_transform) {
	MultiMesh *multimesh = multimeshes.getptr(p_multimesh);
	ERR_FAIL_NULL(multimesh);
	ERR_FAIL_INDEX(p_index, multimesh->instances);
	ERR_FAIL_COND(multimesh->transform_format != RSE::MULTIMESH_TRANSFORM_3D);
	float *data = multimesh->buffer.ptrw() + p_index * multimesh->stride;
	for (int row = 0; row < 3; row++) {
		data[row * 4 + 0] = p_transform.basis.rows[row][0];
		data[row * 4 + 1] = p_transform.basis.rows[row][1];
		data[row * 4 + 2] = p_transform.basis.rows[row][2];
		data[row * 4 + 3] = p_transform.origin[row];
	}
	multimesh->version = ++version_counter;
}

void MeshStorageSoftware::_multimesh_instance_set_transform_2d(RID p_multimesh, int p_index, const Transform2D &p_transform) {
	MultiMesh *multimesh = multimeshes.getptr(p_multimesh);
	ERR_FAIL_NULL(multimesh);
	ERR_FAIL_INDEX(p_index, multimesh->instances);
	ERR_FAIL_COND(multimesh->transform_format != RSE::MULTIMESH_TRANSFORM_2D);
	float *data = multimesh->buffer.ptrw() + p_index * multimesh->stride;
	data[0] = p_transform.columns[0][0];
	data[1] = p_transform.columns[1][0];
	data[2] = 0;
	data[3] = p_transform.columns[2][0];
	data[4] = p_transform.columns[0][1];
	data[5] = p_transform.columns[1][1];
	data[6] = 0;
	data[7] = p_transform.columns[2][1];
	multimesh->version = ++version_counter;
}

void MeshStorageSoftware::_multimesh_instance_set_color(RID p_multimesh, int p_index, const Color &p_color) {
	MultiMesh *multimesh = multimeshes.getptr(p_multimesh);
	ERR_FAIL_NULL(multimesh);
	ERR_FAIL_INDEX(p_index, multimesh->instances);
	ERR_FAIL_COND(!multimesh->use_colors);
	float *data = multimesh->buffer.ptrw() + p_index * multimesh->stride + multimesh->get_color_offset();
	data[0] = p_color.r;
	data[1] = p_color.g;
	data[2] = p_color.b;
	data[3] = p_color.a;
	multimesh->version = ++version_counter;
}

void MeshStorageSoftware::_multimesh_instance_set_custom_data(RID p_multimesh, int p_index, const Color &p_color) {
	MultiMesh *multimesh = multimeshes.getptr(p_multimesh);
	ERR_FAIL_NULL(multimesh);
	ERR_FAIL_INDEX(p_index, multimesh->instances);
	ERR_FAIL_COND(!multimesh->use_custom_data);
	float *data = multimesh->buffer.ptrw() + p_index * multimesh->stride + multimesh->get_color_offset() + (multimesh->use_colors ? 4 : 0);
	data[0] = p_color.r;
	data[1] = p_color.g;
	data[2] = p_color.b;
	data[3] = p_color.a;
	multimesh->version = ++version_counter;
}

Transform3D MeshStorageSoftware::_multimesh_instance_get_transform(RID p_multimesh, int p_index) const {
	const MultiMesh *multimesh = multimeshes.getptr(p_multimesh);
	ERR_FAIL_NULL_V(multimesh, Transform3D());
	ERR_FAIL_INDEX_V(p_index, multimesh->instances, Transform3D());
	ERR_FAIL_COND_V(multimesh->transform_format != RSE::MULTIMESH_TRANSFORM_3D, Transform3D());
	const float *data = multimesh->buffer.ptr() + p_index * multimesh->stride;
	Transform3D transform;
	for (int row = 0; row < 3; row++) {
		transform.basis.rows[row] = Vector3(data[row * 4 + 0], data[row * 4 + 1], data[row * 4 + 2]);
		transform.origin[row] = data[row * 4 + 3];
	}
	return transform;
}

Transform2D MeshStorageSoftware::_multimesh_instance_get_transform_2d(RID p_multimesh, int p_index) const {
	const MultiMesh *multimesh = multimeshes.getptr(p_multimesh);
	ERR_FAIL_NULL_V(multimesh, Transform2D());
	ERR_FAIL_INDEX_V(p_index, multimesh->instances, Transform2D());
	return multimesh->get_transform_2d(p_index);
}

Color MeshStorageSoftware::_multimesh_instance_get_color(RID p_multimesh, int p_index) const {
	const MultiMesh *multimesh = multimeshes.getptr(p_multimesh);
	ERR_FAIL_NULL_V(multimesh, Color());
	ERR_FAIL_INDEX_V(p_index, multimesh->instances, Color());
	return multimesh->get_color(p_index);
}

Color MeshStorageSoftware::_multimesh_instance_get_custom_data(RID p_multimesh, int p_index) const {
	const MultiMesh *multimesh = multimeshes.getptr(p_multimesh);
	ERR_FAIL_NULL_V(multimesh, Color());
	ERR_FAIL_INDEX_V(p_index, multimesh->instances, Color());
	if (!multimesh->use_custom_data) {
		return Color();
	}
	const float *data = multimesh->buffer.ptr() + p_index * multimesh->stride + multimesh->get_color_offset() + (multimesh->use_colors ? 4 : 0);
	return Color(data[0], data[1], data[2], data[3]);
}

void MeshStorageSoftware::_multimesh_set_buffer(RID p_multimesh, const Vector<float> &p_buffer) {
	MultiMesh *multimesh = multimeshes.getptr(p_multimesh);
	ERR_FAIL_NULL(multimesh);
	ERR_FAIL_COND(p_buffer.size() != multimesh->instances * multimesh->stride);
	multimesh->buffer = p_buffer;
	multimesh->version = ++version_counter;
}

Vector<float> MeshStorageSoftware::_multimesh_get_buffer(RID p_multimesh) const {
	const MultiMesh *multimesh = multimeshes.getptr(p_multimesh);
	ERR_FAIL_NULL_V(multimesh, Vector<float>());
	return multimesh->buffer;
}

void MeshStorageSoftware::_multimesh_set_visible_instances(RID p_multimesh, int p_visible) {
	MultiMesh &multimesh = multimeshes[p_multimesh];
	multimesh.visible_instances = p_visible;
	multimesh.version = ++version_counter;
}

int MeshStorageSoftware::_multimesh_get_visible_instances(RID p_multimesh) const {
	const MultiMesh *multimesh = multimeshes.getptr(p_multimesh);
	return multimesh ? multimesh->visible_instances : -1;
}

void MeshStorageSoftware::_multimesh_set_custom_aabb(RID p_multimesh, const AABB &p_aabb) {
	MultiMesh &multimesh = multimeshes[p_multimesh];
	multimesh.custom_aabb = p_aabb;
}

AABB MeshStorageSoftware::_multimesh_get_custom_aabb(RID p_multimesh) const {
	const MultiMesh *multimesh = multimeshes.getptr(p_multimesh);
	return multimesh ? multimesh->custom_aabb : AABB();
}

AABB MeshStorageSoftware::_multimesh_get_aabb(RID p_multimesh) {
	const MultiMesh *multimesh = multimeshes.getptr(p_multimesh);
	if (!multimesh) {
		return AABB();
	}
	if (multimesh->custom_aabb.size != Vector3()) {
		return multimesh->custom_aabb;
	}
	if (multimesh->mesh.is_null() || multimesh->get_visible_count() == 0) {
		return AABB();
	}
	// The canvas uses this as the item's bounds, so it covers every visible instance.
	const AABB mesh_aabb = mesh_get_aabb(multimesh->mesh);
	const Rect2 mesh_rect(mesh_aabb.position.x, mesh_aabb.position.y, mesh_aabb.size.x, mesh_aabb.size.y);
	Rect2 rect;
	for (int i = 0; i < multimesh->get_visible_count(); i++) {
		const Rect2 instance_rect = multimesh->get_transform_2d(i).xform(mesh_rect);
		rect = i == 0 ? instance_rect : rect.merge(instance_rect);
	}
	return AABB(Vector3(rect.position.x, rect.position.y, 0), Vector3(rect.size.x, rect.size.y, 0));
}

MeshStorageSoftware::MeshStorageSoftware() {
	singleton = this;
}

MeshStorageSoftware::~MeshStorageSoftware() {
	singleton = nullptr;
}

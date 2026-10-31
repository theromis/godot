/**************************************************************************/
/*  texture_storage_software.cpp                                          */
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

#include "texture_storage_software.h"

#include "rasterizer_canvas_software.h"

void SoftwareImage::set_from_image(const Ref<Image> &p_image) {
	if (p_image.is_null() || p_image->is_empty()) {
		resize(0, 0);
		return;
	}

	Ref<Image> image = p_image;
	if (image->is_compressed() || image->get_format() != Image::FORMAT_RGBA8 || image->has_mipmaps()) {
		image = p_image->duplicate();
		if (image->is_compressed()) {
			Error err = image->decompress();
			ERR_FAIL_COND_MSG(err != OK, vformat("Software renderer can't decompress image format %s.", Image::get_format_name(p_image->get_format())));
		}
		image->clear_mipmaps();
		image->convert(Image::FORMAT_RGBA8);
	}

	resize(image->get_width(), image->get_height());
	memcpy(pixels.ptr(), image->ptr(), pixels.size() * sizeof(uint32_t));
	invalidate_mipmaps();
}

const uint32_t *SoftwareImage::get_level(int p_level, int &r_width, int &r_height) const {
	if (p_level <= 0 || (width <= 1 && height <= 1)) {
		r_width = width;
		r_height = height;
		return pixels.ptr();
	}

	if (!mipmaps_valid) {
		// Build every level with a 2x2 box filter. Colors are weighted by alpha, so transparent
		// texels don't darken the edges of opaque areas.
		mip_levels.clear();
		int level_width = width;
		int level_height = height;
		uint32_t total = 0;
		while (level_width > 1 || level_height > 1) {
			level_width = MAX(level_width / 2, 1);
			level_height = MAX(level_height / 2, 1);
			MipLevel level;
			level.width = level_width;
			level.height = level_height;
			level.offset = total;
			mip_levels.push_back(level);
			total += level_width * level_height;
		}
		mip_pixels.resize(total);

		const uint32_t *source = pixels.ptr();
		int source_width = width;
		int source_height = height;
		for (const MipLevel &level : mip_levels) {
			uint32_t *destination = mip_pixels.ptr() + level.offset;
			for (int y = 0; y < level.height; y++) {
				const int sy0 = MIN(y * 2, source_height - 1);
				const int sy1 = MIN(y * 2 + 1, source_height - 1);
				for (int x = 0; x < level.width; x++) {
					const int sx0 = MIN(x * 2, source_width - 1);
					const int sx1 = MIN(x * 2 + 1, source_width - 1);
					const uint32_t texels[4] = { source[sy0 * source_width + sx0], source[sy0 * source_width + sx1], source[sy1 * source_width + sx0], source[sy1 * source_width + sx1] };
					uint32_t r = 0, g = 0, b = 0, a = 0;
					for (uint32_t texel : texels) {
						const uint32_t ta = texel >> 24;
						r += (texel & 0xFF) * ta;
						g += ((texel >> 8) & 0xFF) * ta;
						b += ((texel >> 16) & 0xFF) * ta;
						a += ta;
					}
					destination[y * level.width + x] = a == 0 ? 0 : (r / a) | ((g / a) << 8) | ((b / a) << 16) | (((a + 2) / 4) << 24);
				}
			}
			source = destination;
			source_width = level.width;
			source_height = level.height;
		}
		mipmaps_valid = true;
	}

	const MipLevel &level = mip_levels[MIN(p_level, (int)mip_levels.size()) - 1];
	r_width = level.width;
	r_height = level.height;
	return mip_pixels.ptr() + level.offset;
}

Ref<Image> SoftwareImage::to_image() const {
	if (is_empty()) {
		return Ref<Image>();
	}
	Vector<uint8_t> data;
	data.resize(pixels.size() * sizeof(uint32_t));
	memcpy(data.ptrw(), pixels.ptr(), data.size());
	return Image::create_from_data(width, height, false, Image::FORMAT_RGBA8, data);
}

/* Texture API */

void TextureStorageSoftware::_flush_render_target(RID p_render_target) const {
	if (RasterizerCanvasSoftware::get_singleton()) {
		RasterizerCanvasSoftware::get_singleton()->flush(p_render_target);
	}
}

const SoftwareImage *TextureStorageSoftware::get_texture_image(RID p_texture) const {
	Texture *texture = texture_owner.get_or_null(p_texture);
	// Proxies can point at other proxies; limit the depth to avoid cycles.
	for (int depth = 0; texture && texture->proxy_base.is_valid() && depth < 8; depth++) {
		texture = texture_owner.get_or_null(texture->proxy_base);
	}
	if (!texture) {
		return nullptr;
	}
	if (texture->render_target.is_valid()) {
		_flush_render_target(texture->render_target);
		RenderTarget *rt = render_target_owner.get_or_null(texture->render_target);
		return rt ? &rt->image : nullptr;
	}
	return &texture->image;
}

uint64_t TextureStorageSoftware::get_texture_version(RID p_texture) const {
	Texture *texture = texture_owner.get_or_null(p_texture);
	uint64_t version = 0;
	for (int depth = 0; texture && depth < 8; depth++) {
		// Include every proxy in the chain, so that retargeting a proxy counts as a change.
		version = version * 31 + texture->version;
		if (texture->proxy_base.is_null()) {
			break;
		}
		texture = texture_owner.get_or_null(texture->proxy_base);
	}
	if (texture && texture->render_target.is_valid()) {
		_flush_render_target(texture->render_target);
		RenderTarget *rt = render_target_owner.get_or_null(texture->render_target);
		version = version * 31 + (rt ? rt->version : 0);
	}
	return version;
}

void TextureStorageSoftware::render_target_mark_changed(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);
	rt->version = _next_version();
}

RID TextureStorageSoftware::texture_allocate() {
	return texture_owner.allocate_rid();
}

void TextureStorageSoftware::texture_free(RID p_rid) {
	Texture *texture = texture_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(texture);
	if (texture->render_target.is_valid()) {
		RenderTarget *rt = render_target_owner.get_or_null(texture->render_target);
		if (rt && rt->texture == p_rid) {
			rt->texture = RID();
		}
	}
	texture_owner.free(p_rid);
}

void TextureStorageSoftware::texture_2d_initialize(RID p_texture, const Ref<Image> &p_image) {
	Texture texture;
	if (p_image.is_valid()) {
		texture.format = p_image->get_format();
		texture.image.set_from_image(p_image);
	}
	texture.version = _next_version();
	texture_owner.initialize_rid(p_texture, texture);
}

void TextureStorageSoftware::texture_proxy_initialize(RID p_texture, RID p_base) {
	Texture texture;
	texture.proxy_base = p_base;
	texture.version = _next_version();
	texture_owner.initialize_rid(p_texture, texture);
}

void TextureStorageSoftware::texture_2d_update(RID p_texture, const Ref<Image> &p_image, int p_layer) {
	Texture *texture = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL(texture);
	ERR_FAIL_COND(p_image.is_null());
	texture->format = p_image->get_format();
	texture->image.set_from_image(p_image);
	texture->version = _next_version();
}

void TextureStorageSoftware::texture_proxy_update(RID p_proxy, RID p_base) {
	Texture *texture = texture_owner.get_or_null(p_proxy);
	ERR_FAIL_NULL(texture);
	texture->proxy_base = p_base;
	texture->version = _next_version();
}

void TextureStorageSoftware::texture_2d_placeholder_initialize(RID p_texture) {
	// Placeholders are drawn as white, like the other renderers' default texture.
	Texture texture;
	texture.image.resize(1, 1);
	texture.image.fill(0xFFFFFFFF);
	texture.version = _next_version();
	texture_owner.initialize_rid(p_texture, texture);
}

Ref<Image> TextureStorageSoftware::texture_2d_get(RID p_texture) const {
	const SoftwareImage *image = get_texture_image(p_texture);
	ERR_FAIL_NULL_V(image, Ref<Image>());
	return image->to_image();
}

void TextureStorageSoftware::texture_replace(RID p_texture, RID p_by_texture) {
	Texture *texture = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL(texture);
	Texture *by_texture = texture_owner.get_or_null(p_by_texture);
	ERR_FAIL_NULL(by_texture);

	// Keep the path, it belongs to the resource that owns p_texture.
	String path = texture->path;
	*texture = *by_texture;
	texture->path = path;
	texture->version = _next_version();
	texture_owner.free(p_by_texture);
}

void TextureStorageSoftware::texture_set_size_override(RID p_texture, int p_width, int p_height) {
	Texture *texture = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL(texture);
	texture->size_override = Size2i(p_width, p_height);
}

void TextureStorageSoftware::texture_set_path(RID p_texture, const String &p_path) {
	Texture *texture = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL(texture);
	texture->path = p_path;
}

String TextureStorageSoftware::texture_get_path(RID p_texture) const {
	Texture *texture = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(texture, String());
	return texture->path;
}

Image::Format TextureStorageSoftware::texture_get_format(RID p_texture) const {
	Texture *texture = texture_owner.get_or_null(p_texture);
	ERR_FAIL_NULL_V(texture, Image::FORMAT_MAX);
	return texture->format;
}

Size2 TextureStorageSoftware::texture_size_with_proxy(RID p_proxy) {
	Texture *texture = texture_owner.get_or_null(p_proxy);
	ERR_FAIL_NULL_V(texture, Size2());
	if (texture->size_override != Size2i()) {
		return texture->size_override;
	}
	const SoftwareImage *image = get_texture_image(p_proxy);
	return image ? Size2(image->width, image->height) : Size2();
}

/* Render Target API */

RID TextureStorageSoftware::render_target_create() {
	return render_target_owner.make_rid(RenderTarget());
}

void TextureStorageSoftware::render_target_free(RID p_rid) {
	RenderTarget *rt = render_target_owner.get_or_null(p_rid);
	ERR_FAIL_NULL(rt);
	if (RasterizerCanvasSoftware::get_singleton()) {
		RasterizerCanvasSoftware::get_singleton()->render_target_freed(p_rid);
	}
	if (rt->texture.is_valid()) {
		Texture *texture = texture_owner.get_or_null(rt->texture);
		if (texture) {
			// The texture is owned by a ViewportTexture; leave it valid but empty.
			texture->render_target = RID();
		}
	}
	render_target_owner.free(p_rid);
}

void TextureStorageSoftware::render_target_set_size(RID p_render_target, int p_width, int p_height, uint32_t p_view_count) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);
	if (rt->image.width != p_width || rt->image.height != p_height) {
		rt->image.resize(p_width, p_height);
		rt->image.fill(0);
		rt->version = _next_version();
	}
}

Size2i TextureStorageSoftware::render_target_get_size(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, Size2i());
	return Size2i(rt->image.width, rt->image.height);
}

void TextureStorageSoftware::render_target_set_transparent(RID p_render_target, bool p_is_transparent) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);
	rt->is_transparent = p_is_transparent;
}

bool TextureStorageSoftware::render_target_get_transparent(RID p_render_target) const {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, false);
	return rt->is_transparent;
}

void TextureStorageSoftware::render_target_request_clear(RID p_render_target, const Color &p_clear_color) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);
	rt->clear_requested = true;
	rt->clear_color = p_clear_color;
}

bool TextureStorageSoftware::render_target_is_clear_requested(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, false);
	return rt->clear_requested;
}

Color TextureStorageSoftware::render_target_get_clear_request_color(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, Color());
	return rt->clear_color;
}

void TextureStorageSoftware::render_target_disable_clear_request(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);
	rt->clear_requested = false;
}

void TextureStorageSoftware::render_target_do_clear_request(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL(rt);
	if (!rt->clear_requested) {
		return;
	}
	rt->clear_requested = false;
	// The canvas renderer clears only the areas it redraws.
	if (RasterizerCanvasSoftware::get_singleton()) {
		RasterizerCanvasSoftware::get_singleton()->request_clear(p_render_target, rt->clear_color);
	}
}

RID TextureStorageSoftware::render_target_get_texture(RID p_render_target) {
	RenderTarget *rt = render_target_owner.get_or_null(p_render_target);
	ERR_FAIL_NULL_V(rt, RID());
	if (rt->texture.is_null()) {
		Texture texture;
		texture.render_target = p_render_target;
		texture.version = _next_version();
		rt->texture = texture_owner.make_rid(texture);
	}
	return rt->texture;
}

TextureStorageSoftware::TextureStorageSoftware() {
	singleton = this;
}

TextureStorageSoftware::~TextureStorageSoftware() {
	singleton = nullptr;
}

/**************************************************************************/
/*  texture_storage_software.h                                            */
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

#include "core/templates/local_vector.h"
#include "servers/rendering/dummy/storage/texture_storage.h"

// CPU-side image used by the software renderer.
// Pixels are RGBA8, stored in memory order (R, G, B, A), not premultiplied.
struct SoftwareImage {
	struct MipLevel {
		int width = 0;
		int height = 0;
		uint32_t offset = 0;
	};

	int width = 0;
	int height = 0;
	LocalVector<uint32_t> pixels;

	// Smaller versions of the image, built on first use and dropped when the pixels change.
	// Level 0 is the image itself and isn't stored here.
	mutable LocalVector<uint32_t> mip_pixels;
	mutable LocalVector<MipLevel> mip_levels;
	mutable bool mipmaps_valid = false;

	void resize(int p_width, int p_height) {
		width = MAX(p_width, 0);
		height = MAX(p_height, 0);
		pixels.resize(width * height);
		invalidate_mipmaps();
	}

	bool is_empty() const { return width == 0 || height == 0; }

	void fill(uint32_t p_pixel) {
		for (uint32_t &pixel : pixels) {
			pixel = p_pixel;
		}
		invalidate_mipmaps();
	}

	// Must be called after writing to the pixels directly.
	void invalidate_mipmaps() {
		mipmaps_valid = false;
		mip_pixels.clear();
		mip_levels.clear();
	}

	// Returns the pixels of a mipmap level, clamped to the smallest level.
	const uint32_t *get_level(int p_level, int &r_width, int &r_height) const;

	void set_from_image(const Ref<Image> &p_image);
	Ref<Image> to_image() const;
};

// Texture and render target storage for the software renderer.
// Everything that isn't needed for 2D rendering is inherited from the dummy storage.
class TextureStorageSoftware : public RendererDummy::TextureStorage {
public:
	struct Texture {
		SoftwareImage image;
		Image::Format format = Image::FORMAT_RGBA8;
		Size2i size_override;
		String path;
		RID proxy_base;
		RID render_target; // Set when this texture shows the contents of a render target.
		uint64_t version = 0; // Changes whenever the contents change.
	};

	struct RenderTarget {
		SoftwareImage image;
		bool is_transparent = false;
		bool clear_requested = false;
		Color clear_color;
		RID texture;
		uint64_t version = 0; // Changes whenever something is drawn.
		bool damaged = false; // Whether the last drawn frame changed any pixels.
	};

private:
	static inline TextureStorageSoftware *singleton = nullptr;

	mutable RID_Owner<Texture, true> texture_owner;
	mutable RID_Owner<RenderTarget> render_target_owner;

	uint64_t version_counter = 0;
	uint64_t _next_version() { return ++version_counter; }

	// Draws anything still pending for the render target, so that its contents are up to date.
	void _flush_render_target(RID p_render_target) const;

public:
	static TextureStorageSoftware *get_singleton() { return singleton; }

	bool owns_software_texture(RID p_rid) const { return texture_owner.owns(p_rid); }

	// Returns the image to sample for a texture, following proxies and render targets.
	const SoftwareImage *get_texture_image(RID p_texture) const;

	RenderTarget *get_render_target(RID p_render_target) const { return render_target_owner.get_or_null(p_render_target); }

	// Returns a value that changes whenever the texture's contents change, or 0 for no texture.
	uint64_t get_texture_version(RID p_texture) const;
	void render_target_mark_changed(RID p_render_target);

	/* Texture API */

	virtual RID texture_allocate() override;
	virtual void texture_free(RID p_rid) override;

	virtual void texture_2d_initialize(RID p_texture, const Ref<Image> &p_image) override;
	virtual void texture_proxy_initialize(RID p_texture, RID p_base) override;
	virtual void texture_2d_update(RID p_texture, const Ref<Image> &p_image, int p_layer = 0) override;
	virtual void texture_proxy_update(RID p_proxy, RID p_base) override;

	virtual void texture_2d_placeholder_initialize(RID p_texture) override;

	virtual Ref<Image> texture_2d_get(RID p_texture) const override;

	virtual void texture_replace(RID p_texture, RID p_by_texture) override;
	virtual void texture_replace_compatible(RID p_texture, RID p_by_texture) override { texture_replace(p_texture, p_by_texture); }
	virtual void texture_set_size_override(RID p_texture, int p_width, int p_height) override;

	virtual void texture_set_path(RID p_texture, const String &p_path) override;
	virtual String texture_get_path(RID p_texture) const override;

	virtual Image::Format texture_get_format(RID p_texture) const override;

	virtual Size2 texture_size_with_proxy(RID p_proxy) override;

	/* Render Target API */

	virtual RID render_target_create() override;
	virtual void render_target_free(RID p_rid) override;
	virtual void render_target_set_size(RID p_render_target, int p_width, int p_height, uint32_t p_view_count) override;
	virtual Size2i render_target_get_size(RID p_render_target) const override;
	virtual void render_target_set_transparent(RID p_render_target, bool p_is_transparent) override;
	virtual bool render_target_get_transparent(RID p_render_target) const override;

	virtual void render_target_request_clear(RID p_render_target, const Color &p_clear_color) override;
	virtual bool render_target_is_clear_requested(RID p_render_target) override;
	virtual Color render_target_get_clear_request_color(RID p_render_target) override;
	virtual void render_target_disable_clear_request(RID p_render_target) override;
	virtual void render_target_do_clear_request(RID p_render_target) override;

	virtual RID render_target_get_texture(RID p_render_target) override;

	TextureStorageSoftware();
	~TextureStorageSoftware();
};

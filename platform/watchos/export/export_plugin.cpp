/**************************************************************************/
/*  export_plugin.cpp                                                     */
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

#include "export_plugin.h"

#include "logo_svg.gen.h"
#include "run_icon_svg.gen.h"

#include "editor/editor_node.h"
#include "editor/export/editor_export_platform_apple_embedded.h"
#include "editor/import/resource_importer_texture_settings.h"

Vector<String> EditorExportPlatformWatchOS::device_types({ "appleWatch" });

void EditorExportPlatformWatchOS::initialize() {
	if (EditorNode::get_singleton()) {
		EditorExportPlatformAppleEmbedded::_initialize(_watchos_logo_svg, _watchos_run_icon_svg);
#ifdef MACOS_ENABLED
		_start_remote_device_poller_thread();
#endif
	}
}

EditorExportPlatformWatchOS::~EditorExportPlatformWatchOS() {
#ifdef MACOS_ENABLED
	_stop_remote_device_poller_thread();
#endif
}

void EditorExportPlatformWatchOS::get_export_options(List<ExportOption> *r_options) const {
	EditorExportPlatformAppleEmbedded::get_export_options(r_options);

	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "application/min_watchos_version"), get_minimum_deployment_target()));
}

void EditorExportPlatformWatchOS::get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const {
	EditorExportPlatformAppleEmbedded::get_preset_features(p_preset, r_features);
	// Without ETC2/ASTC import there are no such texture variants to export.
	if (!ResourceImporterTextureSettings::should_import_etc2_astc()) {
		r_features->erase("etc2");
		r_features->erase("astc");
	}
}

bool EditorExportPlatformWatchOS::has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const {
	String err;
	bool valid = true;

	List<ExportOption> options;
	get_export_options(&options);
	for (const EditorExportPlatform::ExportOption &E : options) {
		if (get_export_option_visibility(p_preset.ptr(), E.option.name)) {
			String warn = get_export_option_warning(p_preset.ptr(), E.option.name);
			if (!warn.is_empty()) {
				err += warn + "\n";
				if (E.required) {
					valid = false;
				}
			}
		}
	}

	// Unlike the GPU renderers, the software renderer doesn't need compressed textures; lossless
	// and lossy textures (the default for 2D) work without ETC2/ASTC import.
	if (!ResourceImporterTextureSettings::should_import_etc2_astc()) {
		// Reported without failing validation, so it shows as a warning.
		err += TTR("ETC2/ASTC texture compression is disabled, so textures imported with VRAM compression won't load on watchOS. Use lossless or lossy compression for them, or enable 'Import ETC2 ASTC' in Rendering > Textures > VRAM Compression.") + "\n";
	}

	if (!err.is_empty()) {
		r_error = err;
	}
	return valid;
}

Vector<EditorExportPlatformAppleEmbedded::IconInfo> EditorExportPlatformWatchOS::get_icon_infos() const {
	// The single watchOS icon doesn't fit IconInfo; _export_icons emits it directly.
	return Vector<EditorExportPlatformAppleEmbedded::IconInfo>();
}

Error EditorExportPlatformWatchOS::_export_icons(const Ref<EditorExportPreset> &p_preset, const String &p_iconset_dir) {
	constexpr int ICON_SIDE = 1024;
	const String png_name = "Icon-1024.png";

	String icon_path = p_preset->get("icons/icon_1024x1024");
	if (icon_path.is_empty()) {
		icon_path = get_project_setting(p_preset, "application/config/icon");
	}

	Ref<Image> img;
	if (icon_path.is_empty()) {
		img = Image::create_empty(ICON_SIDE, ICON_SIDE, false, Image::FORMAT_RGB8);
		img->fill(get_project_setting(p_preset, "application/boot_splash/bg_color"));
	} else {
		Error err;
		img = _load_icon_or_splash_image(icon_path, &err);
		if (err != OK || img.is_null() || img->is_empty()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export Icons"), vformat("Invalid icon (%s): '%s'.", "icons/icon_1024x1024", icon_path));
			return ERR_UNCONFIGURED;
		}
		if (img->get_width() != ICON_SIDE || img->get_height() != ICON_SIDE) {
			const Image::Interpolation interpolation = (Image::Interpolation)(p_preset->get("application/icon_interpolation").operator int());
			img->resize(ICON_SIDE, ICON_SIDE, interpolation);
		}
		// App Store icons can't have an alpha channel.
		img->convert(Image::FORMAT_RGB8);
	}

	Error err = img->save_png(p_iconset_dir + png_name);
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export Icons"), vformat("Failed to export icon (%s): '%s'.", "icons/icon_1024x1024", icon_path));
		return err;
	}

	const String json_description = "{\"images\":[{\"filename\":\"" + png_name + "\",\"idiom\":\"universal\",\"platform\":\"watchos\",\"size\":\"1024x1024\"}],\"info\":{\"author\":\"xcode\",\"version\":1}}";
	Ref<FileAccess> f = FileAccess::open(p_iconset_dir + "Contents.json", FileAccess::WRITE);
	if (f.is_null()) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export Icons"), vformat(TTR("Could not write to a file at path \"%s\"."), p_iconset_dir + "Contents.json"));
		return ERR_CANT_CREATE;
	}
	CharString utf8 = json_description.utf8();
	f->store_buffer((const uint8_t *)utf8.get_data(), utf8.length());

	return OK;
}

String EditorExportPlatformWatchOS::_process_config_file_line(const Ref<EditorExportPreset> &p_preset, const String &p_line, const AppleEmbeddedConfigData &p_config, bool p_debug, const CodeSigningDetails &p_code_signing) {
	// Do watchOS specific processing first, and call super implementation if there are no matches

	String strnew;

	// Supported Destinations
	if (p_line.contains("$targeted_device_family")) {
		strnew += p_line.replace("$targeted_device_family", "4") + "\n";

		// MoltenVK Framework not used on watchOS
	} else if (p_line.contains("$moltenvk_buildfile")) {
		strnew += p_line.replace("$moltenvk_buildfile", "") + "\n";
	} else if (p_line.contains("$moltenvk_fileref")) {
		strnew += p_line.replace("$moltenvk_fileref", "") + "\n";
	} else if (p_line.contains("$moltenvk_buildphase")) {
		strnew += p_line.replace("$moltenvk_buildphase", "") + "\n";
	} else if (p_line.contains("$moltenvk_buildgrp")) {
		strnew += p_line.replace("$moltenvk_buildgrp", "") + "\n";

		// Launch Storyboard not used on watchOS
	} else if (p_line.contains("$plist_launch_screen_name")) {
		strnew += p_line.replace("$plist_launch_screen_name", "") + "\n";
	} else if (p_line.contains("$pbx_launch_screen_file_reference")) {
		strnew += p_line.replace("$pbx_launch_screen_file_reference", "") + "\n";
	} else if (p_line.contains("$pbx_launch_screen_copy_files")) {
		strnew += p_line.replace("$pbx_launch_screen_copy_files", "") + "\n";
	} else if (p_line.contains("$pbx_launch_screen_build_phase")) {
		strnew += p_line.replace("$pbx_launch_screen_build_phase", "") + "\n";
	} else if (p_line.contains("$pbx_launch_screen_build_reference")) {
		strnew += p_line.replace("$pbx_launch_screen_build_reference", "") + "\n";

		// OS Deployment Target
	} else if (p_line.contains("$os_deployment_target")) {
		String min_version = p_preset->get("application/min_" + get_platform_name() + "_version");
		String value = "WATCHOS_DEPLOYMENT_TARGET = " + min_version + ";";
		strnew += p_line.replace("$os_deployment_target", value) + "\n";

		// Valid Archs
	} else if (p_line.contains("$valid_archs")) {
		strnew += p_line.replace("$valid_archs", "arm64 arm64_32") + "\n";

		// Application Scene Manifest is visionOS-only
	} else if (p_line.contains("$application_scene_manifest_default_session_role")) {
		strnew += p_line.replace("$application_scene_manifest_default_session_role", "") + "\n";
	} else if (p_line.contains("$application_scene_manifest_immersive_configuration")) {
		strnew += p_line.replace("$application_scene_manifest_immersive_configuration", "") + "\n";

		// Info.plist tracking usage descriptions are visionOS-only
	} else if (p_line.contains("$hand_tracking_usage_description")) {
		strnew += p_line.replace("$hand_tracking_usage_description", "") + "\n";
	} else if (p_line.contains("$accessory_tracking_usage_description")) {
		strnew += p_line.replace("$accessory_tracking_usage_description", "") + "\n";

		// Info.plist additional content, prefixed with the keys that make this a standalone watchOS app
	} else if (p_line.contains("$additional_plist_content")) {
		strnew += "\t<key>WKApplication</key>\n\t<true/>\n\t<key>WKWatchOnly</key>\n\t<true/>\n";
		strnew += EditorExportPlatformAppleEmbedded::_process_config_file_line(p_preset, p_line, p_config, p_debug, p_code_signing);

		// Apple Embedded common
	} else {
		strnew += EditorExportPlatformAppleEmbedded::_process_config_file_line(p_preset, p_line, p_config, p_debug, p_code_signing);
	}
	return strnew;
}

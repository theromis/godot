/**************************************************************************/
/*  export_plugin.h                                                       */
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

#include "editor/export/editor_export_platform_apple_embedded.h"

class EditorExportPlatformWatchOS : public EditorExportPlatformAppleEmbedded {
	GDCLASS(EditorExportPlatformWatchOS, EditorExportPlatformAppleEmbedded);

	static Vector<String> device_types;

	virtual String get_platform_name() const override { return "watchos"; }
	virtual String get_sdk_name() const override { return "watchos"; }
	virtual const Vector<String> get_device_types() const override { return device_types; }

	virtual String get_minimum_deployment_target() const override { return "10.0"; }

	// Older Apple Watch models only run arm64_32 (64-bit instructions with 32-bit pointers).
	virtual Vector<ExportArchitecture> _get_supported_architectures() const override {
		Vector<ExportArchitecture> archs;
		archs.push_back(ExportArchitecture("arm64", true));
		archs.push_back(ExportArchitecture("arm64_32", true));
		return archs;
	}

	virtual Vector<EditorExportPlatformAppleEmbedded::IconInfo> get_icon_infos() const override;

	// watchOS uses a single 1024x1024 icon, which doesn't fit IconInfo.
	virtual Error _export_icons(const Ref<EditorExportPreset> &p_preset, const String &p_iconset_dir) override;

	virtual void get_export_options(List<ExportOption> *r_options) const override;

	// The software renderer decompresses textures, so ETC2/ASTC import is optional on watchOS.
	virtual void get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const override;
	virtual bool has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const override;

	virtual String _process_config_file_line(const Ref<EditorExportPreset> &p_preset, const String &p_line, const AppleEmbeddedConfigData &p_config, bool p_debug, const CodeSigningDetails &p_code_signing) override;

public:
	virtual String get_name() const override { return "watchOS"; }
	virtual String get_os_name() const override { return "watchOS"; }

	virtual void get_platform_features(List<String> *r_features) const override {
		EditorExportPlatformAppleEmbedded::get_platform_features(r_features);
		r_features->push_back("watchos");
	}

	virtual void initialize() override;
	~EditorExportPlatformWatchOS();
};

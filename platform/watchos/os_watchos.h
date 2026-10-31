/**************************************************************************/
/*  os_watchos.h                                                          */
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

#ifdef WATCHOS_ENABLED

#include "audio_driver_watchos.h"

#include "drivers/unix/os_unix.h"

class OS_WatchOS : public OS_Unix {
private:
	MainLoop *main_loop = nullptr;

	AudioDriverWatchOS audio_driver;

	bool is_focused = false;

	mutable String remote_fs_dir;

	virtual void initialize_core() override;
	virtual void initialize() override;

	virtual void initialize_joypads() override {}

	virtual void set_main_loop(MainLoop *p_main_loop) override;
	virtual MainLoop *get_main_loop() const override;

	virtual void delete_main_loop() override;

	virtual void finalize() override;

public:
	static OS_WatchOS *get_singleton();

	OS_WatchOS();
	~OS_WatchOS();

	bool iterate();

	void start();

	virtual String get_name() const override;
	virtual String get_distribution_name() const override;
	virtual String get_version() const override;

	virtual Error get_entropy(uint8_t *r_buffer, int p_bytes) override;

	virtual String get_user_data_dir(const String &p_user_dir) const override;

	virtual String get_cache_path() const override;
	virtual String get_temp_path() const override;
	virtual String get_resource_dir() const override;
	virtual String get_bundle_resource_dir() const override;

	virtual String get_locale() const override;

	virtual String get_unique_id() const override;
	virtual String get_processor_name() const override;

	// watchOS only plays predefined haptic patterns, so the duration and amplitude pick one of them.
	virtual void vibrate_handheld(int p_duration_ms = 500, float p_amplitude = -1.0) override;

	virtual bool _check_internal_feature_support(const String &p_feature) override;

	virtual Error setup_remote_filesystem(const String &p_server_host, int p_port, const String &p_password, String &r_project_path) override;

	void on_focus_out();
	void on_focus_in();

	void on_enter_background();
	void on_exit_background();
};

#endif // WATCHOS_ENABLED

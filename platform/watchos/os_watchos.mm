/**************************************************************************/
/*  os_watchos.mm                                                         */
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

#import "os_watchos.h"

#import "display_server_watchos.h"

#ifdef WATCHOS_ENABLED

#include "core/config/project_settings.h"
#include "core/io/logger.h"
#include "core/os/main_loop.h"
#include "core/profiling/profiling.h"
#import "drivers/apple/os_log_logger.h"
#include "main/main.h"
#include "servers/display/display_server.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>
#import <WatchKit/WatchKit.h>

OS_WatchOS *OS_WatchOS::get_singleton() {
	return (OS_WatchOS *)OS::get_singleton();
}

OS_WatchOS::OS_WatchOS() {
	Vector<Logger *> loggers;
	loggers.push_back(memnew(OsLogLogger(NSBundle.mainBundle.bundleIdentifier.UTF8String)));
	_set_logger(memnew(CompositeLogger(loggers)));

	DisplayServerWatchOS::register_watchos_driver();
	AudioDriverManager::add_driver(&audio_driver);
}

OS_WatchOS::~OS_WatchOS() {}

void OS_WatchOS::initialize_core() {
	OS_Unix::initialize_core();
}

void OS_WatchOS::initialize() {
	initialize_core();
}

void OS_WatchOS::set_main_loop(MainLoop *p_main_loop) {
	main_loop = p_main_loop;
}

MainLoop *OS_WatchOS::get_main_loop() const {
	return main_loop;
}

void OS_WatchOS::delete_main_loop() {
	if (main_loop) {
		main_loop->finalize();
		memdelete(main_loop);
	}

	main_loop = nullptr;
}

bool OS_WatchOS::iterate() {
	if (!main_loop) {
		return true;
	}

	GodotProfileFrameMark;
	GodotProfileZone("OS_WatchOS::iterate");

	if (DisplayServer::get_singleton()) {
		DisplayServer::get_singleton()->process_events();
	}

	return Main::iteration();
}

void OS_WatchOS::start() {
	if (Main::start() == EXIT_SUCCESS) {
		main_loop->initialize();
	}
}

void OS_WatchOS::finalize() {
}

String OS_WatchOS::get_name() const {
	return "watchOS";
}

String OS_WatchOS::get_distribution_name() const {
	return get_name();
}

String OS_WatchOS::get_version() const {
	NSOperatingSystemVersion ver = [NSProcessInfo processInfo].operatingSystemVersion;
	return vformat("%d.%d.%d", (int64_t)ver.majorVersion, (int64_t)ver.minorVersion, (int64_t)ver.patchVersion);
}

Error OS_WatchOS::get_entropy(uint8_t *r_buffer, int p_bytes) {
	int status = SecRandomCopyBytes(kSecRandomDefault, p_bytes, r_buffer);
	return status == errSecSuccess ? OK : FAILED;
}

String OS_WatchOS::get_user_data_dir(const String &p_user_dir) const {
	static String ret;
	if (ret.is_empty()) {
		NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
		if (paths && [paths count] >= 1) {
			ret.append_utf8([[paths firstObject] UTF8String]);
		}
	}
	return ret;
}

String OS_WatchOS::get_cache_path() const {
	static String ret;
	if (ret.is_empty()) {
		NSArray *paths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
		if (paths && [paths count] >= 1) {
			ret.append_utf8([[paths firstObject] UTF8String]);
		}
	}
	return ret;
}

String OS_WatchOS::get_temp_path() const {
	static String ret;
	if (ret.is_empty()) {
		NSURL *url = [NSURL fileURLWithPath:NSTemporaryDirectory()
								isDirectory:YES];
		if (url) {
			ret = String::utf8([url.path UTF8String]);
			ret = ret.trim_prefix("file://");
		}
	}
	return ret;
}

String OS_WatchOS::get_resource_dir() const {
#ifdef TOOLS_ENABLED
	return OS_Unix::get_resource_dir();
#else
	if (remote_fs_dir.is_empty()) {
		return OS_Unix::get_resource_dir();
	} else {
		return remote_fs_dir;
	}
#endif
}

String OS_WatchOS::get_bundle_resource_dir() const {
	NSString *str = [[[NSBundle mainBundle] infoDictionary] objectForKey:@"godot_path"];
	if (!str) {
		return OS_Unix::get_bundle_resource_dir();
	} else {
		String res_path = String::utf8([str cStringUsingEncoding:NSUTF8StringEncoding]);
		if (res_path.is_relative_path()) {
			res_path = String::utf8([[[NSBundle mainBundle] bundlePath] cStringUsingEncoding:NSUTF8StringEncoding]).path_join(res_path);
		}
		return res_path;
	}
}

String OS_WatchOS::get_locale() const {
	NSString *preferredLanguage = [NSLocale preferredLanguages].firstObject;

	if (preferredLanguage) {
		return String::utf8([preferredLanguage UTF8String]).replace_char('-', '_');
	}

	NSString *localeIdentifier = [[NSLocale currentLocale] localeIdentifier];
	return String::utf8([localeIdentifier UTF8String]).replace_char('-', '_');
}

String OS_WatchOS::get_unique_id() const {
	NSString *uuid = [WKInterfaceDevice currentDevice].identifierForVendor.UUIDString;
	return String::utf8([uuid UTF8String]);
}

String OS_WatchOS::get_processor_name() const {
#if defined(WATCHOS_SIMULATOR)
	return "Simulator";
#else
	return OS::get_processor_name();
#endif
}

void OS_WatchOS::vibrate_handheld(int p_duration_ms, float p_amplitude) {
	const bool strong = p_duration_ms > 100 || p_amplitude > 0.75;
	[[WKInterfaceDevice currentDevice] playHaptic:strong ? WKHapticTypeNotification : WKHapticTypeClick];
}

bool OS_WatchOS::_check_internal_feature_support(const String &p_feature) {
	if (p_feature == "mobile") {
		return true;
	}

	return false;
}

Error OS_WatchOS::setup_remote_filesystem(const String &p_server_host, int p_port, const String &p_password, String &r_project_path) {
	r_project_path = OS::get_user_data_dir();
	Error err = OS_Unix::setup_remote_filesystem(p_server_host, p_port, p_password, r_project_path);
	if (err == OK) {
		remote_fs_dir = r_project_path;
	}
	return err;
}

void OS_WatchOS::on_focus_out() {
	if (is_focused) {
		is_focused = false;

		if (get_main_loop()) {
			get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_FOCUS_OUT);
		}
	}
}

void OS_WatchOS::on_focus_in() {
	// The app can be launched in the background, where audio can't start yet.
	audio_driver.resume();
	if (!is_focused) {
		is_focused = true;

		if (get_main_loop()) {
			get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_FOCUS_IN);
		}
	}
}

void OS_WatchOS::on_enter_background() {
	if (get_main_loop()) {
		get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_PAUSED);
	}
	audio_driver.suspend();

	on_focus_out();
}

void OS_WatchOS::on_exit_background() {
	audio_driver.resume();
	if (!is_focused) {
		on_focus_in();

		if (get_main_loop()) {
			get_main_loop()->notification(MainLoop::NOTIFICATION_APPLICATION_RESUMED);
		}
	}
}

#endif // WATCHOS_ENABLED

/**************************************************************************/
/*  main_watchos.mm                                                       */
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

#import "godot_engine_watchos.h"

#import "display_server_watchos.h"
#import "os_watchos.h"

#include "core/profiling/profiling.h"
#include "main/main.h"

#include <unistd.h>

static OS_WatchOS *os = nullptr;
static bool engine_started = false;
static void (^frame_handler)(CGImageRef) = nil;

static void change_to_launch_dir(const char *p_executable_path) {
	const String dir = String::utf8(p_executable_path).get_base_dir();
	if (!dir.is_empty()) {
		chdir(dir.utf8().get_data());
	}
}

@implementation GDTEngineWatchOS

+ (BOOL)setUp {
	if (os) {
		return engine_started;
	}

	NSMutableArray<NSString *> *arguments = [[[NSProcessInfo processInfo] arguments] mutableCopy];
	NSArray *bundle_arguments = [[[NSBundle mainBundle] infoDictionary] objectForKey:@"godot_cmdline"];
	if (bundle_arguments) {
		[arguments addObjectsFromArray:bundle_arguments];
	}
	// The software renderer is the only one available on watchOS.
	[arguments addObjectsFromArray:@[ @"--rendering-driver", @"software" ]];

	Vector<CharString> utf8_arguments;
	for (NSString *argument in arguments) {
		utf8_arguments.push_back(String::utf8(argument.UTF8String).utf8());
	}
	Vector<char *> argv;
	for (CharString &argument : utf8_arguments) {
		argv.push_back(argument.ptrw());
	}
	argv.push_back(nullptr);
	const int argc = utf8_arguments.size();

	change_to_launch_dir(argv[0]);

	os = new OS_WatchOS();

	godot_init_profiler();

	Error err = Main::setup(argv[0], argc - 1, &argv.ptrw()[1]);
	if (err != OK) {
		return NO;
	}

	os->start();
	engine_started = true;
	// The app is in the foreground when the engine starts, but the initial scene phase
	// change arrives before the main loop exists, so focus has to be sent here.
	os->on_focus_in();
	return YES;
}

+ (BOOL)iterate {
	if (!engine_started) {
		return YES;
	}
	return os->iterate();
}

+ (void)tearDown {
	if (!os) {
		return;
	}
	engine_started = false;
	Main::cleanup();
	godot_cleanup_profiler();
	delete os;
	os = nullptr;
}

+ (void)focusIn {
	if (engine_started) {
		os->on_focus_in();
	}
}

+ (void)focusOut {
	if (engine_started) {
		os->on_focus_out();
	}
}

+ (void)enterBackground {
	if (engine_started) {
		os->on_enter_background();
	}
}

+ (void (^)(CGImageRef))frameHandler {
	return frame_handler;
}

+ (void)setFrameHandler:(void (^)(CGImageRef))handler {
	frame_handler = [handler copy];
}

+ (void)presentFrame:(CGImageRef)frame {
	if (frame_handler) {
		frame_handler(frame);
	}
}

+ (void)touchWithIndex:(NSInteger)index x:(double)x y:(double)y pressed:(BOOL)pressed {
	if (engine_started && DisplayServerWatchOS::get_singleton()) {
		DisplayServerWatchOS::get_singleton()->touch_press(index, Vector2(x, y), pressed);
	}
}

+ (void)dragWithIndex:(NSInteger)index x:(double)x y:(double)y {
	if (engine_started && DisplayServerWatchOS::get_singleton()) {
		DisplayServerWatchOS::get_singleton()->touch_drag(index, Vector2(x, y));
	}
}

+ (void)crownRotatedBy:(double)delta {
	if (engine_started && DisplayServerWatchOS::get_singleton()) {
		DisplayServerWatchOS::get_singleton()->crown_rotated(delta);
	}
}

+ (void)exitBackground {
	if (engine_started) {
		os->on_exit_background();
	}
}

@end

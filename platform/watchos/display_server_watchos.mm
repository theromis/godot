/**************************************************************************/
/*  display_server_watchos.mm                                             */
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

#import "display_server_watchos.h"

#ifdef WATCHOS_ENABLED

#import "godot_engine_watchos.h"
#import "rasterizer_software.h"

#include "core/input/input.h"
#include "core/input/input_event.h"

#import <CoreGraphics/CoreGraphics.h>
#import <WatchKit/WatchKit.h>

Vector<String> DisplayServerWatchOS::get_rendering_drivers_func() {
	Vector<String> drivers;
	drivers.push_back("software");
	return drivers;
}

DisplayServer *DisplayServerWatchOS::create_func(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error) {
	r_error = OK;
	RasterizerSoftware::make_current();
	return memnew(DisplayServerWatchOS());
}

DisplayServerWatchOS *DisplayServerWatchOS::get_singleton() {
	return static_cast<DisplayServerWatchOS *>(DisplayServer::get_singleton());
}

void DisplayServerWatchOS::register_watchos_driver() {
	register_create_function("watchOS", create_func, get_rendering_drivers_func);
}

bool DisplayServerWatchOS::has_feature(DisplayServerEnums::Feature p_feature) const {
	switch (p_feature) {
		case DisplayServerEnums::FEATURE_TOUCHSCREEN:
		case DisplayServerEnums::FEATURE_HIDPI:
			return true;
		default:
			return false;
	}
}

Vector<DisplayServerEnums::WindowID> DisplayServerWatchOS::get_window_list() const {
	Vector<DisplayServerEnums::WindowID> list;
	list.push_back(DisplayServerEnums::MAIN_WINDOW_ID);
	return list;
}

void DisplayServerWatchOS::touch_press(int p_index, const Vector2 &p_position, bool p_pressed) {
	ERR_FAIL_INDEX(p_index, TOUCH_COUNT);
	const Vector2 position = p_position * Vector2(screen_size);
	touches[p_index].pressed = p_pressed;
	touches[p_index].position = position;

	Ref<InputEventScreenTouch> event;
	event.instantiate();
	event->set_index(p_index);
	event->set_pressed(p_pressed);
	event->set_position(position);
	Input::get_singleton()->parse_input_event(event);
}

void DisplayServerWatchOS::touch_drag(int p_index, const Vector2 &p_position) {
	ERR_FAIL_INDEX(p_index, TOUCH_COUNT);
	const Vector2 position = p_position * Vector2(screen_size);
	const Vector2 relative = position - touches[p_index].position;
	touches[p_index].position = position;

	Ref<InputEventScreenDrag> event;
	event.instantiate();
	event->set_index(p_index);
	event->set_position(position);
	event->set_relative(relative);
	event->set_relative_screen_position(relative);
	Input::get_singleton()->parse_input_event(event);
}

void DisplayServerWatchOS::crown_rotated(double p_delta) {
	// One wheel step per CROWN_STEP of rotation; with medium sensitivity a full turn gives several steps.
	constexpr double CROWN_STEP = 0.1;
	crown_accumulated += p_delta;
	const int steps = (int)(crown_accumulated / CROWN_STEP);
	if (steps == 0) {
		return;
	}
	crown_accumulated -= steps * CROWN_STEP;

	// Turning the crown up increases the value and scrolls the content up, like system lists.
	const MouseButton button = steps > 0 ? MouseButton::WHEEL_UP : MouseButton::WHEEL_DOWN;
	const Vector2 position = touches[0].pressed ? touches[0].position : Vector2(screen_size) / 2;

	Ref<InputEventMouseButton> event;
	event.instantiate();
	event->set_button_index(button);
	event->set_factor(Math::abs(steps));
	event->set_position(position);
	event->set_global_position(position);
	event->set_pressed(true);
	Input::get_singleton()->parse_input_event(event);

	event = event->duplicate();
	event->set_pressed(false);
	Input::get_singleton()->parse_input_event(event);
}

void DisplayServerWatchOS::swap_buffers() {
	const RasterizerSoftware *rasterizer = RasterizerSoftware::get_singleton();
	if (!rasterizer) {
		return;
	}
	const SoftwareImage &screen = rasterizer->get_screen();
	if (screen.is_empty()) {
		return;
	}

	CFDataRef data = CFDataCreate(nullptr, (const UInt8 *)screen.pixels.ptr(), screen.pixels.size() * sizeof(uint32_t));
	CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
	CGColorSpaceRef color_space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
	CGImageRef image = CGImageCreate(screen.width, screen.height, 8, 32, screen.width * sizeof(uint32_t), color_space, kCGBitmapByteOrderDefault | kCGImageAlphaNoneSkipLast, provider, nullptr, false, kCGRenderingIntentDefault);

	[GDTEngineWatchOS presentFrame:image];

	CGImageRelease(image);
	CGColorSpaceRelease(color_space);
	CGDataProviderRelease(provider);
	CFRelease(data);
}

DisplayServerWatchOS::DisplayServerWatchOS() {
	WKInterfaceDevice *device = [WKInterfaceDevice currentDevice];
	screen_scale = device.screenScale;
	screen_size = Size2i(device.screenBounds.size.width * screen_scale, device.screenBounds.size.height * screen_scale);
}

#endif // WATCHOS_ENABLED

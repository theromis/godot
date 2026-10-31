/**************************************************************************/
/*  display_server_watchos.h                                              */
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

#include "servers/display/display_server_headless.h"

// Single full-screen window drawn by the software renderer and presented through SwiftUI.
// Everything without a watchOS equivalent is inherited from the headless display server.
class DisplayServerWatchOS : public DisplayServerHeadless {
	GDSOFTCLASS(DisplayServerWatchOS, DisplayServerHeadless);

	Size2i screen_size;
	float screen_scale = 1.0;

	struct Touch {
		bool pressed = false;
		Vector2 position;
	};
	static constexpr int TOUCH_COUNT = 2;
	Touch touches[TOUCH_COUNT];

	// Crown rotation not yet sent as a wheel event.
	double crown_accumulated = 0.0;

	static Vector<String> get_rendering_drivers_func();
	static DisplayServer *create_func(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error);

public:
	static DisplayServerWatchOS *get_singleton();
	static void register_watchos_driver();

	// Positions are normalized to the screen (0..1).
	void touch_press(int p_index, const Vector2 &p_position, bool p_pressed);
	void touch_drag(int p_index, const Vector2 &p_position);
	// Sent as mouse wheel events, so ScrollContainer and other controls react to the crown.
	void crown_rotated(double p_delta);

	bool has_feature(DisplayServerEnums::Feature p_feature) const override;
	String get_name() const override { return "watchOS"; }

	int get_screen_count() const override { return 1; }
	Size2i screen_get_size(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override { return screen_size; }
	Rect2i screen_get_usable_rect(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override { return Rect2i(Point2i(), screen_size); }
	int screen_get_dpi(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override { return 326; }
	float screen_get_scale(int p_screen = DisplayServerEnums::SCREEN_OF_MAIN_WINDOW) const override { return screen_scale; }
	float screen_get_max_scale() const override { return screen_scale; }

	Vector<DisplayServerEnums::WindowID> get_window_list() const override;
	int window_get_current_screen(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return 0; }
	Size2i window_get_size(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return screen_size; }
	Size2i window_get_size_with_decorations(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return screen_size; }
	DisplayServerEnums::WindowMode window_get_mode(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return DisplayServerEnums::WINDOW_MODE_FULLSCREEN; }
	bool window_can_draw(DisplayServerEnums::WindowID p_window = DisplayServerEnums::MAIN_WINDOW_ID) const override { return true; }
	bool can_any_window_draw() const override { return true; }

	void swap_buffers() override;

	DisplayServerWatchOS();
};

#endif // WATCHOS_ENABLED

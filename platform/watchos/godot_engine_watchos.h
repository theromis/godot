/**************************************************************************/
/*  godot_engine_watchos.h                                                */
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

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// Objective-C entry points into the engine, used by the SwiftUI app.
// This header is part of the Swift bridging header, so it must not include C++ headers.
@interface GDTEngineWatchOS : NSObject

// Receives every frame the engine presents, on the main thread.
@property(class, nonatomic, copy, nullable) void (^frameHandler)(CGImageRef frame);

// Called by the display server to hand a frame to `frameHandler`.
+ (void)presentFrame:(CGImageRef)frame;

// Touch positions are normalized to the screen (0..1).
+ (void)touchWithIndex:(NSInteger)index x:(double)x y:(double)y pressed:(BOOL)pressed;
+ (void)dragWithIndex:(NSInteger)index x:(double)x y:(double)y;

// Digital Crown rotation since the last call, in SwiftUI's crown units.
+ (void)crownRotatedBy:(double)delta;

// Runs `Main::setup()` and `Main::start()`. Returns `NO` if the engine failed to start
// or exited during setup (e.g. `--help`).
+ (BOOL)setUp;

// Runs a single main loop iteration. Returns `YES` when the engine requested to quit.
+ (BOOL)iterate;

+ (void)tearDown;

+ (void)focusIn;
+ (void)focusOut;

+ (void)enterBackground;
+ (void)exitBackground;

@end

NS_ASSUME_NONNULL_END

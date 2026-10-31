/**************************************************************************/
/*  app_watchos.swift                                                     */
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

import OSLog
import SwiftUI

// MARK: Helpers

extension os.Logger {
	static let godot = Logger(subsystem: "com.GodotFoundation.Godot", category: "SwiftUI")
}

// MARK: Engine Loop

/// Drives the engine main loop from the main run loop.
/// TODO: Replace the timer with a display-synchronized loop.
@MainActor
@Observable
final class EngineLoop {
	static let shared = EngineLoop()

	private(set) var isRunning = false
	/// The last frame presented by the engine.
	private(set) var frame: CGImage?
	private var timer: Timer?

	private init() {}

	func start() {
		guard timer == nil else { return }
		GDTEngineWatchOS.frameHandler = { frame in
			MainActor.assumeIsolated {
				EngineLoop.shared.frame = frame
			}
		}
		isRunning = GDTEngineWatchOS.setUp()
		Logger.godot.log("Engine set up (running: \(self.isRunning))")
		guard isRunning else { return }

		timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 60.0, repeats: true) { _ in
			MainActor.assumeIsolated {
				EngineLoop.shared.step()
			}
		}
	}

	private func step() {
		if GDTEngineWatchOS.iterate() {
			stop()
		}
	}

	func stop() {
		timer?.invalidate()
		timer = nil
		isRunning = false
		GDTEngineWatchOS.tearDown()
	}
}

// MARK: Content

struct GodotContentView: View {
	let engine: EngineLoop = .shared
	@State private var isTouching = false
	@State private var crownValue = 0.0
	@FocusState private var isFocused: Bool

	var body: some View {
		GeometryReader { geometry in
			ZStack {
				Color.black
				if let frame = engine.frame {
					Image(decorative: frame, scale: 1)
						.resizable()
						.interpolation(.none)
				} else if !engine.isRunning {
					Text("Godot stopped")
				}
			}
			.contentShape(Rectangle())
			.gesture(touchGesture(size: geometry.size))
		}
		.ignoresSafeArea()
		// The view needs focus to receive Digital Crown rotation.
		.focusable()
		.focused($isFocused)
		.focusEffectDisabled()
		.digitalCrownRotation($crownValue, from: -1_000_000, through: 1_000_000, sensitivity: .medium, isContinuous: false, isHapticFeedbackEnabled: true)
		.onChange(of: crownValue) { oldValue, newValue in
			GDTEngineWatchOS.crownRotated(by: newValue - oldValue)
		}
		.onAppear {
			isFocused = true
		}
	}

	/// Forwards a single touch to the engine, normalized to the screen.
	private func touchGesture(size: CGSize) -> some Gesture {
		DragGesture(minimumDistance: 0)
			.onChanged { value in
				let x = value.location.x / size.width
				let y = value.location.y / size.height
				if isTouching {
					GDTEngineWatchOS.drag(with: 0, x: x, y: y)
				} else {
					isTouching = true
					GDTEngineWatchOS.touch(with: 0, x: x, y: y, pressed: true)
				}
			}
			.onEnded { value in
				isTouching = false
				GDTEngineWatchOS.touch(with: 0, x: value.location.x / size.width, y: value.location.y / size.height, pressed: false)
			}
	}
}

// MARK: App

@main
struct GodotWatchApp: App {
	@Environment(\.scenePhase) private var scenePhase
	// Returning from the background passes through `.inactive`, so the old phase alone can't tell a resume.
	@State private var isInBackground = false

	var body: some Scene {
		WindowGroup {
			GodotContentView()
				.onAppear {
					EngineLoop.shared.start()
				}
		}
		.onChange(of: scenePhase) { _, newPhase in
			switch newPhase {
			case .active:
				if isInBackground {
					isInBackground = false
					GDTEngineWatchOS.exitBackground()
				} else {
					GDTEngineWatchOS.focusIn()
				}
			case .inactive:
				GDTEngineWatchOS.focusOut()
			case .background:
				isInBackground = true
				GDTEngineWatchOS.enterBackground()
			@unknown default:
				break
			}
		}
	}
}

/**************************************************************************/
/*  audio_driver_watchos.mm                                               */
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

#import "audio_driver_watchos.h"

#ifdef WATCHOS_ENABLED

#include "core/config/engine.h"
#include "core/config/project_settings.h"

#import <AVFoundation/AVFoundation.h>

struct AudioDriverWatchOS::Implementation {
	AVAudioEngine *engine = nil;
	AVAudioSourceNode *source = nil;
	id configuration_observer = nil;
	id interruption_observer = nil;
};

Error AudioDriverWatchOS::init() {
	AVAudioSession *session = [AVAudioSession sharedInstance];
	NSError *error = nil;
	// Playback isn't silenced by the watch's silent mode, matching how games behave on iOS.
	if (![session setCategory:AVAudioSessionCategoryPlayback error:&error]) {
		WARN_PRINT(vformat("AVAudioEngine: Couldn't set the audio session category: %s.", String::utf8(error.localizedDescription.UTF8String)));
	}
	// This fails when the app is launched in the background (e.g. while the watch screen is off),
	// so it isn't fatal: activation is retried when the app becomes active, see resume().
	if (![session setActive:YES error:&error]) {
		WARN_PRINT(vformat("AVAudioEngine: Couldn't activate the audio session yet: %s.", String::utf8(error.localizedDescription.UTF8String)));
	}

	mix_rate = session.sampleRate > 0 ? (int)session.sampleRate : _get_configured_mix_rate();
	const int latency_ms = Engine::get_singleton()->is_editor_hint() ? 15 : (int)GLOBAL_GET("audio/driver/output_latency");
	buffer_frames = Math::closest_power_of_2((uint32_t)(latency_ms * mix_rate / 1000));
	samples.resize(buffer_frames * 2);

	implementation = memnew(Implementation);
	implementation->engine = [[AVAudioEngine alloc] init];

	// Godot mixes interleaved 32-bit integers; the source node asks for non-interleaved floats.
	AVAudioFormat *format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:mix_rate channels:2];
	AudioDriverWatchOS *driver = this;
	implementation->source = [[AVAudioSourceNode alloc] initWithFormat:format
															 renderBlock:^OSStatus(BOOL *is_silence, const AudioTimeStamp *timestamp, AVAudioFrameCount frame_count, AudioBufferList *output) {
																 float *left = (float *)output->mBuffers[0].mData;
																 float *right = output->mNumberBuffers > 1 ? (float *)output->mBuffers[1].mData : left;
																 driver->render(frame_count, left, right);
																 return noErr;
															 }];

	[implementation->engine attachNode:implementation->source];
	[implementation->engine connect:implementation->source to:implementation->engine.mainMixerNode format:format];
	[implementation->engine prepare];

	// The engine stops itself when the audio configuration changes or the session is interrupted
	// (e.g. when the screen dims), so it's restarted afterwards.
	NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
	implementation->configuration_observer = [center addObserverForName:AVAudioEngineConfigurationChangeNotification
																object:implementation->engine
																 queue:[NSOperationQueue mainQueue]
															usingBlock:^(NSNotification *notification) {
																driver->resume();
															}];
	implementation->interruption_observer = [center addObserverForName:AVAudioSessionInterruptionNotification
															   object:session
																queue:[NSOperationQueue mainQueue]
														   usingBlock:^(NSNotification *notification) {
															   const NSUInteger type = [notification.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
															   if (type == AVAudioSessionInterruptionTypeEnded) {
																   driver->resume();
															   }
														   }];

	print_verbose(vformat("AVAudioEngine: output sampling rate: %d Hz, buffer frames: %d.", mix_rate, buffer_frames));
	return OK;
}

void AudioDriverWatchOS::start() {
	ERR_FAIL_NULL(implementation);
	active = true;
	NSError *error = nil;
	if (![implementation->engine startAndReturnError:&error]) {
		WARN_PRINT(vformat("AVAudioEngine: Couldn't start the audio engine yet: %s.", String::utf8(error.localizedDescription.UTF8String)));
	}
}

float AudioDriverWatchOS::get_latency() {
	return (float)buffer_frames / mix_rate + [AVAudioSession sharedInstance].outputLatency;
}

void AudioDriverWatchOS::render(uint32_t p_frames, float *r_left, float *r_right) {
	// Never block the audio thread; output silence while the engine holds the lock.
	if (!active || !mutex.try_lock()) {
		memset(r_left, 0, p_frames * sizeof(float));
		if (r_right != r_left) {
			memset(r_right, 0, p_frames * sizeof(float));
		}
		return;
	}

	start_counting_ticks();
	constexpr float SCALE = 1.0f / 2147483648.0f;
	uint32_t done = 0;
	while (done < p_frames) {
		const uint32_t frames = MIN(p_frames - done, buffer_frames);
		audio_server_process(frames, samples.ptr());
		for (uint32_t i = 0; i < frames; i++) {
			r_left[done + i] = samples[i * 2] * SCALE;
			if (r_right != r_left) {
				r_right[done + i] = samples[i * 2 + 1] * SCALE;
			}
		}
		done += frames;
	}
	stop_counting_ticks();
	mutex.unlock();
}

String AudioDriverWatchOS::get_output_device() {
	String outputs;
	for (AVAudioSessionPortDescription *port in [AVAudioSession sharedInstance].currentRoute.outputs) {
		if (!outputs.is_empty()) {
			outputs += ", ";
		}
		outputs += String::utf8(port.portType.UTF8String);
	}
	if (outputs.is_empty()) {
		outputs = "no output";
	}
	const bool running = implementation && implementation->engine.running;
	return outputs + (running ? " (running)" : " (stopped)");
}

void AudioDriverWatchOS::suspend() {
	if (implementation && active) {
		[implementation->engine pause];
	}
}

void AudioDriverWatchOS::resume() {
	if (!implementation || !active || implementation->engine.running) {
		return;
	}
	NSError *error = nil;
	if (![[AVAudioSession sharedInstance] setActive:YES error:&error]) {
		ERR_PRINT(vformat("AVAudioEngine: Couldn't activate the audio session: %s.", String::utf8(error.localizedDescription.UTF8String)));
		return;
	}
	if (![implementation->engine startAndReturnError:&error]) {
		ERR_PRINT(vformat("AVAudioEngine: Couldn't start the audio engine: %s.", String::utf8(error.localizedDescription.UTF8String)));
	}
}

void AudioDriverWatchOS::finish() {
	if (!implementation) {
		return;
	}
	active = false;
	[[NSNotificationCenter defaultCenter] removeObserver:implementation->configuration_observer];
	[[NSNotificationCenter defaultCenter] removeObserver:implementation->interruption_observer];
	[implementation->engine stop];
	memdelete(implementation);
	implementation = nullptr;
	[[AVAudioSession sharedInstance] setActive:NO error:nil];
}

AudioDriverWatchOS::~AudioDriverWatchOS() {
	finish();
}

#endif // WATCHOS_ENABLED

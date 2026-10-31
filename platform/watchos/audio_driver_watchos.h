/**************************************************************************/
/*  audio_driver_watchos.h                                                */
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

#include "core/os/mutex.h"
#include "core/templates/local_vector.h"
#include "servers/audio/audio_driver.h"

// Plays the engine's mix through AVAudioEngine, which is the low-level audio API available on watchOS
// (the AudioUnit-based CoreAudio driver used on iOS isn't).
class AudioDriverWatchOS : public AudioDriver {
	struct Implementation; // Holds the Objective-C objects, so this header stays plain C++.
	Implementation *implementation = nullptr;

	Mutex mutex;
	int mix_rate = 44100;
	uint32_t buffer_frames = 512;
	LocalVector<int32_t> samples;
	bool active = false;

public:
	virtual const char *get_name() const override { return "AVAudioEngine"; }

	virtual Error init() override;
	virtual void start() override;
	virtual int get_mix_rate() const override { return mix_rate; }
	virtual SpeakerMode get_speaker_mode() const override { return SPEAKER_MODE_STEREO; }
	virtual float get_latency() override;

	virtual void lock() override { mutex.lock(); }
	virtual void unlock() override { mutex.unlock(); }
	virtual void finish() override;

	// Reports where watchOS routes the audio and whether the engine runs, which helps diagnose silence.
	virtual String get_output_device() override;

	// Fills the system's output buffers with p_frames frames, called from the audio thread.
	void render(uint32_t p_frames, float *r_left, float *r_right);

	// Called when the app goes to the background and when it becomes active again,
	// which also starts the engine if that wasn't possible at launch.
	void suspend();
	void resume();

	~AudioDriverWatchOS();
};

#endif // WATCHOS_ENABLED

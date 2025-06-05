/*
 * ACELP audio effect based on DISTRHO Plugin Framework (DPF)
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2025 Cheetah van Oranje <vst@cheetah.cat>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "PluginACELP.hpp"
#include "debug.h"

// #include <samplerate.h>

START_NAMESPACE_DISTRHO

// -----------------------------------------------------------------------

PluginACELP::PluginACELP()
    : Plugin(paramCount, 0, 0), dsp(getSampleRate())  // paramCount param(s), presetCount program(s), 0 states
{

    // smooth_gain = new CParamSmooth(20.0f, getSampleRate());

	for (int midiKey = 0; midiKey < 255; midiKey++) {
		midiEnergyMap[midiKey] = 0;
	}
	for (unsigned p = 0; p < paramCount; ++p) {
		Parameter param;
		initParameter(p, param);
		setParameterValue(p, param.ranges.def);
	}
    // reset
    deactivate();
}

PluginACELP::~PluginACELP() {
    // delete smooth_gain;
	
}

// -----------------------------------------------------------------------
// Init

void PluginACELP::initParameter(uint32_t index, Parameter& parameter) {
  if (index < paramCount) {
    parameter.hints      = kParameterIsAutomatable;
    parameter.name       = PARAMS[index].name;
    parameter.symbol     = PARAMS[index].symbol;
    parameter.ranges.min = PARAMS[index].range_min;
    parameter.ranges.def = banks[DEFAULT_BANK].presets[DEFAULT_PRESET].params[index];
    parameter.ranges.max = PARAMS[index].range_max;
    parameter.unit       = PARAMS[index].unit;
  }
}

/**
  Set the name of the program @a index.
  This function will be called once, shortly after the plugin is created.
*/
void PluginACELP::initProgramName(uint32_t index, String& programName) {
    // if (index < presetCount) {
    //     programName = factoryPresets[index].name;
    // }
}

// -----------------------------------------------------------------------
// Internal data

/**
  Optional callback to inform the plugin about a sample rate change.
*/
void PluginACELP::sampleRateChanged(double newSampleRate) {
	dsp.sampleRateChanged(newSampleRate);
}

/**
  Get the current value of a parameter.
*/
float PluginACELP::getParameterValue(uint32_t index) const {
  return dsp.getParameterValue(index);
}

/**
  Change a parameter value.
*/
void PluginACELP::setParameterValue(uint32_t index, float value) {
  dsp.setParameterValue(index, value);
}

void PluginACELP::setState(const char* key, const char* value) {
  if (std::strcmp(key, "preset") == 0) {
    for (int b = 0; b < NUM_BANKS; b++) {
      for (int p = 0; p < PRESETS_PER_BANK; p++) {
        if (std::strcmp(value, banks[b].presets[p].name) == 0) {
          bank = b;
          preset = p;

          // backward compatibility
        //   setParameterValue(paramDecay, banks[b].presets[p].params[paramDecay]);
        }
      }
    }
  }
}


/**
  Load a program.
  The host may call this function from any context,
  including realtime processing.
*/
void PluginACELP::loadProgram(uint32_t index) {
    // if (index < presetCount) {
    //     for (int i=0; i < paramCount; i++) {
    //         setParameterValue(i, factoryPresets[index].params[i]);
    //     }
    // }
}

// -----------------------------------------------------------------------
// Process

void PluginACELP::activate() {
  sampleRateChanged(getSampleRate());
}
void PluginACELP::deactivate()  {
}
// void PluginACELP::threadFunction() {
// 	DEBUG_PRINTF("entering background thread\n");
// 	threadRunning.store(true, std::memory_order_release);

// 	Word16 playback_frame[ACELP_DUAL_CHAN_FRAME_SIZE];         // Temp buffer for each new frame
// 	// Word16 playback_frame[ACELP_DUAL_CHAN_AUDIO_SIZE];         // TEST NEW!!
//     Word16 last_good_frame[ACELP_DUAL_CHAN_AUDIO_SIZE] = { 0 }; // Backup of last valid frame
// 	hex_to_word16_array(fuckFrame, last_good_frame, ACELP_DUAL_CHAN_FRAME_SIZE);
// 	//
//     float decode_buf[ACELP_DUAL_CHAN_AUDIO_SIZE];
//     float upsample_buf[ACELP_DUAL_CHAN_AUDIO_SIZE * UPSAMPLE_RATIO];
// 	//
// 	int has_valid_frame = 1;
// 	//
//     while (!stopThread.load(std::memory_order_acquire)) {
//         // Try to read a new encoded frame from the queue
//         if (cat_ringbuffer_read_space(encoded_frame_queue) >= sizeof(playback_frame)) {
//             // Read it
//             //size_t read_bytes = 
// 			cat_ringbuffer_read(encoded_frame_queue, (char *)playback_frame, sizeof(playback_frame));
//             // DEBUG_PRINTF("RINGBUFFER ENCODEDFRAME QUEUE readbytes=%lu, size %d\n", read_bytes, sizeof(encoded_frame));
// 			memcpy(&last_good_frame, &playback_frame, sizeof(playback_frame));
// 			// print_word16_hex(encoded_frame, sizeof(encoded_frame));
// 			// DEBUG_PRINTF("new nice frame came around");
//             has_valid_frame = 1;
//         } else if (has_valid_frame) {
//             // No new frame: reuse the last good one
// 			// DEBUG_PRINTF("reusing old frame");
//             memcpy(&playback_frame, &last_good_frame, sizeof(playback_frame));
//         } else {
//             // No data available yet: silence output or skip
//             // usleep(1000);
//         	std::this_thread::sleep_for(std::chrono::milliseconds(5));
//             continue;
//         }	
		

//         // Decode and upsample
// 		// DEBUG_PRINTF("received frame:\n");
// 		// print_word16_hex(playback_frame, 432);
//         decode_acelp(playback_frame, decode_buf);
//         // upsample_6x(decode_buf, upsample_buf, ACELP_DUAL_CHAN_AUDIO_SIZE * UPSAMPLE_RATIO);
// 		// --- Upsample 6x ---
// 		spx_uint32_t in_len = ACELP_DUAL_CHAN_AUDIO_SIZE;
// 		spx_uint32_t out_len = ACELP_DUAL_CHAN_AUDIO_SIZE * UPSAMPLE_RATIO;
// 		// memcpy(down_in, in, sizeof(float) * in_len);
// 		speex_resampler_process_float(resampler_up, 0, decode_buf, &in_len, upsample_buf, &out_len);


//         // Wait until there's enough space to write
//         size_t bytes_needed = sizeof(float) * ACELP_DUAL_CHAN_AUDIO_SIZE * UPSAMPLE_RATIO;
//         while (!stopThread.load(std::memory_order_acquire) && cat_ringbuffer_write_space(pcm_output_buffer) < bytes_needed) {
//         	std::this_thread::sleep_for(std::chrono::milliseconds(5));
//         }

//         // Write the samples to output buffer
//         // size_t wrote_bytes = 
// 		cat_ringbuffer_write(pcm_output_buffer, (char *)upsample_buf, bytes_needed);
// 		// DEBUG_PRINTF("decoding to %d 48khz samples=%d bytes, wrote %d bytes\n", ACELP_DUAL_CHAN_AUDIO_SIZE * UPSAMPLE_RATIO, bytes_needed, wrote_bytes);
// 	}
// 	//
// 	threadRunning.store(false, std::memory_order_release);
// 	DEBUG_PRINTF("bg: leaving thread\n");
// 	return;
// }

void PluginACELP::run(const float** inputs, float** outputs,
                      uint32_t frames,
                      const MidiEvent* midiEvents, uint32_t midiEventCount) {
	dsp.run(inputs, outputs, frames);
	
    for (int i = 0; i < midiEventCount; ++i) {
        MidiEvent event = midiEvents[i];
		uint8_t eventType = event.data[0];
		uint8_t eventKey = event.data[1];
		uint8_t eventParam = event.data[2];
		switch (eventType) {
			case 128: // release
				midiEnergyMap[eventKey] = eventParam;
				break;
			case 144: // attack
				midiEnergyMap[eventKey] = eventParam;
				break;
			case 176: // knob
				midiEnergyMap[eventKey] = eventParam;
				break;
			case 129: // pad1
				midiEnergyMap[eventKey] = eventParam;
				break;
			case 145: // pad2
				midiEnergyMap[eventKey] = eventParam;
				break;
			case 209: // pad3
				midiEnergyMap[eventKey] = eventParam;
				break;
		}			
		DEBUG_PRINTF("midi %d %d %d\n", eventType, eventKey, eventParam);
    }
}

// -----------------------------------------------------------------------

Plugin* createPlugin() {
    return new PluginACELP();
}

// -----------------------------------------------------------------------

END_NAMESPACE_DISTRHO

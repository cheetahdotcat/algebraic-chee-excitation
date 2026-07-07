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
#include "corrupt.hpp"
#include "debug.h"
#include "codec_viz.hpp"

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
  for (int i = 0; i < 128; i++) hex_string[i] = 0;
  dsp.setCallback(this);
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

void PluginACELP::initState(uint32_t index, State& state) {
  switch (index) {
    case 0:
        state.key = "top-left";
        state.label = "Top Left";
        break;
    case 1:
        state.key = "top-center";
        state.label = "Top Center";
        break;
    case 2:
        state.key = "top-right";
        state.label = "Top Right";
        break;
    case 3:
        state.key = "middle-left";
        state.label = "Middle Left";
        break;
    case 4:
        state.key = "middle-center";
        state.label = "Middle Center";
        break;
    case 5:
        state.key = "middle-right";
        state.label = "Middle Right";
        break;
    case 6:
        state.key = "bottom-left";
        state.label = "Bottom Left";
        break;
    case 7:
        state.key = "bottom-center";
        state.label = "Bottom Center";
        break;
    case 8:
        state.key = "bottom-right";
        state.label = "Bottom Right";
        break;
  }
  state.hints = kStateIsHostWritable;
  state.defaultValue = "false";
}
void PluginACELP::setState(const char* key, const char* value) {
  // if (std::strcmp(key, "preset") == 0) {
  //   for (int b = 0; b < NUM_BANKS; b++) {
  //     for (int p = 0; p < PRESETS_PER_BANK; p++) {
  //       if (std::strcmp(value, banks[b].presets[p].name) == 0) {
  //         bank = b;
  //         preset = p;

  //         // backward compatibility
  //       //   setParameterValue(paramDecay, banks[b].presets[p].params[paramDecay]);
  //       }
  //     }
  //   }
  // }
}

String PluginACELP::getState(const char* key) const {
    static const String sTrue ("true");
    static const String sFalse("false");

    // // check which block changed
    // /**/ if (std::strcmp(key, "top-left") == 0)
    //     return fParamGrid[0] ? sTrue : sFalse;
    // else if (std::strcmp(key, "top-center") == 0)
    //     return fParamGrid[1] ? sTrue : sFalse;
    // else if (std::strcmp(key, "top-right") == 0)
    //     return fParamGrid[2] ? sTrue : sFalse;
    // else if (std::strcmp(key, "middle-left") == 0)
    //     return fParamGrid[3] ? sTrue : sFalse;
    // else if (std::strcmp(key, "middle-center") == 0)
    //     return fParamGrid[4] ? sTrue : sFalse;
    // else if (std::strcmp(key, "middle-right") == 0)
    //     return fParamGrid[5] ? sTrue : sFalse;
    // else if (std::strcmp(key, "bottom-left") == 0)
    //     return fParamGrid[6] ? sTrue : sFalse;
    // else if (std::strcmp(key, "bottom-center") == 0)
    //     return fParamGrid[7] ? sTrue : sFalse;
    // else if (std::strcmp(key, "bottom-right") == 0)
    //     return fParamGrid[8] ? sTrue : sFalse;

    return sFalse;
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
//
void shorts_to_hex_string(const Word16 *shorts, size_t count, char *output) {
    for (size_t i = 0; i < count; ++i) {
        sprintf(output + i * 4, "%04X", (Word16)shorts[i]);
    }
    output[count * 4] = '\0';  // Null-terminate
}
// 432
void PluginACELP::onVocoderFrame(Word16 *frame) {
  // DEBUG_PRINTF("ONFRAME 1\n");
  // shorts_to_hex_string(frame, 432, hex_string);
  // DEBUG_PRINTF("ONFRAME 2\n");
  // // updateStateValue("codec_frame", hex_string);
  // DEBUG_PRINTF("ONFRAME 3\n");
	float pVolume = dsp.getParameterValue(paramVolume);
	float pCorrMode = dsp.getParameterValue(paramCorruptionMode);
	float pCorrInt = dsp.getParameterValue(paramCorruptionIntensity);
	float pCorrMag = dsp.getParameterValue(paramCorruptionMagnitude);
	float pCodecType = dsp.getParameterValue(paramCodecType);
  //
  {
    if (midiEnergyMap[60]>0) {
      corrupt_by_wrong_interleave(frame, 432);
    }
    if (midiEnergyMap[61]>0) {
      corrupt_by_overflow(frame, 432);
    }
    if (midiEnergyMap[62]>0) {
      bit_desync_shift_left_Word16(frame, 23);
      // int r = rand() % 100;
      // if (r < midiEnergyMap[70]) {
      // 	// allowFrameWrite = 0;
      // }
    } else {
      // allowFrameWrite = allowFrameWrite || 1;
    }
    if (midiEnergyMap[63]>0) {
      random_bit_desync_Word16(frame, 432, pCorrInt, pCorrMag);
    }
    if (midiEnergyMap[64]>0) {
      corrupt_bit_flips_Word16(frame, 432, pCorrInt);
      // random_bit_desync_Word16(encoder_Interleaved_coded_array, 432, midiEnergyMap[70], midiEnergyMap[65]);
    }
  }
  //
  for (int midiKey = 0; midiKey < 255; midiKey++) {
    if (midiEnergyMap[midiKey] > 0) {
      if (round(pCorrMode) == 1) {
        frame[ midiKey ] = midiEnergyMap[midiKey];
      }
      if (round(pCorrMode) == 2) {
        frame[ midiKey ] -= pCorrMag;
      }
      if (round(pCorrMode) == 3) {
        frame[ midiKey ] += pCorrMag;
      }
      if (round(pCorrMode) == 4) {
        frame[ midiKey ] *= pCorrMag;
      }
      if (round(pCorrMode) == 5) {
        frame[ midiKey ] /= pCorrMag;
      }
      DEBUG_PRINTF("applying corruption %d\n", midiKey, midiEnergyMap[midiKey]);
    }
  }
  // Publish the final (post-corruption) encoded frame for the UI data-stream
  // visualization. This is exactly the bitstream that gets decoded.
  codec_viz_write((const short*)frame);
}
void PluginACELP::run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) {
	dsp.run(inputs, outputs, frames);
  for (int i = 0; i < midiEventCount; ++i) {
    MidiEvent event = midiEvents[i];
    uint8_t eventType = event.data[0];
    uint8_t eventKey = event.data[1];
    uint8_t eventParam = event.data[2];
    // printf("processing midi: %d- %d,%d,%d\n", i, eventType, eventKey, eventParam);
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
		// DEBUG_PRINTF("midi %d %d %d\n", eventType, eventKey, eventParam);
    }
}

// -----------------------------------------------------------------------

Plugin* createPlugin() {
    return new PluginACELP();
}

// -----------------------------------------------------------------------

END_NAMESPACE_DISTRHO

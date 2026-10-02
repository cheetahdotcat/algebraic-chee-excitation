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

// #include <samplerate.h>

START_NAMESPACE_DISTRHO

// -----------------------------------------------------------------------

PluginACELP::PluginACELP()
    : Plugin(paramCount, 0, 1), dsp(getSampleRate())  // paramCount param(s), 0 programs, 1 state (cc_map)
{

    // smooth_gain = new CParamSmooth(20.0f, getSampleRate());

	for (unsigned p = 0; p < paramCount; ++p) {
		Parameter param;
		initParameter(p, param);
		setParameterValue(p, param.ranges.def);
	}
  for (int cc = 0; cc < 128; cc++) ccMap[cc].store(-1, std::memory_order_relaxed);
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
    if (index == paramCodecType || index == paramCorruptionMode || index == paramLpStage || index == paramHdMath ||
        index == paramCodecSplit || index == paramDecMath || index == paramAlgoSplit || index == paramFec ||
        index == paramCorruptTarget || index == paramGainVq || index == paramDecGainVq ||
        index == paramNetMode || index == paramNetChannel)
      parameter.hints |= kParameterIsInteger;
    if (index == paramVolume)
      parameter.hints |= kParameterIsHidden; // no-op, kept for index stability
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

// One state: the MIDI CC map, "cc:param,cc:param,..." (saved with the session).
void PluginACELP::initState(uint32_t index, State& state) {
  if (index == 0) {
    state.key = "cc_map";
    state.label = "MIDI CC map";
    state.defaultValue = "";
    state.hints = kStateIsHostWritable;
  }
}

void PluginACELP::setState(const char* key, const char* value) {
  if (std::strcmp(key, "cc_map") != 0) return;
  for (int cc = 0; cc < 128; cc++) ccMap[cc].store(-1, std::memory_order_relaxed);
  const char* p = value;
  while (p != nullptr && *p != '\0') {
    int cc = -1, param = -1;
    if (std::sscanf(p, "%d:%d", &cc, &param) == 2 && cc >= 0 && cc < 128 && param > 0 && param < paramCount)
      ccMap[cc].store((int8_t)param, std::memory_order_relaxed);
    p = std::strchr(p, ',');
    if (p != nullptr) ++p;
  }
  ccMapGen.fetch_add(1, std::memory_order_release);
}

String PluginACELP::getState(const char* key) const {
  if (std::strcmp(key, "cc_map") != 0) return String();
  char buf[128 * 8 + 1];
  int n = 0;
  buf[0] = '\0';
  for (int cc = 0; cc < 128; cc++) {
    const int param = ccMap[cc].load(std::memory_order_relaxed);
    if (param >= 0)
      n += std::snprintf(buf + n, sizeof(buf) - n, "%s%d:%d", n ? "," : "", cc, param);
  }
  return String(buf);
}

int PluginACELP::ccForParam(uint32_t param) const {
  for (int cc = 0; cc < 128; cc++)
    if (ccMap[cc].load(std::memory_order_relaxed) == (int)param) return cc;
  return -1;
}

void PluginACELP::clearCcForParam(uint32_t param) {
  for (int cc = 0; cc < 128; cc++)
    if (ccMap[cc].load(std::memory_order_relaxed) == (int)param) ccMap[cc].store(-1, std::memory_order_relaxed);
  ccMapGen.fetch_add(1, std::memory_order_release);
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
// MIDI: a chromatic block of 12 keys from MIDI note 48 triggers corruption
// while held (see CheetahDSP::MIDI_CORRUPT_BASE_NOTE); all other notes are
// ignored. Any channel.
void PluginACELP::run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) {
  for (uint32_t i = 0; i < midiEventCount; ++i) {
    const MidiEvent& event = midiEvents[i];
    if (event.size < 2 || event.size > MidiEvent::kDataSize) continue;
    const uint8_t status = event.data[0] & 0xF0;
    const uint8_t d1 = event.data[1];
    const uint8_t d2 = event.size > 2 ? event.data[2] : 0; // channel pressure is 2 bytes
    if (status == 0x90 || status == 0x80) {
      // note-off (or note-on with velocity 0) always releases; the release
      // velocity is ignored
      const int key = (int)d1 - CheetahDSP::MIDI_CORRUPT_BASE_NOTE;
      if (key >= 0 && key < CheetahDSP::MIDI_CORRUPT_KEYS) {
        if (status == 0x90 && d2 > 0) dsp.midiCorruptNoteOn(key, d2);
        else                          dsp.midiCorruptNoteOff(key);
      }
    } else if (status == 0xD0) {
      dsp.setMidiCorruptPressure(d1);           // channel aftertouch
    } else if (status == 0xA0) {
      const int key = (int)d1 - CheetahDSP::MIDI_CORRUPT_BASE_NOTE;
      if (key >= 0 && key < CheetahDSP::MIDI_CORRUPT_KEYS)
        dsp.setMidiCorruptPressure(d2);         // poly aftertouch on a corruption key
    } else if (status == 0xB0 && (d1 == 120 || d1 == 123)) {
      dsp.clearMidiCorruptKeys(); // All Sound Off / All Notes Off
    } else if (status == 0xB0 && d1 < 120) {
      // MIDI learn: the next CC binds to the armed parameter (one CC per parameter)
      const int armed = learnArmed.exchange(-1, std::memory_order_acq_rel);
      if (armed > 0) {
        for (int cc = 0; cc < 128; cc++)
          if (ccMap[cc].load(std::memory_order_relaxed) == armed) ccMap[cc].store(-1, std::memory_order_relaxed);
        ccMap[d1].store((int8_t)armed, std::memory_order_relaxed);
        ccMapGen.fetch_add(1, std::memory_order_release);
      }
      const int param = ccMap[d1].load(std::memory_order_relaxed);
      if (param > 0 && param < paramCount) {
        float v = PARAMS[param].range_min + (PARAMS[param].range_max - PARAMS[param].range_min) * (d2 / 127.0f);
        if (param == paramCodecType || param == paramCorruptionMode || param == paramLpStage || param == paramHdMath ||
            param == paramCodecSplit || param == paramDecMath || param == paramAlgoSplit || param == paramFec ||
            param == paramCorruptTarget || param == paramGainVq || param == paramDecGainVq ||
            param == paramNetMode || param == paramNetChannel)
          v = std::round(v);
        dsp.setParameterValue(param, v);
        requestParameterValueChange(param, v); // host + UI follow
      }
    }
  }
  dsp.run(inputs, outputs, frames);
}

// -----------------------------------------------------------------------

Plugin* createPlugin() {
    return new PluginACELP();
}

// -----------------------------------------------------------------------

END_NAMESPACE_DISTRHO

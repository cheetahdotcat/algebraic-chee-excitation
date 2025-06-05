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

#ifndef DISTRHO_PLUGIN_INFO_H
#define DISTRHO_PLUGIN_INFO_H

#include "CheeExcitationArtwork.hpp"
#include "../../common/Param.hpp"

// The plugin name.
// This is used to identify your plugin before a Plugin instance can be created.
#define DISTRHO_PLUGIN_NAME  "Algebraic-Chee-Excitation"
// The plugin brand name. Used for the LV2 metadata and VST3 UI interface.
// Must be a valid C++ identifier, i.e. can not contain spaces or dashes.
#define DISTRHO_PLUGIN_BRAND "vst.cheetah.cat"
// The plugin URI when exporting in LV2 format.
// See https://lv2plug.in/book/#_manifest_ttl_in
#define DISTRHO_PLUGIN_URI   "https://vst.cheetah.cat/plugins/acelp"
// The plugin id when exporting in CLAP format, in reverse URI form
#define DISTRHO_PLUGIN_CLAP_ID "cat.cheetah.vst.acelp"

#define DISTRHO_PLUGIN_HAS_UI        1
#define DISTRHO_UI_USE_NANOVG        0

#define DISTRHO_PLUGIN_IS_RT_SAFE       1
#define DISTRHO_PLUGIN_NUM_INPUTS       1
#define DISTRHO_PLUGIN_NUM_OUTPUTS      1
#define DISTRHO_PLUGIN_WANT_STATE       1
#define DISTRHO_PLUGIN_WANT_TIMEPOS     0
#define DISTRHO_PLUGIN_WANT_PROGRAMS    1
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  1
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT 0

// See http://lv2plug.in/ns/lv2core#ref-classes
#define DISTRHO_PLUGIN_LV2_CATEGORY "lv2:FilterPlugin"
// See https://github.com/DISTRHO/DPF/blob/1504e7d327bfe0eac6a889cecd199c963d35532f/distrho/DistrhoInfo.hpp#L717
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Filter|Restoration|Mono"
// See https://github.com/DISTRHO/DPF/blob/1504e7d327bfe0eac6a889cecd199c963d35532f/distrho/DistrhoInfo.hpp#L761
#define DISTRHO_PLUGIN_CLAP_FEATURES "audio-effect", "phase-vocoder", "mono"


#define DISTRHO_UI_DEFAULT_WIDTH CheeExcitationArtwork::backgroundWidth
#define DISTRHO_UI_DEFAULT_HEIGHT CheeExcitationArtwork::backgroundHeight




enum Parameters
{
  paramVolume = 0,
  paramCodecType,
  paramCodecBitrate,
  paramCorruptionMode,
  paramCorruptionIntensity,
  paramCorruptionMagnitude
};

static const int paramCount = 6;
static const Param PARAMS[paramCount] = {
  {paramVolume,        "Volume",   "volume",    0.0f,   100.0f,   "%"},
  {paramCodecType,      "Early Level", "early_level",  0.0f,   100.0f,   "%"},
  {paramCodecBitrate,       "Late Level",  "late_level",   0.0f,   100.0f,   "%"},
  {paramCorruptionMode,       "Size",        "size",        10.0f,    60.0f,   "m"},
  {paramCorruptionIntensity,      "Width",       "width",       50.0f,   150.0f,   "%"},
  {paramCorruptionMagnitude,   "Predelay",    "delay",        0.0f,   100.0f,  "ms"}
  // {paramDiffuse,    "Diffuse",     "diffuse",      0.0f,   100.0f,   "%"},
  // {paramLowCut,     "Low Cut",     "low_cut",      0.0f,   200.0f,  "Hz"},
  // {paramLowXover,   "Low Cross",   "low_xo",     200.0f,  1200.0f,  "Hz"},
  // {paramLowMult,    "Low Mult",    "low_mult",     0.5f,     2.5f,   "X"},
  // {paramHighCut,    "High Cut",    "high_cut",  1000.0f, 16000.0f,  "Hz"},
  // {paramHighXover,  "High Cross",  "high_xo",   1000.0f, 16000.0f,  "Hz"},
  // {paramHighMult,   "High Mult",   "high_mult",    0.2f,     1.2f,   "X"},
  // {paramSpin,       "Spin",        "spin",         0.0f,    10.0f,  "Hz"},
  // {paramWander,     "Wander",      "wander",       0.0f,    40.0f,  "ms"},
  // {paramDecay,      "Decay",       "decay",        0.1f,    10.0f,   "s"},
  // {paramEarlySend,  "Early Send",  "early_send",   0.0f,   100.0f,   "%"},
  // {paramModulation, "Modulation",  "modulation",   0.0f,   100.0f,   "%"}
};

static const int NUM_BANKS = 5;
static const int PRESETS_PER_BANK = 5;

typedef struct {
  const char *name;
  const float params[paramCount];
} Preset;

typedef struct {
  const char *name;
  const Preset presets[PRESETS_PER_BANK];
} Bank;

static const Bank banks[NUM_BANKS] = {
  {
    "Test", {
      {"Large Bright Hall",      { 80.0,  10.0, 20.0, 40.0, 100.0,  20.0 }},
      {"Large Clear Hall",       { 80.0,  10.0, 20.0, 40.0, 100.0,  12.0 }},
      {"Large Dark Hall",        { 80.0,  10.0, 20.0, 40.0, 100.0,  20.0 }},
      {"Large Vocal Hall",       { 80.0,  10.0, 20.0, 40.0,  80.0,  12.0 }},
      {"Great Hall",             { 80.0,  10.0, 20.0, 50.0,  90.0,  20.0 }},
    }
  }
};

static const int DEFAULT_BANK   = 0; // Small Halls
static const int DEFAULT_PRESET = 1; // Second preset in each bank

#endif // DISTRHO_PLUGIN_INFO_H

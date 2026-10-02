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
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_TIMEPOS     0
#define DISTRHO_PLUGIN_WANT_PROGRAMS    1
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  1
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT 0
// The UI reads the codec visualization feeds and MIDI-learn state straight
// from its own plugin instance.
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1
// MIDI CC learn: the DSP changes parameters and tells the host/UI.
#define DISTRHO_PLUGIN_WANT_PARAMETER_VALUE_CHANGE_REQUEST 1

// See http://lv2plug.in/ns/lv2core#ref-classes
#define DISTRHO_PLUGIN_LV2_CATEGORY "lv2:FilterPlugin"
// See https://github.com/DISTRHO/DPF/blob/1504e7d327bfe0eac6a889cecd199c963d35532f/distrho/DistrhoInfo.hpp#L717
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Filter|Restoration|Mono"
// See https://github.com/DISTRHO/DPF/blob/1504e7d327bfe0eac6a889cecd199c963d35532f/distrho/DistrhoInfo.hpp#L761
#define DISTRHO_PLUGIN_CLAP_FEATURES "audio-effect", "phase-vocoder", "mono"


#define DISTRHO_UI_DEFAULT_WIDTH 1100
#define DISTRHO_UI_DEFAULT_HEIGHT 864




enum Parameters
{
  paramVolume = 0,
  paramCodecType,
  paramCodecBitrate,
  paramCorruptionMode,
  paramCorruptionIntensity,
  paramCorruptionMagnitude,
  // HD ACELP linear-prediction modification (ignored by the TETRA codec)
  paramLpStage,
  paramLpWarp,
  paramLpDepth,
  paramLpOrder,
  paramLpSmooth,
  paramLpFreeze,
  paramLpCrush,
  paramHdMath,
  // HD ACELP decoder-side excitation modification
  paramExcPitch,
  paramExcVoice,
  paramExcNoise,
  // Encoder/decoder split (codec group): decoder-side rate and math
  paramCodecSplit,
  paramDecBitrate,
  paramDecMath,
  // Decoder-side LP set (used when LP Stage = SPLIT)
  paramDecLpWarp,
  paramDecLpDepth,
  paramDecLpOrder,
  paramDecLpSmooth,
  paramDecLpFreeze,
  paramDecLpCrush,
  // Algorithm constants (encoder side / linked), split toggle, decoder side
  paramAlgoSplit,
  paramPreEmph,
  paramLsfPred,
  paramGainOfs,
  paramSharpen,
  paramDecPreEmph,
  paramDecLsfPred,
  paramDecGainOfs,
  paramDecSharpen,
  // TETRA-style channel protection (T+ codecs): 0 off .. 4 max
  paramFec,
  // What the bitstream corruption hits (CorruptTarget): 0 = everything
  paramCorruptTarget,
  // Gain quantizer per side (algorithm group): 0 scalar, 1 indexed joint VQ
  paramGainVq,
  paramDecGainVq,
  // LAN link (HD / TETRA+): 0 off, 1 send, 2 receive, 3 loop
  paramNetMode,
  paramNetChannel,
  paramNetLoss,
  paramNetJitter,
  // LP lattice page (main set, then decoder set for STAGE = SPLIT)
  paramLpTaper,
  paramLpResonance,
  paramLpBand,
  paramLpMirror,
  paramLpJitter,
  paramDecLpTaper,
  paramDecLpResonance,
  paramDecLpBand,
  paramDecLpMirror,
  paramDecLpJitter
};

static const int paramCount = 53;
static const Param PARAMS[paramCount] = {
  // Unused; kept (hidden) so the indices of all other parameters stay stable.
  {paramVolume,               "Volume",               "volume",         0.0f,   100.0f,   "%"},
  // Codec: 0=TETRA (ETSI, 8kHz), 1/2/3=HD ACELP 16/32/48kHz,
  //        4/5=TETRA+ (ETSI-style chain with finer quantizers) 8/16kHz
  {paramCodecType,            "Codec Type",           "codec_type",     0.0f,     5.0f,   ""},
  // HD ACELP only: bitrate mode 0..6 (0% = ~7 kbit/s ... 100% = ~27-71 kbit/s)
  {paramCodecBitrate,         "Codec Bitrate",        "codec_bitrate",  0.0f,   100.0f,   "%"},
  // Mode: 0=off; 1-5 musical (LSP,pitch,codebook,gain,freeze); 6-10 extreme
  // (bit-flips, bit-slip, burst, overflow, reinterleave). See CorruptMode.
  {paramCorruptionMode,       "Corruption Mode",      "corr_mode",      0.0f,    10.0f,   ""},
  {paramCorruptionIntensity,  "Corruption Intensity", "corr_int",       0.0f,   100.0f,   "%"},
  {paramCorruptionMagnitude,  "Corruption Magnitude", "corr_mag",       0.0f,   100.0f,   "%"},
  // 0 = decoder (resynthesize through the modified envelope),
  // 1 = encoder (transmit the modified envelope; the codec fights it),
  // 2 = split (main set in the encoder, decoder set in the decoder)
  {paramLpStage,              "LP Stage",             "lp_stage",       0.0f,     2.0f,   ""},
  {paramLpWarp,               "LP Formant Warp",      "lp_warp",     -100.0f,   100.0f,   "%"},
  {paramLpDepth,              "LP Formant Depth",     "lp_depth",    -100.0f,   100.0f,   "%"},
  {paramLpOrder,              "LP Order",             "lp_order",       0.0f,    32.0f,   ""},
  {paramLpSmooth,             "LP Smooth",            "lp_smooth",      0.0f,   100.0f,   "%"},
  {paramLpFreeze,             "LP Freeze",            "lp_freeze",      0.0f,   100.0f,   "%"},
  {paramLpCrush,              "LP Crush",             "lp_crush",       0.0f,   100.0f,   "%"},
  // HD ACELP arithmetic: 0 = float, 1 = 16-bit fixed point (ETSI style)
  {paramHdMath,               "HD Math",              "hd_math",        0.0f,     1.0f,   ""},
  // Pitch shift of the decoded excitation (lag rescaling)
  {paramExcPitch,             "Excitation Pitch",     "exc_pitch",    -12.0f,    12.0f,  "st"},
  // Pitch-gain scale: 0% = noise only (whisper), >100% = buzzier
  {paramExcVoice,             "Excitation Voicing",   "exc_voice",      0.0f,   200.0f,   "%"},
  // Algebraic-codebook gain scale
  {paramExcNoise,             "Excitation Noise",     "exc_noise",    -30.0f,    12.0f,  "dB"},
  {paramCodecSplit,           "Codec Split",          "codec_split",    0.0f,     1.0f,   ""},
  {paramDecBitrate,           "Dec Bitrate",          "dec_bitrate",    0.0f,   100.0f,   "%"},
  {paramDecMath,              "Dec Math",             "dec_math",       0.0f,     1.0f,   ""},
  {paramDecLpWarp,            "Dec LP Warp",          "dec_lp_warp", -100.0f,   100.0f,   "%"},
  {paramDecLpDepth,           "Dec LP Depth",         "dec_lp_depth",-100.0f,   100.0f,   "%"},
  {paramDecLpOrder,           "Dec LP Order",         "dec_lp_order",   0.0f,    32.0f,   ""},
  {paramDecLpSmooth,          "Dec LP Smooth",        "dec_lp_smooth",  0.0f,   100.0f,   "%"},
  {paramDecLpFreeze,          "Dec LP Freeze",        "dec_lp_freeze",  0.0f,   100.0f,   "%"},
  {paramDecLpCrush,           "Dec LP Crush",         "dec_lp_crush",   0.0f,   100.0f,   "%"},
  {paramAlgoSplit,            "Algorithm Split",      "algo_split",     0.0f,     1.0f,   ""},
  {paramPreEmph,              "Pre-emphasis",         "pre_emph",       0.0f,     0.95f,  ""},
  {paramLsfPred,              "LSF Prediction",       "lsf_pred",       0.0f,     0.95f,  ""},
  {paramGainOfs,              "Gain Table Offset",    "gain_ofs",     -24.0f,    24.0f,  "dB"},
  {paramSharpen,              "Pitch Sharpening",     "sharpen",        0.0f,     1.0f,   ""},
  {paramDecPreEmph,           "Dec Pre-emphasis",     "dec_pre_emph",   0.0f,     0.95f,  ""},
  {paramDecLsfPred,           "Dec LSF Prediction",   "dec_lsf_pred",   0.0f,     0.95f,  ""},
  {paramDecGainOfs,           "Dec Gain Table Offset","dec_gain_ofs", -24.0f,    24.0f,  "dB"},
  {paramDecSharpen,           "Dec Pitch Sharpening", "dec_sharpen",    0.0f,     1.0f,   ""},
  {paramFec,                  "Channel Protection",   "fec",            0.0f,     4.0f,   ""},
  // 0 all, 1 speech data, 2 synth (LPC), 3 pitch, 4 gain, 5 codebook
  {paramCorruptTarget,        "Corruption Target",    "corr_target",    0.0f,     5.0f,   ""},
  {paramGainVq,               "Gain Quantizer",       "gain_vq",        0.0f,     1.0f,   ""},
  {paramDecGainVq,            "Dec Gain Quantizer",   "dec_gain_vq",    0.0f,     1.0f,   ""},
  {paramNetMode,              "Network Mode",         "net_mode",       0.0f,     3.0f,   ""},
  {paramNetChannel,           "Network Channel",      "net_channel",    1.0f,    16.0f,   ""},
  {paramNetLoss,              "Network Loss",         "net_loss",       0.0f,    50.0f,   "%"},
  {paramNetJitter,            "Network Jitter",       "net_jitter",     0.0f,   200.0f,  "ms"},
  {paramLpTaper,              "LP Taper",             "lp_taper",       0.0f,   100.0f,   "%"},
  {paramLpResonance,          "LP Resonance",         "lp_resonance",  25.0f,   175.0f,   "%"},
  {paramLpBand,               "LP Band",              "lp_band",        0.0f,     8.0f,   ""},
  {paramLpMirror,             "LP Mirror",            "lp_mirror",      0.0f,   100.0f,   "%"},
  {paramLpJitter,             "LP Jitter",            "lp_jitter",      0.0f,   100.0f,   "%"},
  {paramDecLpTaper,           "Dec LP Taper",         "dec_lp_taper",   0.0f,   100.0f,   "%"},
  {paramDecLpResonance,       "Dec LP Resonance",     "dec_lp_resonance",25.0f,  175.0f,   "%"},
  {paramDecLpBand,            "Dec LP Band",          "dec_lp_band",    0.0f,     8.0f,   ""},
  {paramDecLpMirror,          "Dec LP Mirror",        "dec_lp_mirror",  0.0f,   100.0f,   "%"},
  {paramDecLpJitter,          "Dec LP Jitter",        "dec_lp_jitter",  0.0f,   100.0f,   "%"}
};

static const int NUM_BANKS = 2;
static const int PRESETS_PER_BANK = 5;

typedef struct {
  const char *name;
  const float params[paramCount];
} Preset;

typedef struct {
  const char *name;
  const Preset presets[PRESETS_PER_BANK];
} Bank;

//  vol  type  rate  cmode cint cmag | stage warp depth order smooth freeze crush | math | pitch voice noise | split drate dmath | dec LP x6 | asplit pre pred gofs sharp | dec x4 | fec target | gainvq decgainvq | net mode ch loss jitter | lattice x5 | dec lattice x5
static const Bank banks[NUM_BANKS] = {
  {
    "TETRA", {
      {"Clean TETRA",             {  80.0,   0.0,  66.7,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,  32.0,   0.0,   0.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,  66.7,   0.0,   0.0,   0.0,  32.0,   0.0,   0.0,   0.0,   0.0,  0.68,   0.6,   0.0,   0.8,  0.68,   0.6,   0.0,   0.8,   2.0,   0.0,   0.0,   0.0,   0.0,   1.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0 }},
      {"Default",                 {  80.0,   0.0,  66.7,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,  32.0,   0.0,   0.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,  66.7,   0.0,   0.0,   0.0,  32.0,   0.0,   0.0,   0.0,   0.0,  0.68,   0.6,   0.0,   0.8,  0.68,   0.6,   0.0,   0.8,   2.0,   0.0,   0.0,   0.0,   0.0,   1.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0 }},
      {"LSP Scramble",            {  80.0,   0.0,  66.7,   1.0,  40.0,  30.0,   0.0,   0.0,   0.0,  32.0,   0.0,   0.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,  66.7,   0.0,   0.0,   0.0,  32.0,   0.0,   0.0,   0.0,   0.0,  0.68,   0.6,   0.0,   0.8,  0.68,   0.6,   0.0,   0.8,   2.0,   0.0,   0.0,   0.0,   0.0,   1.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0 }},
      {"Pitch Chaos",             {  80.0,   0.0,  66.7,   2.0,  50.0,  40.0,   0.0,   0.0,   0.0,  32.0,   0.0,   0.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,  66.7,   0.0,   0.0,   0.0,  32.0,   0.0,   0.0,   0.0,   0.0,  0.68,   0.6,   0.0,   0.8,  0.68,   0.6,   0.0,   0.8,   2.0,   0.0,   0.0,   0.0,   0.0,   1.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0 }},
      {"Burst Errors",            {  80.0,   0.0,  66.7,   8.0,  40.0,  50.0,   0.0,   0.0,   0.0,  32.0,   0.0,   0.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,  66.7,   0.0,   0.0,   0.0,  32.0,   0.0,   0.0,   0.0,   0.0,  0.68,   0.6,   0.0,   0.8,  0.68,   0.6,   0.0,   0.8,   2.0,   0.0,   0.0,   0.0,   0.0,   1.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0 }}
    },
  },
  {
    "HD LP", {
      {"HD 32k Clean",            {  80.0,   2.0,  66.7,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,  32.0,   0.0,   0.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,  66.7,   0.0,   0.0,   0.0,  32.0,   0.0,   0.0,   0.0,   0.0,  0.68,   0.6,   0.0,   0.8,  0.68,   0.6,   0.0,   0.8,   2.0,   0.0,   0.0,   0.0,   0.0,   1.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0 }},
      {"Formant Up",              {  80.0,   2.0,  66.7,   0.0,   0.0,   0.0,   0.0,  60.0,   0.0,  32.0,   0.0,   0.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,  66.7,   0.0,   0.0,   0.0,  32.0,   0.0,   0.0,   0.0,   0.0,  0.68,   0.6,   0.0,   0.8,  0.68,   0.6,   0.0,   0.8,   2.0,   0.0,   0.0,   0.0,   0.0,   1.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0 }},
      {"Whisper",                 {  80.0,   2.0,  66.7,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,  32.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,  66.7,   0.0,   0.0,   0.0,  32.0,   0.0,   0.0,   0.0,   0.0,  0.68,   0.6,   0.0,   0.8,  0.68,   0.6,   0.0,   0.8,   2.0,   0.0,   0.0,   0.0,   0.0,   1.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0 }},
      {"Robot Freeze",            {  80.0,   2.0,  66.7,   0.0,   0.0,   0.0,   0.0,   0.0,  40.0,  32.0,  50.0, 100.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,  66.7,   0.0,   0.0,   0.0,  32.0,   0.0,   0.0,   0.0,   0.0,  0.68,   0.6,   0.0,   0.8,  0.68,   0.6,   0.0,   0.8,   2.0,   0.0,   0.0,   0.0,   0.0,   1.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0 }},
      {"Fighting Codec",          {  80.0,   2.0,  16.7,   0.0,   0.0,   0.0,   1.0,   0.0,   0.0,   4.0,   0.0,   0.0,  40.0,   1.0,   0.0, 100.0,   0.0,   0.0,  16.7,   1.0,   0.0,   0.0,  32.0,   0.0,   0.0,   0.0,   0.0,  0.68,   0.6,   0.0,   0.8,  0.68,   0.6,   0.0,   0.8,   2.0,   0.0,   0.0,   0.0,   0.0,   1.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0,   0.0, 100.0,   0.0,   0.0,   0.0 }}
    },
  }
};

static const int DEFAULT_BANK   = 0; // TETRA
static const int DEFAULT_PRESET = 1; // Default

#endif // DISTRHO_PLUGIN_INFO_H

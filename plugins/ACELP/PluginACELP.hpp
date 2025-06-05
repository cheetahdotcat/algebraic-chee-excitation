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


#ifndef PLUGIN_ACELP_H
#define PLUGIN_ACELP_H

#include "DistrhoPlugin.hpp"
#include "DistrhoPluginInfo.h"
#include "CParamSmooth.hpp"
#include "DSP.hpp"

START_NAMESPACE_DISTRHO

#ifndef MIN
#define MIN(a,b) ( (a) < (b) ? (a) : (b) )
#endif

#ifndef MAX
#define MAX(a,b) ( (a) > (b) ? (a) : (b) )
#endif

#ifndef CLAMP
#define CLAMP(v, min, max) (MIN((max), MAX((min), (v))))
#endif

#ifndef DB_CO
#define DB_CO(g) ((g) > -90.0f ? powf(10.0f, (g) * 0.05f) : 0.0f)
#endif

// -----------------------------------------------------------------------


class PluginACELP : public Plugin, CheetahDSP::Callback {
public:

    PluginACELP();

    ~PluginACELP();

protected:
    // -------------------------------------------------------------------
    // Information

    const char* getLabel() const noexcept override {
        return "Algebraic-Chee-Excitation";
    }

    const char* getDescription() const override {
        return "Algebraic-Chee-Excitation";
    }

    const char* getMaker() const noexcept override {
        return "vst.cheetah.cat";
    }

    const char* getHomePage() const override {
        return "https://vst.cheetah.cat/plugins/acelp";
    }

    const char* getLicense() const noexcept override {
        return "https://spdx.org/licenses/MIT";
    }

    uint32_t getVersion() const noexcept override {
        return d_version(0, 1, 0);
    }

    // Go to:
    //
    // http://service.steinberg.de/databases/plugin.nsf/plugIn
    //
    // Get a proper plugin UID and fill it in here!
    int64_t getUniqueId() const noexcept override {
        return d_cconst('c', 'h', 'e', 'e');
    }

    // -------------------------------------------------------------------
    // Init

    void initParameter(uint32_t index, Parameter& parameter) override;
    void initProgramName(uint32_t index, String& programName) override;

    // -------------------------------------------------------------------
    // Internal data

    float getParameterValue(uint32_t index) const override;
    void setParameterValue(uint32_t index, float value) override;
    void loadProgram(uint32_t index) override;

    // -------------------------------------------------------------------
    // Optional

    // Optional callback to inform the plugin about a sample rate change.
    void sampleRateChanged(double newSampleRate) override;
    /**
      Get the value of an internal state.
      The host may call this function from any non-realtime context.
    */
    // String getState(const char* key) const override;

    /**
      Change an internal state.
    */
    void initState(uint32_t index, State& state) override;
    String getState(const char* key) const override;
    void setState(const char* key, const char* value) override;
    
    // -------------------------------------------------------------------
    // Process

    void deactivate() override;
    void activate() override;
    void run(const float**, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override;

    void onVocoderFrame(Word16 *frame) override;

    // -------------------------------------------------------------------

private:
    int midiEnergyMap[255];

    CheetahDSP dsp;
    int bank;
    int preset;

    char hex_string[128];

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginACELP)
};

// struct Preset {
//     const char* name;
//     float params[PluginACELP::paramCount];
// };

// const Preset factoryPresets[] = {
//     {
//         "Unity Gain",
//         {0.0f}
//     }
//     //,{
//     //    "Another preset",  // preset name
//     //    {-14.0f, ...}      // array of presetCount float param values
//     //}
// };

// const uint presetCount = sizeof(factoryPresets) / sizeof(Preset);

// -----------------------------------------------------------------------

END_NAMESPACE_DISTRHO

#endif  // #ifndef PLUGIN_ACELP_H

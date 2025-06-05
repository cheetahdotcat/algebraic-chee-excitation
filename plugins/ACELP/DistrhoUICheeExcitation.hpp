/*
 * DISTRHO Nekobi Plugin, based on Nekobee by Sean Bolton and others.
 * Copyright (C) 2013-2022 Filipe Coelho <falktx@falktx.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * For a full copy of the GNU General Public License see the LICENSE file.
 */

#ifndef DISTRHO_UI_NEKOBI_HPP_INCLUDED
#define DISTRHO_UI_NEKOBI_HPP_INCLUDED

#include "DistrhoUI.hpp"
#include "debug.h"

#include "NanoVG.hpp"
#include "ImageWidgets.hpp"
#include "../../common/Selection.hpp"
#include "../../common/Spectrogram.hpp"
#include "../../common/Bitstream_Vera_Sans_Regular.hpp"

// #include "NekoWidget.hpp"

using DGL_NAMESPACE::ImageAboutWindow;
using DGL_NAMESPACE::ImageButton;
using DGL_NAMESPACE::ImageKnob;
using DGL_NAMESPACE::ImageSlider;

START_NAMESPACE_DISTRHO

// -----------------------------------------------------------------------

class DistrhoUICheeExcitation : public DISTRHO::UI,
                        public ImageButton::Callback,
                        public ImageKnob::Callback,
                        public ImageSlider::Callback,
                        public Selection::Callback,
                        public IdleCallback
{
public:
    DistrhoUICheeExcitation();
    ~DistrhoUICheeExcitation() override;

protected:
    // -------------------------------------------------------------------
    // DSP Callbacks

    void parameterChanged(uint32_t index, float value) override;
    void stateChanged(const char* key, const char* value) override;
    void programLoaded(uint32_t index) override;
    // -------------------------------------------------------------------
    // Widget Callbacks

    void imageButtonClicked(ImageButton* button, int) override;
    void imageKnobDragStarted(ImageKnob* knob) override;
    void imageKnobDragFinished(ImageKnob* knob) override;
    void imageKnobValueChanged(ImageKnob* knob, float value) override;
    void imageSliderDragStarted(ImageSlider* slider) override;
    void imageSliderDragFinished(ImageSlider* slider) override;
    void imageSliderValueChanged(ImageSlider* slider, float value) override;
    void selectionClicked(Selection* selection, int option) override;

    void onDisplay() override;
    void uiIdle() override;

    // -------------------------------------------------------------------
    // Other Callbacks

    void idleCallback() override;

    void addLogLine(const std::string& line);
private:
#pragma region "UI"
    Image            fImgBackground;
    ImageAboutWindow fAboutWindow;
    // NekoWidget       fNeko;

    ScopedPointer<ImageButton> fButtonAbout;
    ScopedPointer<ImageSlider> fSliderWaveform;
    ScopedPointer<ImageKnob> fKnobCorrMode, fKnobCorrInt, fKnobCorrMag;
    // ScopedPointer<ImageKnob> fKnobCodecBitrate;
    ScopedPointer<ImageKnob> fKnobCodecType, fKnobVolume;
#pragma endregion
#pragma region "UI-2"
    NanoVG nanoText;
    DGL::Rectangle<int> rectDisplay;
    ScopedPointer<Image> spectrogramImage;

    ScopedPointer<Selection> bankSelection;
    ScopedPointer<Selection> presetSelection;
    ScopedPointer<Spectrogram> spectrogram;
    int currentBank;
    int currentProgram[NUM_BANKS];
    void updateBank(int newBank);
    void updatePresetDefaults();
    // // GlFont fontId;
#pragma endregion
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistrhoUICheeExcitation)
};

// -----------------------------------------------------------------------

END_NAMESPACE_DISTRHO

#endif // DISTRHO_UI_NEKOBI_HPP_INCLUDED

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

#include "PluginACELP.hpp"
#include "DistrhoUICheeExcitation.hpp"

#define UI_OFFSET_TYPE_X 108
#define UI_OFFSET_CORR_MAG_X 246
#define UI_OFFSET_CORR_MAG_Y 465
#define UI_OFFSET_CORR_INT_Y 464
#define UI_OFFSET_VOLUME_X 546
#define UI_OFFSET_VOLUME_Y 553
START_NAMESPACE_DISTRHO

namespace Art = CheeExcitationArtwork;

// -----------------------------------------------------------------------

DistrhoUICheeExcitation::DistrhoUICheeExcitation()
    : UI(Art::backgroundWidth, Art::backgroundHeight, true),
      fImgBackground(Art::backgroundData, Art::backgroundWidth, Art::backgroundHeight, kImageFormatBGR),
      fAboutWindow(this)
{
    DEBUG_PRINTF("initializing UI\n");
    NanoVG::FontId font  = nanoText.createFontFromMemory(
        "bitstream vera sans",
        font_bitstream_vera::bitstream_vera_sans_ttf,
        font_bitstream_vera::bitstream_vera_sans_ttf_size,
        false);
    nanoText.fontFaceId(font);
    // about
    Image aboutImage(Art::aboutData, Art::aboutWidth, Art::aboutHeight, kImageFormatBGR);
    fAboutWindow.setImage(aboutImage);

    // // slider
    Image sliderImage(Art::sliderData, Art::sliderWidth, Art::sliderHeight, kImageFormatBGRA);

    fSliderWaveform = new ImageSlider(this, sliderImage);
    fSliderWaveform->setId(CheetahDSP::paramCorruptionIntensity);
    fSliderWaveform->setStartPos(38, 424);
    fSliderWaveform->setEndPos(602, 434);
    fSliderWaveform->setCheckable(true);
    fSliderWaveform->setRange(0.0f, 1.0f);
    fSliderWaveform->setStep(1.0f);
    fSliderWaveform->setValue(0.0f);
    fSliderWaveform->setCallback(this);

    // knobs
    Image knobImage(Art::knobData, Art::knobWidth, Art::knobHeight, kImageFormatBGRA);

    DEBUG_PRINTF("fKnobCorrInt\n");
    fKnobCorrInt = new ImageKnob(this, knobImage, ImageKnob::Vertical);
    fKnobCorrInt->setId(CheetahDSP::paramCorruptionIntensity);
    fKnobCorrInt->setAbsolutePos(UI_OFFSET_VOLUME_X, UI_OFFSET_CORR_INT_Y);
    fKnobCorrInt->setRange(0.0f, 100.0f);
    fKnobCorrInt->setDefault(25.0f);
    fKnobCorrInt->setValue(25.0f);
    fKnobCorrInt->setRotationAngle(305);
    fKnobCorrInt->setCallback(this);

    DEBUG_PRINTF("fKnobCorrMag\n");
    fKnobCorrMag = new ImageKnob(this, knobImage, ImageKnob::Vertical);
    fKnobCorrMag->setId(CheetahDSP::paramCorruptionMagnitude);
    fKnobCorrMag->setAbsolutePos(UI_OFFSET_CORR_MAG_X, UI_OFFSET_CORR_INT_Y);
    fKnobCorrMag->setRange(0.0f, 100.0f);
    fKnobCorrMag->setDefault(25.0f);
    fKnobCorrMag->setValue(25.0f);
    fKnobCorrMag->setRotationAngle(305);
    fKnobCorrMag->setCallback(this);

    // DEBUG_PRINTF("fKnobCodecBitrate\n");
    // fKnobCodecBitrate = new ImageKnob(this, knobImage, ImageKnob::Vertical);
    // fKnobCodecBitrate->setId(PluginACELP::paramCodecBitrate);
    // fKnobCodecBitrate->setAbsolutePos(257, 43);
    // fKnobCodecBitrate->setRange(0.0f, 95.0f);
    // fKnobCodecBitrate->setDefault(25.0f);
    // fKnobCodecBitrate->setValue(25.0f);
    // fKnobCodecBitrate->setRotationAngle(305);
    // fKnobCodecBitrate->setCallback(this);

    DEBUG_PRINTF("fKnobCodecType\n");
    fKnobCodecType = new ImageKnob(this, knobImage, ImageKnob::Vertical);
    fKnobCodecType->setId(CheetahDSP::paramCodecType);
    fKnobCodecType->setAbsolutePos(UI_OFFSET_TYPE_X, UI_OFFSET_VOLUME_Y);
    fKnobCodecType->setRange(0.0f, 1.0f);
    fKnobCodecType->setDefault(0.0f);
    fKnobCodecType->setValue(0.0f);
    fKnobCodecType->setRotationAngle(305);
    fKnobCodecType->setCallback(this);

    fKnobCorrMode = new ImageKnob(this, knobImage, ImageKnob::Vertical);
    fKnobCorrMode->setId(CheetahDSP::paramCorruptionMode);
    fKnobCorrMode->setAbsolutePos(UI_OFFSET_CORR_MAG_X, UI_OFFSET_VOLUME_Y);
    fKnobCorrMode->setRange(0.0f, 2.0f);
    fKnobCorrMode->setDefault(0.0f);
    fKnobCorrMode->setValue(0.0f);
    fKnobCorrMode->setRotationAngle(305);
    fKnobCorrMode->setCallback(this);

    // knob Volume
    fKnobVolume = new ImageKnob(this, knobImage, ImageKnob::Vertical);
    fKnobVolume->setId(CheetahDSP::paramVolume);
    fKnobVolume->setAbsolutePos(UI_OFFSET_VOLUME_X, UI_OFFSET_VOLUME_Y);
    fKnobVolume->setRange(0.0f, 100.0f);
    fKnobVolume->setDefault(75.0f);
    fKnobVolume->setValue(75.0f);
    fKnobVolume->setRotationAngle(305);
    fKnobVolume->setCallback(this);

    DEBUG_PRINTF("aboutImageNormal\n");
    // about button
    Image aboutImageNormal(Art::cheeData, Art::cheeWidth, Art::cheeHeight, kImageFormatBGRA);
    Image aboutImageHover(Art::aboutButtonHoverData, Art::aboutButtonHoverWidth, Art::aboutButtonHoverHeight, kImageFormatBGRA);
    fButtonAbout = new ImageButton(this, aboutImageNormal, aboutImageNormal, aboutImageNormal);
    fButtonAbout->setAbsolutePos(535, 0);
    fButtonAbout->setCallback(this);

    // neko animation
    addIdleCallback(this, 120);

    // Spectrogram
    rectDisplay.setPos  ( 355, 126 );
    rectDisplay.setSize ( 305, 207 );

    AbstractDSP *dsp = new CheetahDSP(SPECTROGRAM_SAMPLE_RATE);
    spectrogram = new Spectrogram(this, &nanoText, &rectDisplay, dsp);
    spectrogram->setAbsolutePos (355, 126);
    DEBUG_PRINTF("done init UI\n");
}

DistrhoUICheeExcitation::~DistrhoUICheeExcitation()
{
    removeIdleCallback(this);
}

// -----------------------------------------------------------------------
// DSP Callbacks

void DistrhoUICheeExcitation::parameterChanged(uint32_t index, float value)
{
    switch (index)
    {
    case CheetahDSP::paramCorruptionMode:
        fKnobCorrMode->setValue(value);
        break;
    case CheetahDSP::paramCorruptionIntensity:
        fKnobCorrInt->setValue(value);
        break;
    case CheetahDSP::paramCorruptionMagnitude:
        fKnobCorrMag->setValue(value);
        break;
    // case PluginACELP::paramCodecBitrate:
        // fKnobCodecBitrate->setValue(value);
        // break;
    case CheetahDSP::paramCodecType:
        fKnobCodecType->setValue(value);
        break;
    case CheetahDSP::paramVolume:
        fKnobVolume->setValue(value);
        break;
    }
}

void DistrhoUICheeExcitation::stateChanged(const char* key, const char* value)
{
  if (std::strcmp(key, "preset") == 0) {
    for (int b = 0; b < NUM_BANKS; b++) {
      for (int p = 0; p < PRESETS_PER_BANK; p++) {
        if (std::strcmp(value, banks[b].presets[p].name) == 0) {
          currentProgram[b] = p;
          updateBank(b);
        }
      }
    }

    updatePresetDefaults();
  }

  repaint();
}

// -----------------------------------------------------------------------
// Widget Callbacks

void DistrhoUICheeExcitation::imageButtonClicked(ImageButton* button, int)
{
    if (button != fButtonAbout)
        return;

    fAboutWindow.runAsModal();
}

void DistrhoUICheeExcitation::imageKnobDragStarted(ImageKnob* knob)
{
    editParameter(knob->getId(), true);
}

void DistrhoUICheeExcitation::imageKnobDragFinished(ImageKnob* knob)
{
    editParameter(knob->getId(), false);
}

void DistrhoUICheeExcitation::imageKnobValueChanged(ImageKnob* knob, float value)
{
    setParameterValue(knob->getId(), value);
}

void DistrhoUICheeExcitation::imageSliderDragStarted(ImageSlider* slider)
{
    editParameter(slider->getId(), true);
}

void DistrhoUICheeExcitation::imageSliderDragFinished(ImageSlider* slider)
{
    editParameter(slider->getId(), false);
}

void DistrhoUICheeExcitation::imageSliderValueChanged(ImageSlider* slider, float value)
{
    setParameterValue(slider->getId(), value);
}

void DistrhoUICheeExcitation::onDisplay()
{
    const GraphicsContext& context(getGraphicsContext());

    fImgBackground.draw(context);
    // fNeko.draw(context);
    // print parameters
    nanoText.beginFrame ( this );
    nanoText.fontSize ( 15 );
    nanoText.textAlign ( NanoVG::ALIGN_CENTER|NanoVG::ALIGN_MIDDLE );

    nanoText.fillColor ( Color ( 255,255, 255) );

    char strBuf[32+1];
    strBuf[32] = '\0';

    //   std::snprintf ( strBuf, 32, "%i%%", int ( sliderDryLevel->getValue() ) );
    //   nanoText.textBox ( 17 - 2, 330, 35.0f, strBuf, nullptr );
    //   std::snprintf ( strBuf, 32, "%i%%", int ( sliderEarlyLevel->getValue() ) );
    //   nanoText.textBox ( 57 - 2, 330, 35.0f, strBuf, nullptr );
    //   std::snprintf ( strBuf, 32, "%i%%", int ( sliderEarlySend->getValue() ) );
    //   nanoText.textBox ( 97 - 2, 330, 35.0f, strBuf, nullptr );
    //   std::snprintf ( strBuf, 32, "%i%%", int ( sliderLateLevel->getValue() ) );
    //   nanoText.textBox (137 - 2, 330, 35.0f, strBuf, nullptr );

    // print labels;
    nanoText.fillColor ( Color ( 0.90f, 0.95f, 1.00f ) );
    nanoText.fontSize ( 14 );
    nanoText.textBox (  10, 130, 40, "Dry\nLevel",   nullptr );
    nanoText.textBox (  50, 130, 40, "Early\nLevel", nullptr );
    nanoText.textBox (  90, 130, 40, "Early\nSend",  nullptr );
    nanoText.textBox ( 130, 130, 40, "Late\nLevel",  nullptr );

    nanoText.endFrame();
    spectrogram->show();
}

void DistrhoUICheeExcitation::uiIdle() {
  spectrogram->uiIdle();
}
// -----------------------------------------------------------------------
// Other Callbacks

void DistrhoUICheeExcitation::idleCallback()
{
    // if (fNeko.idle())
        // repaint();
}
void DistrhoUICheeExcitation::programLoaded(uint32_t index)
{
    // Handle program loading logic here.
    // If you don't use programs, you can leave it empty:
    (void)index;
}
// -----------------------------------------------------------------------

void DistrhoUICheeExcitation::selectionClicked(Selection* selection, int selectedOption) {
  if (selection == bankSelection) {
    updateBank(selectedOption);
  }
  else if (selection == presetSelection) {
    currentProgram[currentBank] = selectedOption;
    presetSelection->setSelectedOption(selectedOption);
  }

  setState("preset", banks[currentBank].presets[currentProgram[currentBank]].name);
  updatePresetDefaults();

  const float *preset = banks[currentBank].presets[currentProgram[currentBank]].params;

  fKnobVolume->setDefault ( preset[paramVolume] );
  fKnobCodecType->setDefault ( preset[paramCodecType] );
  fKnobCorrInt->setDefault ( preset[paramCorruptionIntensity] );
  fKnobCorrMag->setDefault ( preset[paramCorruptionMagnitude] );
  fKnobCorrMode->setDefault ( preset[paramCorruptionMode] );

//   for ( uint32_t i = 0; i < paramCount; i++ ) {
//     // Don't set sliders
//     if (i != paramDry   &&
//         i != paramEarly &&
//         i != paramEarlySend   &&
//         i != paramLate) {
//             setParameterValue ( i, preset[i] );
//             spectrogram->setParameterValue(i, preset[i]);
//     }
//   }

  repaint();
}

void DistrhoUICheeExcitation::updateBank(int newBank) {
  currentBank = newBank;
  bankSelection->setSelectedOption(newBank);
  presetSelection->setSelectedOption(currentProgram[currentBank]);
  for ( int p = 0; p < NUM_BANKS; ++p) {
    presetSelection->setOptionName(p, banks[currentBank].presets[p].name);
  }
}

void DistrhoUICheeExcitation::updatePresetDefaults() {
  const float *preset = banks[currentBank].presets[currentProgram[currentBank]].params;

  fKnobVolume->setDefault ( preset[paramVolume] );
  fKnobCodecType->setDefault ( preset[paramCodecType] );
  fKnobCorrInt->setDefault ( preset[paramCorruptionIntensity] );
  fKnobCorrMag->setDefault ( preset[paramCorruptionMagnitude] );
  fKnobCorrMode->setDefault ( preset[paramCorruptionMode] );
}

UI* createUI()
{
    return new DistrhoUICheeExcitation();
}

// -----------------------------------------------------------------------

END_NAMESPACE_DISTRHO

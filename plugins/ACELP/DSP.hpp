#ifndef CHEETAH_DSP_HPP_INCLUDED
#define CHEETAH_DSP_HPP_INCLUDED

#include "../../common/AbstractDSP.hpp"

#pragma region "Cheetah Header"
// JACK Ringbuffer Copy
#define USE_MLOCK 1
#include <ringbuffer.h>
#include "debug.h"
#include "codec/ener.h"
#include <atomic>
#include <pthread.h>
#include <unistd.h>
#include <speex/speex_resampler.h>
#include <thread>
#define RINGBUFFER_SIZE 8192
#pragma endregion


class CheetahDSP : public AbstractDSP {
public:
  enum Parameters
  {
      paramVolume = 0,
      paramCodecType,
      paramCodecBitrate,
      paramCorruptionMode,
      paramCorruptionIntensity,
      paramCorruptionMagnitude
  };

  CheetahDSP(double sampleRate);
  ~CheetahDSP();
  float getParameterValue(uint32_t index) const;
  void  setParameterValue(uint32_t index, float value);
  void threadFunction();
  void process_buff_ring1_audio(size_t nframes);
  void run(const float** inputs, float** outputs, uint32_t frames);
  void sampleRateChanged(double newSampleRate);
  void mute();

private:
  bool finalized;

  cat_ringbuffer_t *ring_48k_incoming;
  cat_ringbuffer_t *ring2;
  cat_ringbuffer_t *pcm_output_buffer;
  cat_ringbuffer_t *encoded_frame_queue;
  std::thread backgroundThread;
  std::atomic<bool> stopThread;
  std::atomic<bool> threadRunning;
  SpeexResamplerState *resampler_down;
  SpeexResamplerState *resampler_up;
  double          sampleRate;


  float oldParams[paramCount];
  float newParams[paramCount];

  float dryLevel = 0.0;
  float earlyLevel = 0.0;
  float early_send = 0.0;
  float lateLevel = 0.0;

  static const uint32_t BUFFER_SIZE = 256;
  float early_out_buffer[2][BUFFER_SIZE];
  float late_in_buffer[2][BUFFER_SIZE];
  float late_out_buffer[2][BUFFER_SIZE];
};

#endif

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

#include "codec/channel.h"
#include "codec/source.h"
#include "corrupt.hpp"


#ifdef __cplusplus
extern "C" {
#endif
// NOTE: These are the TETRA codec's NATIVE ("SD") frame sizes. The codec in
// codec/*.c is hard-wired to L_frame=240 (8kHz, 30ms speech frames); the glue
// here MUST match it. A previous experiment doubled these to 480/276/864 for a
// 16kHz build, but the codec itself still only fills 240 samples per frame, so
// half of every frame came out silent (frame-rate gating). A future selectable
// 16k/32k "HD" mode should switch these together with an adapted codec path.
#define L_frame 240
#define serial_size 138
#define ana_size 23
#define prm_size 24

#define dual_serial_size 2*serial_size // 276
#define s286_size dual_serial_size + 10 // 286
#define TS7k2_size 432               // TETRA time-slot @7.2kb/s (one channel frame)
#define TimeSlotBufferSize TS7k2_size*4

#define TETRA_SampleRate 8000
#define VST_SampleRate 48000

#define ACELP_DUAL_CHAN_FRAME_SIZE TS7k2_size // 60ms @ 8kHz
#define ACELP_DUAL_CHAN_AUDIO_SIZE L_frame*2 // 8kHz // L_frame*2

#define ACELP_FRAME_SIZE s286_size // 60ms @ 8kHz
#define ACELP_VOCODER_SAMPLE_COUNT s286_size
#define UPSAMPLE_RATIO VST_SampleRate/TETRA_SampleRate
#define DOWNSAMPLE_RATIO UPSAMPLE_RATIO

// How many whole codec frames of slack to keep in the pipeline ring buffers,
// and how many to accumulate in the output ring before playback begins. More
// frames = smoother under jitter but higher latency (each frame is 60ms).
#define PIPELINE_BUFFER_FRAMES 8
#define OUTPUT_PREBUFFER_FRAMES 3
#define UPSAMPLED_FRAME_SIZE (ACELP_FRAME_SIZE * UPSAMPLE_RATIO)

#ifdef __cplusplus
}
#endif




class CheetahDSP : public AbstractDSP {
public:
  class Callback
  {
  public:
      virtual ~Callback() {}
      virtual void onVocoderFrame(Word16 *frame) = 0;
  };
  Callback* callback;
  
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
  void setCallback(Callback* callback) noexcept;

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

  // Output pre-buffering: the decode/upsample runs on a background thread and
  // delivers audio in whole 60ms codec frames, so the output ring must hold a
  // few frames of slack and be filled before playback starts, otherwise normal
  // scheduling jitter underruns the ring and produces clicks/gaps.
  bool outputPrimed = false;


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

  CorruptCfg corruptCfg() const;
};

#endif

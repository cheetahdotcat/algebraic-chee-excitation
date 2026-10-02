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
#include "speex/ace_resampler.h"
#include <thread>
#define RINGBUFFER_SIZE 8192
#pragma endregion

#include "codec/channel.h"
#include "codec/source.h"
#include "corrupt.hpp"
#include "hdacelp/hd_acelp.hpp"
#include "hdacelp/tp_channel.hpp"
#include "net/net_link.hpp"
#include "lpc_viz.hpp"
#include "viz_feed.hpp"


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
  enum Parameters
  {
      paramVolume = 0,
      paramCodecType,
      paramCodecBitrate,
      paramCorruptionMode,
      paramCorruptionIntensity,
      paramCorruptionMagnitude,
      paramLpStage,
      paramLpWarp,
      paramLpDepth,
      paramLpOrder,
      paramLpSmooth,
      paramLpFreeze,
      paramLpCrush,
      paramHdMath,
      paramExcPitch,
      paramExcVoice,
      paramExcNoise,
      paramCodecSplit,
      paramDecBitrate,
      paramDecMath,
      paramDecLpWarp,
      paramDecLpDepth,
      paramDecLpOrder,
      paramDecLpSmooth,
      paramDecLpFreeze,
      paramDecLpCrush,
      paramAlgoSplit,
      paramPreEmph,
      paramLsfPred,
      paramGainOfs,
      paramSharpen,
      paramDecPreEmph,
      paramDecLsfPred,
      paramDecGainOfs,
      paramDecSharpen,
      paramFec,
      paramCorruptTarget,
      paramGainVq,
      paramDecGainVq,
      paramNetMode,
      paramNetChannel,
      paramNetLoss,
      paramNetJitter,
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

  CheetahDSP(double sampleRate);
  ~CheetahDSP();

  float getParameterValue(uint32_t index) const;
  void  setParameterValue(uint32_t index, float value);
  void threadFunction();
  void process_buff_ring1_audio(size_t nframes);
  void run(const float** inputs, float** outputs, uint32_t frames);
  void sampleRateChanged(double newSampleRate);
  void mute();

  // MIDI corruption keys: a chromatic block of 12 notes starting at
  // MIDI_CORRUPT_BASE_NOTE (48 = C3 scientific / "C2" in Ableton & FL).
  //   +0..+9  corruption modes 1..10 (LSP, pitch, codebook, gain, freeze,
  //           bit flips, bit slip, burst, overflow, reinterleave)
  //   +10     momentary LP warp up      (HD only)
  //   +11     momentary LP freeze       (HD only)
  // A key press is a "hit": the corruption depth jumps to velocity/127 and
  // decays (~250 ms) to a sustain level set by aftertouch (40..100 %), then
  // fades out (~120 ms) on release. Called from the audio thread; the
  // envelopes run on the codec thread, once per codec frame.
  static const int MIDI_CORRUPT_BASE_NOTE = 48;
  static const int MIDI_CORRUPT_KEYS = 12;
  void midiCorruptNoteOn(int key, int velocity);
  void midiCorruptNoteOff(int key);
  void setMidiCorruptPressure(int pressure);   // channel / poly aftertouch, 0..127
  void clearMidiCorruptKeys();
  uint16_t heldMidiCorruptKeys() const;        // keys whose envelope is sounding

  // Visualization feeds, read by the UI through DPF direct access.
  LpcViz  lpcViz;   // latest frame: envelope + excitation
  VizFeed vizFeed;  // every frame: history for the scrolling displays + chain

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
  // Knob-driven corruption plus one entry per held MIDI corruption key.
  static const int MAX_CORRUPT_CFGS = 1 + 10;
  int corruptCfgs(CorruptCfg *out);

  // audio thread -> codec thread
  std::atomic<uint8_t>  midiKeyVel[MIDI_CORRUPT_KEYS] = {};   // 0 = released
  std::atomic<uint32_t> midiKeyTrig[MIDI_CORRUPT_KEYS] = {};  // bumps on every note-on
  // codec thread
  struct KeyEnv {
    uint32_t trigSeen;
    float    peak, t, e;
    bool     held, fired;
  };
  KeyEnv keyEnv[MIDI_CORRUPT_KEYS] = {};
  std::atomic<uint16_t> keyActiveMask{0};
public:
  std::atomic<uint8_t>  keyLevel[MIDI_CORRUPT_KEYS] = {};     // envelope 0..255, for the UI
  std::atomic<uint8_t>  midiPressure{0};
private:
  void updateKeyEnvelopes(float dtSeconds);

  // --- HD ACELP path (background thread only) ---------------------------
  // Codec type from the parameters: 0 = ETSI TETRA, 1-3 = HD 16/32/48 kHz,
  // 4/5 = TETRA+ 8/16 kHz (see paramCodecType).
  int  codecTypeForParams() const;
  void hdConfigure(int type);
  bool processHd();
  void hdCloseResamplers();
  hdacelp::Config hdConfigFor(int type, bool decoderSide) const;
  void publishViz(const hdacelp::FrameTrace &t, const hdacelp::Config &c, const hdacelp::Config &cd,
                  const float *in, const float *pcm, bool paramCorr, bool bitCorr,
                  bool lpEnc, bool lpDec, bool excActive, float guardDb);
  void publishTetraViz(const float *pcm, int n);

  // TETRA: encoder-side frame info waiting for its frame to be decoded (the
  // encoded-frame queue is FIFO and drained by the same thread).
  struct TetraPending {
    uint8_t bits[TS7k2_size];
    int     nflipped;
    int     nTargeted;
    float   inDb;
    bool    paramCorr, bitCorr;
  };
  TetraPending tetraPending[PIPELINE_BUFFER_FRAMES];
  int tetraPendHead = 0, tetraPendCount = 0;
  TetraPending tetraCurrent;

  struct HdState {
    hdacelp::Config  cfg;        // encoder side
    hdacelp::Config  decCfg;     // decoder side (differs when split)
    float            guardGain;  // output level guard
    // TETRA+ channel
    hdacelp::Channel chan;
    int              chanMode, chanFec;
    int16_t          sym[hdacelp::TP_MAX_SYMBOLS], symClean[hdacelp::TP_MAX_SYMBOLS];
    int              nsym;
    bool             bfi;
    int16_t          lastGood[hdacelp::HD_MAX_FIELDS];
    bool             haveGood;
    int              nbad;
    // LAN link
    acenet::NetLink  net;
    uint32_t         txSeq;
    int              rxType;        // codec type of the stream being received (0 = none yet)
    acenet::NetFrame jb[32];        // jitter buffer, indexed by seq % 32
    bool             jbValid[32];
    bool             jbStarted;
    uint32_t         jbExpected;
    double           jbLastRx;
    int              jbLost, jbFrames;  // recent window for the loss readout
    float            jbLossPct;
    bool             netConceal;
    hdacelp::Config  rxCfg;         // layout of the stream being received
    bool             haveRxCfg;
    bool             vizChannel;    // this frame went through the T+ channel locally
    int              lastGoodSig;   // layout signature of lastGood
    // bitstream as sent (clean) and as received, for the visualization
    Word16           wordsClean[hdacelp::HD_MAX_WORDS], wordsRx[hdacelp::HD_MAX_WORDS];
    hdacelp::Encoder enc;
    hdacelp::Decoder dec;
    CorruptField     fields[hdacelp::HD_MAX_FIELDS];
    CorruptState     cst;        // freeze + gain-wobble memory
    int              nTargeted;  // last frame's targeted bit flips
    // codec-rate input FIFO (resampled from the host rate)
    float            inFifo[hdacelp::HD_MAX_FRAME * 4];
    int              inFill;
    hdacelp::FrameTrace trace;   // last decoded frame, for the LPC display
    SpeexResamplerState *down;
    SpeexResamplerState *up;
    double           hostRate;
  };
  HdState *hd = nullptr;
  int hdActiveType = 0;          // background thread: codec type hd is set up for
  std::atomic<int> publishedType{-1}; // codec thread -> run(): codec actually running
  // receive side: 0 = nothing to play, 1 = frame in `out`, 2 = lost (conceal)
  int  netPull(acenet::NetFrame& out);
  hdacelp::Config netFrameConfig(const acenet::NetFrame& f) const;
  hdacelp::Config decoderConfigFrom(const hdacelp::Config& src) const;
  void concealOrRemember(Word16* idx, const hdacelp::Config& cd, bool bad);
  int rtCodecType = -1;          // realtime thread: codec seen by run()
};

#endif

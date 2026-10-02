# ACELP

ACELP

## Codecs

The **Type** knob picks the codec:

| Type  | Codec                          | Band   | LPC order | Latency |
|-------|--------------------------------|--------|-----------|---------|
| TETRA | ETSI TETRA ACELP (4.6 kbit/s)  | 8 kHz  | 10        | ~200 ms |
| HD16  | HD ACELP                       | 16 kHz | 16        | ~70 ms  |
| HD32  | HD ACELP                       | 32 kHz | 24        | ~70 ms  |
| HD48  | HD ACELP                       | 48 kHz | 32        | ~70 ms  |
| T+8   | TETRA+ (ETSI-style chain)      | 8 kHz  | 10        | ~90 ms  |
| T+16  | TETRA+ (ETSI-style chain)      | 16 kHz | 16        | ~90 ms  |

HD ACELP (`plugins/ACELP/hdacelp/`) is a floating-point, instance-based
algebraic CELP codec: 20 ms frames, 4 x 5 ms subframes, predictive LSF
quantization, closed-loop pitch, and 16-position interleaved algebraic tracks.
The bitstream runs through the same corruption modes as TETRA.

### TETRA+ (T+8 / T+16)

The ETSI TETRA structure with more resolution, running on the HD ACELP engine
(the TETRA type itself stays the bit-exact ETSI coder): 30 ms frames of four
7.5 ms subframes, interleaved 15-position algebraic tracks, the ETSI pitch
range, and finer LSF / gain quantizers plus 1-3 pulses per track. **Rate**
picks low / mid / high:

| Level | T+8        | T+16        |
|-------|------------|-------------|
| low   | 6.0 kbit/s | 9.9 kbit/s  |
| mid   | 9.1 kbit/s | 15.4 kbit/s |
| high  | 12.1 kbit/s| 21.3 kbit/s |

Frames then go through a TETRA-style protected channel: class 1 bits
(envelope, pitch, gains) get a CRC-8 and a K=5 convolutional code, class 0
bits (pulse positions / signs) a weaker code or none; the result is
interleaved and sent as soft symbols. The receiver runs a soft-decision
Viterbi decoder, and frames whose CRC fails are concealed like ETSI does
(last good parameters with decaying gains, muted after ~8 bad frames).
**FEC** (Corruption group) sets the protection:

| FEC    | class 1        | class 0   | channel size |
|--------|----------------|-----------|--------------|
| OFF    | raw, no CRC    | raw       | x1.0         |
| LIGHT  | rate 3/4 + CRC | raw       | ~x1.1-1.3    |
| ETSI   | rate 2/3 + CRC | raw       | ~x1.15-1.4   |
| STRONG | rate 1/2 + CRC | rate 3/4  | ~x1.6-1.9    |
| MAX    | rate 1/3 + CRC | rate 1/2  | ~x2.3-2.8    |

The bitstream corruption modes hit the channel symbols, so the same bit error
rate sounds very different per FEC level. The chain view shows the code rate,
symbol count and bad-frame / concealment state; the history and frame anatomy
show the channel slot coloured by protection class.

### HD bitrate modes

**Rate** picks one of 7 bitrate modes. The three lowest put only 1, 2 or 4
pulses in each 5 ms subframe and use coarser envelope and gain quantizers; from
mode 3 up every 16-sample track carries 1-4 pulses:

| Mode (Rate)  | Pulses / subframe (16k / 32k / 48k) | HD16      | HD32      | HD48      |
|--------------|-------------------------------------|-----------|-----------|-----------|
| 0 (0%)       | 1                                   | 6.5 kbps  | 7.8 kbps  | 8.8 kbps  |
| 1 (17%)      | 2                                   | 8.5 kbps  | 10.4 kbps | 11.8 kbps |
| 2 (33%)      | 4                                   | 11.0 kbps | 13.3 kbps | 14.7 kbps |
| 3 (50%)      | 5 / 10 / 15                         | 12.2 kbps | 19.1 kbps | 25.9 kbps |
| 4 (67%)      | 10 / 20 / 30                        | 17.2 kbps | 29.1 kbps | 40.9 kbps |
| 5 (83%)      | 15 / 30 / 45                        | 22.2 kbps | 39.1 kbps | 55.9 kbps |
| 6 (100%)     | 20 / 40 / 60                        | 27.2 kbps | 49.1 kbps | 70.9 kbps |

**Math** switches the HD signal path between 32-bit float and 16-bit fixed
point in the style of the ETSI reference codecs: Q13 samples and filter states
(saturating at +-4.0 in the pre-emphasis domain), 16-bit LPC coefficients with a
per-filter block exponent (at most Q12), saturating 32-bit multiply-accumulate,
Q14/Q12 gains and a Q15 de-emphasis. The parameter searches stay floating point.
On loud material the two sound close. Fixed point shows on quiet signals (a
~-78 dBFS noise floor that gates very quiet tails), on resonant LP settings
(coefficient rounding) and as faint limit-cycle ticks in digital silence.
Rate and Math can be changed while playing without resetting the codec.

### Linear prediction modification (HD only)

| Knob   | Effect |
|--------|--------|
| MATH   | HD arithmetic: `FLOAT` or `FIX16` (see above). |
| STAGE  | `DEC`: resynthesize the decoded excitation through the modified envelope (clean formant effect, loudness compensated). `ENC`: transmit the modified envelope; the analysis-by-synthesis loop fights it, producing codec artifacts. |
| WARP   | All-pass frequency warp of the envelope: + shifts formants up, - down. |
| DEPTH  | Formant contrast: -100 flattens (whisper), +100 exaggerates resonances. |
| ORDER  | Effective LPC order via reflection-coefficient truncation; 0 = raw excitation. |
| SMOOTH | Temporal smoothing of the envelope (up to ~1 s). |
| FREEZE | Blend toward the envelope captured when Freeze was engaged. |
| CRUSH  | Snap LSFs to a coarse frequency grid. |

The header switch `ENV | LATTICE` flips the knobs to a second page that
shapes the filter through its reflection coefficients (lattice form):

| Knob   | Effect |
|--------|--------|
| ORDER  | same as on the ENV page: effective LPC order |
| TAPER  | soft order cut: fades the coefficients out towards ORDER instead of a hard cliff (smoother, less detailed envelope) |
| RESON  | 25-175 %: scales the coefficients in log-area-ratio space; below 100 % formants broaden, above they sharpen (always stable) |
| BAND   | removes the lowest 0-8 coefficients: keeps the formant fine structure but drops the coarse spectral tilt |
| MIRROR | blends the envelope towards its mirror image around fs/4 (low formants become high ones) |
| JITTER | random per-frame wobble of the coefficients (shimmer), scaled to the LPC order |

All of them follow STAGE (DEC / ENC / SPLIT, with separate decoder-side
values) and the decoder's loudness compensation, which matches the level of
the de-emphasized output.

Warp/depth are applied to the envelope's power spectrum and refit with
Levinson-Durbin, so every modified filter is stable by construction.

### Corruption

**Mode** (0-10) runs one corruption continuously. **Intensity** is the rate
on a squared curve (share of frames hit for bitstream modes, share of fields
for parameter modes; the readout shows the actual percentage), **Magnitude**
the depth: a logarithmic bit error rate of 0.01 %-5 % for bit flips and
bursts, 1-16 bits for slips, 1-16 words for overflow, 1-32 swaps for shuffle.
Shift-drag or Shift-scroll for fine steps.

| Mode | Domain     | Effect |
|------|------------|--------|
| 1    | parameters | LSP/LSF: envelope indices nudged, formants jump |
| 2    | parameters | Pitch lag nudged: pitch chaos |
| 3    | parameters | Codebook: pulse-position bits flipped, gritty texture |
| 4    | parameters | Gain wobble: a held random gain offset (new step at the Intensity rate, depth = Magnitude, up to ~+-26 dB on the codebook gain plus a voicing shift), ramped across subframes; boosts are capped harder than cuts |
| 5    | parameters | Freeze: repeats the previous frame's parameters (stutter) |
| 6    | bitstream  | Random bit errors at the set bit error rate |
| 7    | bitstream  | Bit slip: whole frame shifted 1-16 bits |
| 8    | bitstream  | Burst errors (Gilbert-Elliott fading channel, same average rate, clustered) |
| 9    | bitstream  | Overflow: words slammed to full scale |
| 10   | bitstream  | Reinterleave: words swapped |

**TARGET** limits the bitstream modes (and the MIDI bitstream keys) to one
kind of codec data instead of the whole transmitted stream:

| Target   | Hits |
|----------|------|
| ALL      | the whole bitstream / channel symbols (default) |
| SPEECH   | all codec parameters, but no channel-coding overhead |
| SYNTH    | the LPC envelope (LSF / LSP) |
| PITCH    | the pitch lag |
| GAIN     | the gains |
| CODEBOOK | the algebraic codebook (pulse positions, signs, TETRA shift) |

Targeted errors land behind the channel coding (TETRA: inside the speech
frames before channel coding; T+: after Viterbi decoding), so FEC cannot hide
them. The chain's BIT CORR block shows how many bits hit the target.

Parameter-domain modes act on the quantized codec parameters before packing,
so the decoder still produces coherent (mangled) speech. Bitstream modes hit
the bits: on TETRA they go through the channel decoder's error correction and
bad-frame concealment (a bit error negates a channel soft symbol), on HD they
reach the decoder unprotected.

### MIDI

Twelve keys starting at MIDI note 48 (C3 in scientific pitch; Ableton and FL
Studio call it C2) trigger corruption on any channel. FREEZE (E3) and ENVFZ (B3) capture the
frame / envelope at the key press and hold it while the key is down, at a
depth set by the velocity (127 = complete hold). For the other keys a press is a hit: the depth jumps to
the velocity and decays (~250 ms) to a sustain level, which aftertouch raises
(40-100 %); release fades out (~120 ms). Bit flips and bursts follow the same
log bit-error-rate scale as the Magnitude knob, a slip fires once per hit.
Keys stack with each other and with the Mode knob. All other notes are
ignored.

| Key | Note | Effect |   | Key | Note | Effect |
|-----|------|--------|---|-----|------|--------|
| C3  | 48 | LSP        | | F#3 | 54 | Bit slip |
| C#3 | 49 | Pitch      | | G3  | 55 | Burst |
| D3  | 50 | Codebook   | | G#3 | 56 | Overflow |
| D#3 | 51 | Gain       | | A3  | 57 | Reinterleave |
| E3  | 52 | Freeze     | | A#3 | 58 | LP warp up (HD only, momentary) |
| F3  | 53 | Bit flips  | | B3  | 59 | LP envelope freeze (HD only, momentary) |

### Encoder / decoder split (HD only)

Encoder and decoder normally share every setting. Three groups can be split so
the decoder deliberately disagrees with the encoder:

* **CODEC** (header `LINK | SPLIT`): decoder-side Math and Rate. A different
  rate makes the decoder parse the bitstream with the wrong bit layout.
* **LP MODIFICATION** (STAGE `SPLIT`): the main knob set runs in the encoder,
  a second set in the decoder.
* **ALGORITHM** (header `LINK | SPLIT`): gain quantizer, pre-emphasis
  coefficient, LSF prediction factor, codebook-gain table offset and
  pitch-sharpening limit.

When a group is split, the header `ENC | DEC` switch selects which side the
knobs edit (decoder-side labels turn amber; right-click learns the side being
edited). The chain view shows both sides and marks mismatched stages amber.
An output level guard keeps a mismatched decoder within +6 dB of the input
level; the OUT block shows its gain reduction while it works.

### Gain quantizer (HD / T+)

**GAIN Q** (ALGORITHM group) selects how the two excitation gains are sent:

* **SCALAR**: a pitch-gain field and a codebook-gain field per subframe.
* **VQ**: one 7-bit index per subframe into a joint codebook, like ETSI
  TETRA's energy VQ: 16 codebook-gain corrections (3 dB steps, -24..+21 dB)
  relative to an MA prediction from the last four subframes, x 8 pitch-gain
  levels. Saves 16 bits per frame for ~1 dB segSNR; a damaged index now jumps
  across codebook entries and lingers through the predictor. Mismatching it
  between encoder and decoder (ALGORITHM split) misparses the frame.

### Network link (LAN, HD / TETRA+)

Instances can stream codec frames to each other over the local network.

| MODE | |
|------|--|
| OFF  | no network |
| SEND | stream the encoded (and corrupted) frames on CHANNEL; you keep hearing the local decode |
| RECV | ignore the input; decode the stream heard on CHANNEL. The codec type, rate, math, gain quantizer and algorithm constants follow the sender; your decoder-side settings (LP STAGE DEC/SPLIT, EXCITATION, CODEC / ALGORITHM split, corruption) apply on top |
| LOOP | send and play back your own stream through the real socket path |

* One UDP datagram per codec frame to multicast group 239.255.76.67, port
  47100 + channel (1-16), TTL 1, so packets never leave the local network.
  No addresses to type: pick the same channel on both sides. A receiver locks
  onto the first stream it hears and takes over another after 1 s of silence.
* The receiver is clocked by its own audio interface. A jitter buffer
  (2 frames + JITTER) absorbs network timing; it grows when frames arrive
  late, drops a frame when it runs long, and lost or late frames are
  concealed (last parameters, decaying gains). The panel shows stream id,
  codec, buffer depth and recent loss.
* **LOSS** and **JITTER** simulate a bad network on the receiving side
  (random loss, uniform delay with reordering) — useful in LOOP on one
  machine.
* Allow UDP ports 47100-47116 through the receiving machine's firewall.
  The ETSI TETRA codec is not networked (use T+8 / T+16). Networking is
  implemented for Linux/POSIX; other platforms build without it.
* `ACE_NET_TTL=0` in the environment keeps all traffic on the local host.

### Reset

**RESET** (header) sets every parameter back to its default after a second
confirming click within 3 s. MIDI CC mappings are kept.

### Excitation (HD only)

Decoder-side changes to the decoded excitation (the decoder's pitch memory
drifts away from the encoder's, which is part of the effect):

| Knob    | Effect |
|---------|--------|
| PITCH   | +-12 semitones: rescales the decoded pitch lag (CELP pitch shift) |
| VOICING | pitch-gain scale: 0 % = noise only (whisper), 200 % = buzzy |
| NOISE   | algebraic-codebook gain, -30..+12 dB |

### Interface

* **Encode / decode chain**: every stage of the active codec with a live value
  (levels, LPC order, LSF bits, pitch, pulses, gains, frame size, flipped bits).
  LP MOD lights up at the encoder or decoder position, corruption stages turn
  red while they damage the stream.
* **History** (last 7 s, 100 px per second for every codec):
  envelope spectrogram |1/A(z)| on a fixed 50 Hz-24 kHz log axis (hatched above
  the codec's Nyquist), pitch and codebook gain, algebraic pulse positions, and
  the bitstream map: every frame's bits coloured by meaning (envelope, pitch,
  gain, pulse position, sign, FEC), corrupted bits in red.
* **Current frame**: envelope with LSF ticks (amber = after LP modification),
  excitation (pitch contribution + pulses), and the frame anatomy bit grid.

For TETRA (ETSI) the envelope, pitch and pulses are re-estimated from the decoded
audio (marked "est."), since the fixed-point ETSI decoder keeps them in
internal state; the bitstream view shows the real 432-bit channel-coded slot.

Controls: drag knobs vertically (Shift = fine), mouse wheel (Shift = fine),
double-click resets. ORDER covers the active codec's order (16/24/32).

### MIDI learn

Right-click any control, then move a CC: the control follows that CC from now
on (shown as "CC n" under it). Right-click a mapped control to unmap it,
right-click an armed one to cancel. Mappings are saved with the session.
CC 120/123 (all sound/notes off) are reserved.

### Codec test tool

```con
cd plugins/ACELP/hdacelp
g++ -O2 -o hdacelp_cli tools/hdacelp_cli.cpp hd_acelp.cpp hd_lpc.cpp
ffmpeg -i speech.wav -f f32le -ac 1 -ar 32000 in.f32
./hdacelp_cli in.f32 out.f32 32000 4                       # mode 4, float
./hdacelp_cli in.f32 out.f32 32000 0 1                     # mode 0, fixed point
./hdacelp_cli in.f32 out.f32 32000 4 0 0 0.6 0 24 0 0 0    # math stage warp depth order smooth freeze crush
ffmpeg -f f32le -ar 32000 -ac 1 -i out.f32 out.wav
```

## Formats

All plugins in this collection come in the following plug-in formats:

* [LV2]
* [VST2]
* [VST3]
* [CLAP]

## Compiling

Make sure you have installed the required build tools and libraries (see
section "Prerequisites" below) and then clone this repository (including
sub-modules) and simply run `make` in the project's root directory:

```con
git clone --recursive https://github.com:cheetahdotcat/acelp
cd acelp
make
```

`make DEBUG=true` builds unoptimized with debug symbols.

### macOS

The GitHub Actions workflow (`.github/workflows/build.yml`) builds universal
(x86_64 + arm64, macOS 10.15+) VST2 / VST3 bundles on a macOS runner with
`make macos-universal-10.15`, ad-hoc signs them and uploads them as an
artifact (a Linux build runs alongside). Ad-hoc signed plugins load on your
own Macs; if a downloaded copy is blocked, run
`xattr -dr com.apple.quarantine <plugin>`. Public distribution needs a
Developer ID signature and notarization.

On a Mac you can also build locally: install the Xcode command line tools and
run `make macos-universal-10.15`.

## Installation

To install all plugin formats to their appropriate system-wide location, run
the following command (root priviledges may be required):

```con
make install
```

The makefiles support the usual `PREFIX` and `DESTDIR` variables to change the
installation prefix and set an installation root directory (defaulty: empty).
`PREFIX` defaults to `/usr/local`, but on macOS and Windows it is not used,
since the system-wide installation directories for plugins are fixed.

Use make's `-n` option to see where the plugins would be installed without
actually installing them.

You can also set the installation directory for each plugin format with a
dedicated makefile variable.

* LV2: `LV2_DIR` (`<prefix>/lib/lv2`)
* VST2: `VST2_DIR` (`<prefix>/lib/vst`)
* VST3: `VST3_DIR` (`<prefix>/lib/vst3`)
* CLAP: `CLAP_DIR` (`<prefix>/lib/clap`)

Example:

```con
make DESTDIR=/tmp/build-root PREFIX=/usr VST2_DIR=/usr/lib/lxvst install
```

To install the plugins only for your current user account, run:

```con
make install-user
```

Again, you can also set the installation directory for each plugin format with
a dedicated makefile variable.

* LV2: `USER_LV2_DIR` (`$HOME/.lv2`)
* VST2: `USER_VST2_DIR` (`$HOME/.vst`)
* VST3: `USER_VST3_DIR` (`$HOME/.vst3`)
* CLAP: `USER_CLAP_DIR` (`$HOME/.clap`)

*Note: The given default values for all of the above listed environment
variables differ depending on the target OS.*


## Prerequisites

* The GCC C++ compiler, library and the usual associated software build tools
  (GNU `make`, etc.).

  Debian / Ubuntu users should install the `build-essential` package
  to get these, Arch users the `base-devel` package group.

* [pkgconf]

The speexdsp resampler is vendored (`plugins/ACELP/speex/`, BSD license), so
no system libspeexdsp is needed. Linux builds need the usual DPF dependencies
(OpenGL, X11 headers; JACK for the standalone).

The [LV2] and [VST2] (vestige) headers are included in the
[DPF] framework, which is integrated as a Git sub-module. These need not be
installed separately to build the software in the respective plug-in formats.


## Author

This software was created by *Cheetah van Oranje*.


## Acknowledgements

This project is built using the DISTRHO Plugin Framework ([DPF]) and set up
with the [cookiecutter-dpf-effect] project template.


[cookiecutter-dpf-effect]: https://github.com/SpotlightKid/cookiecutter-dpf-effect
[DPF]: https://github.com/DISTRHO/DPF
[LV2]: http://lv2plug.in/
[pkgconf]: https://github.com/pkgconf/pkgconf
[VST2/3]: https://en.wikipedia.org/wiki/Virtual_Studio_Technology
[CLAP]: https://cleveraudio.org/

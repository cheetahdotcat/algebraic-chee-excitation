// Standalone round-trip tool for the HD ACELP codec.
//
//   hdacelp_cli in.f32 out.f32 fs mode [math [stage warp depth order smooth freeze crush]]
//
// in/out are raw mono float32 at `fs`. mode: bitrate mode 0..6. math: 0 =
// float, 1 = fixed-point 16-bit. stage: 0 = decoder-side LP mod, 1 = encoder-side. Prints bitrate, realtime factor and the decoder vs. encoder
// local-synthesis mismatch (must be 0 without modification).
#include "../hd_acelp.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace hdacelp;

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s in.f32 out.f32 fs mode [math [stage warp depth order smooth freeze crush]]\n", argv[0]);
        return 1;
    }
    FILE *fi = fopen(argv[1], "rb");
    if (!fi) { perror(argv[1]); return 1; }
    std::vector<float> in;
    float buf[4096];
    size_t n;
    while ((n = fread(buf, sizeof(float), 4096, fi)) > 0) in.insert(in.end(), buf, buf + n);
    fclose(fi);

    // fs "t8" / "t16" selects the TETRA-style profile at 8 / 16 kHz
    const int math = argc >= 6 ? atoi(argv[5]) : MATH_FLOAT;
    Config c = argv[3][0] == 't' ? Config::makeTetraPlus(atoi(argv[3] + 1) * 1000, atoi(argv[4]), math)
                                       : Config::make(atoi(argv[3]), atoi(argv[4]), math);
    if (getenv("GAINVQ")) c.gainVq = true; // indexed joint gain codebook
    LpMod mod;
    int stage = 0;
    if (argc >= 13) {
        stage = atoi(argv[6]);
        mod.warp = atof(argv[7]);
        mod.depth = atof(argv[8]);
        mod.order = atof(argv[9]);
        mod.smooth = atof(argv[10]);
        mod.freeze = atof(argv[11]);
        mod.crush = atof(argv[12]);
    }
    printf("profile=%s fs=%d order=%d mode=%d math=%s pulses/sub=%d %sbits/frame=%d bitrate=%.1f kbps\n", c.profile ? "T+" : "HD", c.fs, c.order,
           c.mode, c.math ? "fixed16" : "float", c.tracks * c.pulsesPerTrack, c.gainVq ? "gainVQ " : "", c.numBits(), c.bitrate() / 1000.0);

    static Encoder enc;
    static Decoder dec;
    enc.init(c);
    dec.init(c);
    if (stage == 1) enc.setLpMod(mod);

    const int N = c.frameLen;
    const size_t frames = in.size() / N;
    std::vector<float> out(frames * N), loc(frames * N);
    int16_t idx[HD_MAX_FIELDS], idx2[HD_MAX_FIELDS], words[HD_MAX_WORDS];
    double maxDiff = 0.0;
    auto t0 = std::chrono::steady_clock::now();
    for (size_t f = 0; f < frames; f++) {
        enc.encode(&in[f * N], idx, &loc[f * N]);
        pack_words(c, idx, words);
        unpack_words(c, words, idx2);
        for (int i = 0; i < c.numFields(); i++)
            if (idx[i] != idx2[i]) { fprintf(stderr, "pack mismatch frame %zu field %d\n", f, i); return 2; }
        dec.decode(idx2, &out[f * N], stage == 0 ? &mod : nullptr);
        for (int i = 0; i < N; i++) {
            const double d = fabs(out[f * N + i] - loc[f * N + i]);
            if (d > maxDiff) maxDiff = d;
            if (!std::isfinite(out[f * N + i])) { fprintf(stderr, "non-finite output frame %zu\n", f); return 3; }
        }
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("frames=%zu audio=%.2fs cpu=%.3fs realtime x%.1f  max|dec-encsynth|=%g\n", frames,
           frames * N / (double)c.fs, secs, frames * N / (double)c.fs / secs, maxDiff);

    FILE *fo = fopen(argv[2], "wb");
    fwrite(out.data(), sizeof(float), out.size(), fo);
    fclose(fo);
    return 0;
}

/*
 * Algebraic Chee Excitation — plugin UI (see DistrhoUICheeExcitation.hpp).
 */

#include "PluginACELP.hpp"
#include "DistrhoUICheeExcitation.hpp"
#include "CheeExcitationArtwork.hpp"
#include "../../common/Bitstream_Vera_Sans_Regular.hpp"
#include "hdacelp/hd_acelp.hpp"
#include "corrupt.hpp"
#include "hdacelp/tp_channel.hpp"

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>

// nanovg C API (part of libdgl): stream new pixels into an existing image.
extern "C" void nvgUpdateImage(NVGcontext* ctx, int image, const unsigned char* data);

START_NAMESPACE_DISTRHO

namespace Art = CheeExcitationArtwork;

namespace {

// ---- layout ------------------------------------------------------------------
const float UI_W = DISTRHO_UI_DEFAULT_WIDTH;
const float UI_H = DISTRHO_UI_DEFAULT_HEIGHT;
const float M = 12.0f;                       // outer margin

const float CHAIN_Y = 50.0f, CHAIN_H = 100.0f;

const float LANE_X = M, LANE_W = 700.0f;
const float SPEC_Y = 158.0f,  SPEC_H = 160.0f;
const float PITCH_Y = 324.0f, PITCH_H = 60.0f;
const float PULSE_Y = 390.0f, PULSE_H = 60.0f;
const float BITS_Y = 456.0f,  BITS_H = 104.0f;

const float SIDE_X = 720.0f, SIDE_W = UI_W - SIDE_X - M;
const float ENV_Y = 158.0f,  ENV_H = 160.0f;
const float EXC_Y = 324.0f,  EXC_H = 100.0f;
const float ANAT_Y = 430.0f, ANAT_H = 130.0f;

const float CTRL_Y = 568.0f, CTRL_H = 140.0f;   // first control row
const float RESET_X = UI_W - 112.0f, RESET_W = 74.0f;
const float CTRL2_Y = 716.0f;                     // second control row

// Control group panels: x, y, w, h (see the kGroup* order)
const float GROUP_RECT[7][4] = {
    {  12, CTRL_Y,  250, CTRL_H },   // codec
    { 270, CTRL_Y,  330, CTRL_H },   // corruption (+ target, channel protection)
    { 608, CTRL_Y,  480, CTRL_H },   // LP modification
    {  12, CTRL2_Y, 200, CTRL_H },   // excitation
    { 220, CTRL2_Y, 370, CTRL_H },   // algorithm
    { 598, CTRL2_Y, 290, CTRL_H },   // network
    { 896, CTRL2_Y, 192, CTRL_H },   // MIDI
};
const float PX_PER_SECOND = 100.0f;          // history scroll speed (all codecs)

// ---- colours -----------------------------------------------------------------
struct RGB { int r, g, b; };
const RGB C_BG     = {  6, 13,  9 };
const RGB C_PANEL  = { 11, 23, 17 };
const RGB C_PANEL2 = { 15, 31, 22 };
const RGB C_BORDER = { 30, 58, 42 };
const RGB C_TEXT   = { 207, 233, 214 };
const RGB C_MUTED  = { 111, 154, 128 };
const RGB C_GREEN  = {  70, 235, 120 };
const RGB C_AMBER  = { 255, 170,  40 };
const RGB C_RED    = { 255,  74,  58 };
const RGB C_CYAN   = {  76, 195, 255 };
const RGB C_VIOLET = { 180, 140, 255 };

const RGB C_TEAL   = {  90, 220, 200 };
const RGB GROUP_COL[7] = { C_GREEN, C_RED, C_AMBER, C_CYAN, C_VIOLET, C_TEAL, C_MUTED };
const char* const GROUP_NAME[7] = { "CODEC", "CORRUPTION", "LP MODIFICATION", "EXCITATION", "ALGORITHM", "NETWORK", "MIDI" };
const char* const NET_NAMES[4] = { "OFF", "SEND", "RECV", "LOOP" };
const char* const PAGE_NAMES[2] = { "ENV", "LATTICE" };

const RGB BIT_COL[kVizBitCount] = {
    {  70, 235, 120 },  // LSF
    {  76, 195, 255 },  // pitch
    { 180, 140, 255 },  // pulse position
    { 120,  95, 190 },  // sign
    { 150, 150, 150 },  // shift
    { 255, 170,  40 },  // gain
    {  63, 181, 157 },  // FEC class 1
    {  40, 120, 105 },  // FEC class 0
};
const char* const BIT_NAME[kVizBitCount] = { "envelope", "pitch", "pulse pos", "sign", "shift", "gain", "FEC cl.1", "FEC cl.0" };

// note: DGL Color takes alpha as 0..1
inline Color col(const RGB& c, int a = 255) { return Color(c.r, c.g, c.b, a / 255.0f); }

const char* const TYPE_NAMES[6]  = { "TETRA", "HD16", "HD32", "HD48", "T+8", "T+16" };
const int         TYPE_RATES[6]  = { 8000, 16000, 32000, 48000, 8000, 16000 };
const char* const FEC_NAMES[5]   = { "OFF", "LIGHT", "ETSI", "STRONG", "MAX" };
const char* const TARGET_NAMES[6] = { "ALL", "SPEECH", "SYNTH", "PITCH", "GAIN", "CODEBOOK" };
const char* const MATH_NAMES[2]  = { "FLOAT", "FIX16" };
const char* const STAGE_NAMES[3] = { "DEC", "ENC", "SPLIT" };
const char* const LINK_NAMES[2]  = { "LINK", "SPLIT" };
const char* const SIDE_NAMES[2]  = { "ENC", "DEC" };
const char* const GAINQ_NAMES[2] = { "SCALAR", "VQ" };
const char* const KEY_NAMES[12]  = { "LSP", "PITCH", "CB", "GAIN", "FRZ", "FLIP",
                                     "SLIP", "BURST", "OVFL", "SHUF", "WARP", "ENVFZ" };
const char* const MODE_NAMES[11] = { "OFF", "LSP", "PITCH", "CODEBOOK", "GAIN", "FREEZE",
                                     "BITFLIP", "SLIP", "BURST", "OVERFLOW", "SHUFFLE" };

// Phosphor colour map for the envelope spectrogram, t in 0..1.
void cmap(float t, unsigned char* px) {
    static const float stops[][4] = {
        { 0.00f,   4,  12,   8 }, { 0.30f,  12,  60,  34 }, { 0.55f,  40, 170,  85 },
        { 0.75f, 190, 225,  80 }, { 0.90f, 255, 220, 120 }, { 1.00f, 255, 250, 230 },
    };
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    int i = 0;
    while (i < 4 && t > stops[i + 1][0]) ++i;
    const float u = (t - stops[i][0]) / (stops[i + 1][0] - stops[i][0]);
    for (int k = 0; k < 3; ++k) px[k] = (unsigned char)(stops[i][k + 1] + u * (stops[i + 1][k + 1] - stops[i][k + 1]));
    px[3] = 255;
}

inline void putpx(unsigned char* p, int r, int g, int b) { p[0] = (unsigned char)r; p[1] = (unsigned char)g; p[2] = (unsigned char)b; p[3] = 255; }

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int typeIndex(float v) { return clampi((int)std::lround(v), 0, 5); }
bool isTetraPlus(int t) { return t >= 4; }
// Rate knob -> HD bitrate mode or TETRA+ level.
int rateMode(int t, float v) {
    const int n = isTetraPlus(t) ? hdacelp::TP_NUM_LEVELS : hdacelp::HD_NUM_MODES;
    return clampi((int)std::lround(v / 100.0f * (n - 1)), 0, n - 1);
}
hdacelp::Config uiConfig(int t, int mode) {
    return isTetraPlus(t) ? hdacelp::Config::makeTetraPlus(TYPE_RATES[t], mode)
                          : hdacelp::Config::make(TYPE_RATES[t], mode);
}
// channel symbols per frame for a TETRA+ config and protection level
int channelSymbols(const hdacelp::Config& c, int fec) {
    static hdacelp::Channel ch;
    ch.configure(c, fec);
    return ch.symbols();
}

} // namespace

// -----------------------------------------------------------------------

DistrhoUICheeExcitation::DistrhoUICheeExcitation()
    : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT, true),
      fDrag(-1), fDragStartY(0.0f), fDragStartValue(0.0f),
      fLastClickControl(-1), fLastClickTime(0),
      fFeedNext(0), fHaveRec(false), fHaveFrame(false),
      fEditSide{0, 0, 0},
      fResetArmedUntil(0.0),
      fLpPage(0),
      fBuildPage(0),
      fTime(0.0),
      fAboutWindow(this)
{
    NanoVG::FontId font = fNvg.createFontFromMemory("vera", font_bitstream_vera::bitstream_vera_sans_ttf,
                                                    font_bitstream_vera::bitstream_vera_sans_ttf_size, false);
    fNvg.fontFaceId(font);

    Image aboutImage(Art::aboutData, Art::aboutWidth, Art::aboutHeight, kImageFormatBGR);
    fAboutWindow.setImage(aboutImage);

    std::memset(&fLast, 0, sizeof(fLast));
    std::memset(&fFrame, 0, sizeof(fFrame));
    for (int i = 0; i < paramCount; ++i)
        fValues[i] = banks[DEFAULT_BANK].presets[DEFAULT_PRESET].params[i];

    buildControls();
    initLane(fLaneSpec, (int)LANE_W, (int)SPEC_H);
    initLane(fLanePitch, (int)LANE_W, (int)PITCH_H);
    initLane(fLanePulse, (int)LANE_W, (int)PULSE_H);
    initLane(fLaneBits, (int)LANE_W, (int)BITS_H);

    // Start reading the feed from "now" rather than replaying old frames.
    if (PluginACELP* p = plugin())
        fFeedNext = p->getDSP().vizFeed.count.load(std::memory_order_acquire);

    addIdleCallback(this, 33); // ~30 fps
}

DistrhoUICheeExcitation::~DistrhoUICheeExcitation()
{
    removeIdleCallback(this);
}

PluginACELP* DistrhoUICheeExcitation::plugin() const
{
    return static_cast<PluginACELP*>(getPluginInstancePointer());
}

// -----------------------------------------------------------------------
// Controls

void DistrhoUICheeExcitation::resetToDefaults()
{
    // every parameter back to the default preset; MIDI CC mappings are kept
    for (uint32_t p = 0; p < (uint32_t)paramCount; ++p) {
        const float v = banks[DEFAULT_BANK].presets[DEFAULT_PRESET].params[p];
        if (v == fValues[p]) continue;
        fValues[p] = v;
        editParameter(p, true);
        setParameterValue(p, v);
        editParameter(p, false);
    }
    fEditSide[0] = fEditSide[1] = fEditSide[2] = 0;
    fLpPage = 0;
}

void DistrhoUICheeExcitation::buildControls()
{
    auto add = [this](uint32_t p, uint32_t pd, const char* label, ControlKind kind, int group,
                      float x, float y, float w, float h, int n = 0, const char* const* names = nullptr,
                      int cols = 1, bool bipolar = false, bool mini = false) {
        Control c = {};
        c.param = p; c.paramDec = pd; c.label = label; c.kind = kind; c.group = group;
        c.x = x; c.y = y; c.w = w; c.h = h; c.nseg = n; c.segNames = names; c.segCols = cols;
        c.bipolar = bipolar; c.mini = mini; c.page = fBuildPage;
        fControls.push_back(c);
    };
    auto knob = [&](uint32_t p, uint32_t pd, const char* label, int group, float x, bool bipolar = false) {
        add(p, pd, label, kKnob, group, x, GROUP_RECT[group][1] + 18, 58, 112, 0, nullptr, 1, bipolar);
    };
    auto seg = [&](uint32_t p, uint32_t pd, const char* label, int group, float x, float w, int n,
                   const char* const* names, int cols) {
        add(p, pd, label, kSegment, group, x, GROUP_RECT[group][1] + 18, w, 112, n, names, cols);
    };
    auto mini = [&](uint32_t p, int group, float x, float w, int n, const char* const* names) {
        add(p, kNoParam, "", kSegment, group, x, GROUP_RECT[group][1] + 3, w, 14, n, names, n, false, true);
    };

    // CODEC
    seg(paramCodecType, kNoParam, "TYPE", kGroupCodec, 20, 126, 6, TYPE_NAMES, 3);
    seg(paramHdMath, paramDecMath, "MATH", kGroupCodec, 150, 52, 2, MATH_NAMES, 1);
    knob(paramCodecBitrate, paramDecBitrate, "RATE", kGroupCodec, 204);
    mini(paramCodecSplit, kGroupCodec, 120, 66, 2, LINK_NAMES);
    mini(kEditCodec, kGroupCodec, 190, 66, 2, SIDE_NAMES);
    // CORRUPTION
    knob(paramCorruptionMode, kNoParam, "MODE", kGroupCorrupt, 278);
    knob(paramCorruptionIntensity, kNoParam, "INTENSITY", kGroupCorrupt, 340);
    knob(paramCorruptionMagnitude, kNoParam, "MAGNITUDE", kGroupCorrupt, 402);
    knob(paramCorruptTarget, kNoParam, "TARGET", kGroupCorrupt, 464);
    knob(paramFec, kNoParam, "FEC", kGroupCorrupt, 532);
    // LP MODIFICATION
    seg(paramLpStage, kNoParam, "STAGE", kGroupLp, 618, 52, 3, STAGE_NAMES, 1);
    mini(kPageLp, kGroupLp, 880, 110, 2, PAGE_NAMES);
    fBuildPage = 1; // ENV page
    knob(paramLpWarp,   paramDecLpWarp,   "WARP",   kGroupLp, 676, true);
    knob(paramLpDepth,  paramDecLpDepth,  "DEPTH",  kGroupLp, 743, true);
    knob(paramLpOrder,  paramDecLpOrder,  "ORDER",  kGroupLp, 810);
    knob(paramLpSmooth, paramDecLpSmooth, "SMOOTH", kGroupLp, 877);
    knob(paramLpFreeze, paramDecLpFreeze, "FREEZE", kGroupLp, 944);
    knob(paramLpCrush,  paramDecLpCrush,  "CRUSH",  kGroupLp, 1011);
    fBuildPage = 2; // LATTICE page (reflection-coefficient shaping)
    knob(paramLpOrder,     paramDecLpOrder,     "ORDER",  kGroupLp, 676);
    knob(paramLpTaper,     paramDecLpTaper,     "TAPER",  kGroupLp, 743);
    knob(paramLpResonance, paramDecLpResonance, "RESON",  kGroupLp, 810, true);
    knob(paramLpBand,      paramDecLpBand,      "BAND",   kGroupLp, 877);
    knob(paramLpMirror,    paramDecLpMirror,    "MIRROR", kGroupLp, 944);
    knob(paramLpJitter,    paramDecLpJitter,    "JITTER", kGroupLp, 1011);
    fBuildPage = 0;
    mini(kEditLp, kGroupLp, 1012, 70, 2, SIDE_NAMES);
    // EXCITATION
    knob(paramExcPitch, kNoParam, "PITCH",   kGroupExc, 20, true);
    knob(paramExcVoice, kNoParam, "VOICING", kGroupExc, 82);
    knob(paramExcNoise, kNoParam, "NOISE",   kGroupExc, 144, true);
    // ALGORITHM
    seg(paramGainVq, paramDecGainVq, "GAIN Q", kGroupAlgo, 230, 60, 2, GAINQ_NAMES, 1);
    knob(paramPreEmph, paramDecPreEmph, "PRE-EMPH", kGroupAlgo, 298);
    knob(paramLsfPred, paramDecLsfPred, "LSF PRED", kGroupAlgo, 368);
    knob(paramGainOfs, paramDecGainOfs, "GAIN OFS", kGroupAlgo, 438, true);
    knob(paramSharpen, paramDecSharpen, "SHARPEN",  kGroupAlgo, 508);
    mini(paramAlgoSplit, kGroupAlgo, 446, 66, 2, LINK_NAMES);
    mini(kEditAlgo, kGroupAlgo, 516, 66, 2, SIDE_NAMES);
    // NETWORK
    seg(paramNetMode, kNoParam, "MODE", kGroupNet, 606, 96, 4, NET_NAMES, 2);
    knob(paramNetChannel, kNoParam, "CHANNEL", kGroupNet, 708);
    knob(paramNetLoss, kNoParam, "LOSS", kGroupNet, 766);
    knob(paramNetJitter, kNoParam, "JITTER", kGroupNet, 824);
}

bool DistrhoUICheeExcitation::groupSplit(int group) const
{
    switch (group) {
    case kGroupCodec: return fValues[paramCodecSplit] >= 0.5f;
    case kGroupLp:    return std::lround(fValues[paramLpStage]) == 2;
    case kGroupAlgo:  return fValues[paramAlgoSplit] >= 0.5f;
    default:          return false;
    }
}

bool DistrhoUICheeExcitation::controlVisible(const Control& c) const
{
    if (c.page != 0 && c.page != fLpPage + 1) return false;
    if (c.param >= kEditCodec && c.param <= kEditAlgo) return groupSplit(c.group);
    return true;
}

uint32_t DistrhoUICheeExcitation::effParam(const Control& c) const
{
    if (c.paramDec == kNoParam || !groupSplit(c.group)) return c.param;
    const int side = c.group == kGroupCodec ? fEditSide[0] : (c.group == kGroupLp ? fEditSide[1] : fEditSide[2]);
    return side ? c.paramDec : c.param;
}

float DistrhoUICheeExcitation::getVal(uint32_t p) const
{
    if (p == kPageLp) return (float)fLpPage;
    if (p >= kEditCodec && p <= kEditAlgo) return (float)fEditSide[p - kEditCodec];
    return p < (uint32_t)paramCount ? fValues[p] : 0.0f;
}

void DistrhoUICheeExcitation::segRect(const Control& c, float& x, float& y, float& w, float& h) const
{
    x = c.x; w = c.w;
    if (c.mini) { y = c.y; h = c.h; }
    else        { y = c.y + 12; h = 64; }
}

namespace {
// Decoder-side parameters are formatted like their encoder counterparts.
uint32_t baseParam(uint32_t p)
{
    switch (p) {
    case paramDecBitrate:  return paramCodecBitrate;
    case paramDecMath:     return paramHdMath;
    case paramDecLpWarp:   return paramLpWarp;
    case paramDecLpDepth:  return paramLpDepth;
    case paramDecLpOrder:  return paramLpOrder;
    case paramDecLpSmooth: return paramLpSmooth;
    case paramDecLpFreeze: return paramLpFreeze;
    case paramDecLpCrush:  return paramLpCrush;
    case paramDecPreEmph:  return paramPreEmph;
    case paramDecLsfPred:  return paramLsfPred;
    case paramDecGainOfs:  return paramGainOfs;
    case paramDecSharpen:  return paramSharpen;
    case paramDecGainVq:   return paramGainVq;
    case paramDecLpTaper:     return paramLpTaper;
    case paramDecLpResonance: return paramLpResonance;
    case paramDecLpBand:      return paramLpBand;
    case paramDecLpMirror:    return paramLpMirror;
    case paramDecLpJitter:    return paramLpJitter;
    default:               return p;
    }
}
}

void DistrhoUICheeExcitation::controlRange(const Control& c, float& lo, float& hi) const
{
    const uint32_t p = effParam(c);
    if (p >= (uint32_t)paramCount) { lo = 0; hi = (float)(c.nseg - 1); return; }
    lo = PARAMS[p].range_min;
    hi = PARAMS[p].range_max;
    if (baseParam(p) == paramLpOrder) {
        // Only the current codec's real order range, so the whole knob travel
        // is useful (16 / 24 / 32).
        const int t = typeIndex(fValues[paramCodecType]);
        if (t > 0) hi = (float)uiConfig(t, 0).order;
    }
}

bool DistrhoUICheeExcitation::controlEnabled(const Control& c) const
{
    const bool hd = typeIndex(fValues[paramCodecType]) != 0;
    if (c.group == kGroupLp || c.group == kGroupExc || c.group == kGroupAlgo) return hd;
    if (c.group == kGroupCodec && c.param != paramCodecType) return hd;
    if (c.param == paramFec) return isTetraPlus(typeIndex(fValues[paramCodecType]));
    // the network carries HD / TETRA+; a receiver follows whatever it hears
    if (c.group == kGroupNet) return hd || std::lround(fValues[paramNetMode]) == 2 || c.param == paramNetMode;
    return true;
}

int DistrhoUICheeExcitation::hitControl(double x, double y) const
{
    for (size_t i = 0; i < fControls.size(); ++i) {
        const Control& c = fControls[i];
        if (!controlVisible(c)) continue;
        if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h) return (int)i;
    }
    return -1;
}

void DistrhoUICheeExcitation::setControlValue(const Control& c, float v, bool notify)
{
    const uint32_t p = effParam(c);
    if (p == kPageLp) {                                 // UI-only LP page switch
        fLpPage = v >= 0.5f ? 1 : 0;
        repaint();
        return;
    }
    if (p >= kEditCodec && p <= kEditAlgo) {          // UI-only edit-side switch
        fEditSide[p - kEditCodec] = v >= 0.5f ? 1 : 0;
        repaint();
        return;
    }
    float lo, hi;
    controlRange(c, lo, hi);
    if (baseParam(p) == paramLpOrder && v >= hi) v = PARAMS[p].range_max; // top = full order
    v = std::fmax(lo, std::fmin(PARAMS[p].range_max, v));
    if (c.kind == kSegment || p == paramCorruptionMode || p == paramFec || p == paramCorruptTarget ||
        p == paramNetChannel) v = std::round(v);
    if (v == fValues[p]) return;
    fValues[p] = v;
    if (notify) setParameterValue(p, v);
    repaint();
}

void DistrhoUICheeExcitation::formatValue(const Control& c, char* buf, size_t n) const
{
    const uint32_t p = effParam(c);
    const float v = getVal(p);
    const int t = typeIndex(fValues[paramCodecType]);
    switch (baseParam(p)) {
    case paramCodecBitrate:
        if (t == 0) std::snprintf(buf, n, "4.6 kbit/s");
        else        std::snprintf(buf, n, "%.1f kbit/s", uiConfig(t, rateMode(t, v)).bitrate() / 1000.0);
        break;
    case paramCorruptTarget:
        std::snprintf(buf, n, "%s", TARGET_NAMES[clampi((int)std::lround(v), 0, 5)]);
        break;
    case paramNetChannel:
        std::snprintf(buf, n, "ch %d", (int)std::lround(v));
        break;
    case paramLpBand:
        std::snprintf(buf, n, v < 0.05f ? "off" : "-%.1f k", v);
        break;
    case paramNetLoss:
        std::snprintf(buf, n, "%.1f%%", v);
        break;
    case paramNetJitter:
        std::snprintf(buf, n, "%.0f ms", v);
        break;
    case paramFec:
        if (!isTetraPlus(t)) std::snprintf(buf, n, t == 0 ? "ETSI" : "n/a");
        else                 std::snprintf(buf, n, "%s", FEC_NAMES[clampi((int)std::lround(v), 0, 4)]);
        break;
    case paramCorruptionMode:
        std::snprintf(buf, n, "%s", MODE_NAMES[clampi((int)std::lround(v), 0, 10)]);
        break;
    case paramCorruptionIntensity: {
        // rate: share of frames (bitstream modes) or fields (parameter modes) hit
        const float rate = 100.0f * corrupt_rate_curve(v / 100.0f);
        const int mode = (int)std::lround(fValues[paramCorruptionMode]);
        const char* unit = mode >= kCorruptBitFlips ? "frm" : "fld";
        if (rate < 10.0f) std::snprintf(buf, n, "%.2f%% %s", rate, unit);
        else              std::snprintf(buf, n, "%.1f%% %s", rate, unit);
        break;
    }
    case paramCorruptionMagnitude: {
        const float d = v / 100.0f;
        switch ((int)std::lround(fValues[paramCorruptionMode])) {
        case kCorruptBitFlips:
        case kCorruptBurst: {
            const float ber = 100.0f * corrupt_ber(d);
            if (ber < 0.1f) std::snprintf(buf, n, "BER %.3f%%", ber);
            else            std::snprintf(buf, n, "BER %.2f%%", ber);
            break;
        }
        case kCorruptBitSlip:      std::snprintf(buf, n, "%d bit", corrupt_slip_bits(d)); break;
        case kCorruptOverflow:     std::snprintf(buf, n, "%d words", corrupt_overflow_words(d)); break;
        case kCorruptReinterleave: std::snprintf(buf, n, "%d swaps", corrupt_shuffle_swaps(d)); break;
        default:                   std::snprintf(buf, n, "%.1f%%", v); break;
        }
        break;
    }
    case paramLpWarp:
    case paramLpDepth:
        std::snprintf(buf, n, "%+d%%", (int)std::lround(v));
        break;
    case paramLpOrder: {
        float lo, hi;
        controlRange(c, lo, hi);
        if (v >= hi) std::snprintf(buf, n, "FULL %d", (int)hi);
        else         std::snprintf(buf, n, "%.1f", v);
        break;
    }
    case paramExcPitch:
        std::snprintf(buf, n, "%+.1f st", v);
        break;
    case paramExcNoise:
    case paramGainOfs:
        std::snprintf(buf, n, "%+.1f dB", v);
        break;
    case paramPreEmph:
    case paramLsfPred:
    case paramSharpen:
        std::snprintf(buf, n, "%.2f", v);
        break;
    default:
        std::snprintf(buf, n, "%d%%", (int)std::lround(v));
        break;
    }
}

// -----------------------------------------------------------------------
// DSP callbacks

void DistrhoUICheeExcitation::parameterChanged(uint32_t index, float value)
{
    if (index < (uint32_t)paramCount) {
        fValues[index] = value;
        repaint();
    }
}

void DistrhoUICheeExcitation::stateChanged(const char*, const char*)
{
    repaint();
}

// -----------------------------------------------------------------------
// Input

bool DistrhoUICheeExcitation::onMouse(const MouseEvent& ev)
{
    const double x = ev.pos.getX(), y = ev.pos.getY();

    if (ev.press && ev.button == DGL_NAMESPACE::kMouseButtonLeft && x >= UI_W - 40 && y < 40) {
        fAboutWindow.runAsModal();
        return true;
    }
    // RESET: first click arms (3 s), second click resets every parameter
    if (ev.press && ev.button == DGL_NAMESPACE::kMouseButtonLeft &&
        x >= RESET_X && x < RESET_X + RESET_W && y >= 12 && y < 32) {
        if (fResetArmedUntil > fTime) {
            resetToDefaults();
            fResetArmedUntil = 0.0;
        } else {
            fResetArmedUntil = fTime + 3.0;
        }
        repaint();
        return true;
    }

    const int hit = hitControl(x, y);

    // Right click: MIDI learn on the parameter the control currently edits
    // (armed -> cancel, mapped -> unmap, else arm)
    if (ev.press && ev.button == DGL_NAMESPACE::kMouseButtonRight && hit >= 0) {
        const uint32_t param = effParam(fControls[hit]);
        PluginACELP* p = plugin();
        if (p != nullptr && param < (uint32_t)paramCount) {
            if (p->learnArmed.load() == (int)param) p->learnArmed.store(-1);
            else if (p->ccForParam(param) >= 0)     p->clearCcForParam(param);
            else                                    p->learnArmed.store((int)param);
            repaint();
        }
        return true;
    }

    if (ev.button != DGL_NAMESPACE::kMouseButtonLeft) return false;

    if (ev.press) {
        if (hit < 0) return false;
        const Control& c = fControls[hit];
        const uint32_t p = effParam(c);
        if (c.kind == kSegment) {
            float gx, gy, gw, gh;
            segRect(c, gx, gy, gw, gh);
            const int cols = c.segCols, rows = (c.nseg + cols - 1) / cols;
            const int cx = clampi((int)((x - gx) / (gw / cols)), 0, cols - 1);
            const int cy = clampi((int)((y - gy) / (gh / rows)), 0, rows - 1);
            const int sidx = cy * cols + cx;
            if (sidx < c.nseg && y >= gy && y < gy + gh) {
                const bool real = p < (uint32_t)paramCount;
                if (real) editParameter(p, true);
                setControlValue(c, (float)sidx, true);
                if (real) editParameter(p, false);
            }
            return true;
        }
        // knob: double-click resets, otherwise start a drag
        if (hit == fLastClickControl && ev.time - fLastClickTime < 350) {
            editParameter(p, true);
            setControlValue(c, banks[DEFAULT_BANK].presets[DEFAULT_PRESET].params[p], true);
            editParameter(p, false);
            fLastClickControl = -1;
            return true;
        }
        fLastClickControl = hit;
        fLastClickTime = ev.time;
        fDrag = hit;
        fDragStartY = (float)y;
        fDragStartValue = getVal(p);
        editParameter(p, true);
        return true;
    }

    if (fDrag >= 0) {
        editParameter(effParam(fControls[fDrag]), false);
        fDrag = -1;
        return true;
    }
    return false;
}

bool DistrhoUICheeExcitation::onMotion(const MotionEvent& ev)
{
    if (fDrag < 0) return false;
    const Control& c = fControls[fDrag];
    float lo, hi;
    controlRange(c, lo, hi);
    // 180 px for the full range; Shift = 10x finer
    const float px = (ev.mod & DGL_NAMESPACE::kModifierShift) ? 1800.0f : 180.0f;
    float v = fDragStartValue;
    if (v > hi) v = hi;
    v += (fDragStartY - (float)ev.pos.getY()) / px * (hi - lo);
    setControlValue(c, v, true);
    return true;
}

bool DistrhoUICheeExcitation::onScroll(const ScrollEvent& ev)
{
    const int hit = hitControl(ev.pos.getX(), ev.pos.getY());
    if (hit < 0) return false;
    const Control& c = fControls[hit];
    const uint32_t p = effParam(c);
    float lo, hi;
    controlRange(c, lo, hi);
    float step = (hi - lo) * ((ev.mod & DGL_NAMESPACE::kModifierShift) ? 0.002f : 0.02f);
    if (c.kind == kSegment || p == paramCorruptionMode || p == paramFec || p == paramCorruptTarget ||
        p == paramNetChannel) step = 1.0f;
    float v = getVal(p);
    if (v > hi) v = hi;
    const bool real = p < (uint32_t)paramCount;
    if (real) editParameter(p, true);
    setControlValue(c, v + (float)ev.delta.getY() * step, true);
    if (real) editParameter(p, false);
    return true;
}

// -----------------------------------------------------------------------
// Codec data feed -> scrolling lane images

void DistrhoUICheeExcitation::initLane(Lane& l, int w, int h)
{
    l.w = w;
    l.h = h;
    l.rgba.assign((size_t)w * h * 4, 0);
    for (size_t i = 0; i < l.rgba.size(); i += 4) putpx(&l.rgba[i], C_BG.r, C_BG.g, C_BG.b);
    l.imageId = 0;
    l.dirty = true;
}

void DistrhoUICheeExcitation::scrollLanes(int px)
{
    Lane* lanes[4] = { &fLaneSpec, &fLanePitch, &fLanePulse, &fLaneBits };
    for (Lane* l : lanes) {
        px = std::min(px, l->w);
        for (int y = 0; y < l->h; ++y) {
            unsigned char* row = &l->rgba[(size_t)y * l->w * 4];
            std::memmove(row, row + px * 4, (size_t)(l->w - px) * 4);
        }
        l->dirty = true;
    }
}

void DistrhoUICheeExcitation::paintLaneColumn(const CodecVizRecord& r, int px)
{
    // --- envelope spectrogram (fixed log axis, top = 24 kHz)
    {
        Lane& l = fLaneSpec;
        for (int y = 0; y < l.h; ++y) {
            const int bin = clampi((int)((1.0f - (y + 0.5f) / l.h) * VIZ_ENV_BINS), 0, VIZ_ENV_BINS - 1);
            const float db = r.env[bin];
            for (int x = l.w - px; x < l.w; ++x) {
                unsigned char* p = &l.rgba[((size_t)y * l.w + x) * 4];
                if (db <= VIZ_ENV_NONE + 1.0f) {
                    // above this codec's Nyquist: hatched
                    if (((x + y) % 7) == 0) putpx(p, 20, 34, 26);
                    else                    putpx(p, 8, 15, 11);
                } else {
                    cmap((db + 80.0f) / 110.0f, p); // -80..+30 dB (envelope peaks sit well above the frame level)
                }
            }
        }
    }

    const int x0 = std::max(0, fLanePitch.w - px);
    auto subX = [&](int s) { return x0 + (s * px) / VIZ_NSUB; };
    auto clearCols = [&](Lane& l, int r0, int g0, int b0) {
        for (int y = 0; y < l.h; ++y)
            for (int x = l.w - px; x < l.w; ++x) {
                unsigned char* p = &l.rgba[((size_t)y * l.w + x) * 4];
                // faint gridlines every quarter height
                if (y % (l.h / 4) == 0) putpx(p, r0 + 8, g0 + 16, b0 + 10);
                else                    putpx(p, r0, g0, b0);
            }
    };

    // --- pitch (green, log 50..800 Hz, brightness = pitch gain) + innovation gain (amber)
    {
        Lane& l = fLanePitch;
        clearCols(l, 7, 15, 11);
        for (int s = 0; s < VIZ_NSUB; ++s) {
            const int x = std::min(l.w - 1, subX(s));
            const float gdb = r.gcDb[s];
            if (r.kind != 0 || gdb != 0.0f) {
                const int yg = clampi((int)((1.0f - (gdb + 90.0f) / 90.0f) * (l.h - 1)), 0, l.h - 1);
                putpx(&l.rgba[((size_t)yg * l.w + x) * 4], 150, 100, 25);
            }
            if (r.f0[s] > 0.0f) {
                const float t = std::log(r.f0[s] / 50.0f) / std::log(800.0f / 50.0f);
                const int y = clampi((int)((1.0f - t) * (l.h - 1)), 0, l.h - 2);
                const float b = 0.35f + 0.65f * std::fmin(1.0f, r.gp[s] / 1.2f);
                for (int dy = 0; dy < 2; ++dy)
                    putpx(&l.rgba[((size_t)(y + dy) * l.w + x) * 4], (int)(C_GREEN.r * b), (int)(C_GREEN.g * b), (int)(C_GREEN.b * b));
            }
        }
    }

    // --- algebraic pulses: y = position in the subframe, green +, amber -
    {
        Lane& l = fLanePulse;
        clearCols(l, 7, 13, 12);
        for (int s = 0; s < VIZ_NSUB; ++s) {
            const int x = std::min(l.w - 1, subX(s));
            for (int p = 0; p < r.npulses[s]; ++p) {
                const int v = r.pulse[s][p];
                const int y = clampi(((v < 0 ? -v : v) - 1) * l.h / 1000, 0, l.h - 1);
                const RGB& c = v > 0 ? C_GREEN : C_AMBER;
                putpx(&l.rgba[((size_t)y * l.w + x) * 4], c.r, c.g, c.b);
            }
        }
    }

    // --- bitstream: rows = bits (top = first bit), colour = meaning, red = corrupted
    {
        Lane& l = fLaneBits;
        const int n = std::max(1, r.nbits);
        for (int y = 0; y < l.h; ++y) {
            const int b0 = y * n / l.h;
            const int b1 = std::max(b0 + 1, (y + 1) * n / l.h);
            int ones = 0, bad = 0;
            for (int b = b0; b < b1 && b < VIZ_MAX_BITS; ++b) {
                ones += r.bits[b] & 1;
                bad += (r.bits[b] >> 4) & 1;
            }
            const RGB& c = BIT_COL[clampi((r.bits[b0] >> 1) & 7, 0, kVizBitCount - 1)];
            const float f = 0.18f + 0.82f * (float)ones / (b1 - b0);
            for (int x = l.w - px; x < l.w; ++x) {
                unsigned char* p = &l.rgba[((size_t)y * l.w + x) * 4];
                if (r.nbits == 0)  putpx(p, C_BG.r, C_BG.g, C_BG.b);
                else if (bad)      putpx(p, 255, 60 + 120 * ones / (b1 - b0), 50);
                else               putpx(p, (int)(c.r * f), (int)(c.g * f), (int)(c.b * f));
            }
        }
    }
}

void DistrhoUICheeExcitation::drainFeed()
{
    PluginACELP* p = plugin();
    if (p == nullptr) return;
    CheetahDSP& dsp = p->getDSP();

    unsigned gen;
    if (lpc_viz_read(dsp.lpcViz, fFrame, gen)) fHaveFrame = true;

    const uint32_t head = dsp.vizFeed.count.load(std::memory_order_acquire);
    if (head - fFeedNext > VIZ_RING) fFeedNext = head - VIZ_RING + 1; // fell behind: skip
    static CodecVizRecord rec;
    while (fFeedNext != head) {
        if (dsp.vizFeed.get(fFeedNext, rec)) {
            const int px = std::max(1, (int)std::lround(rec.frameMs * PX_PER_SECOND / 1000.0f));
            scrollLanes(px);
            paintLaneColumn(rec, px);
            std::memcpy(&fLast, &rec, sizeof(rec));
            fHaveRec = true;
        }
        ++fFeedNext;
    }
}

void DistrhoUICheeExcitation::idleCallback()
{
    fTime += 0.033;
    drainFeed();
    repaint();
}

void DistrhoUICheeExcitation::uploadLane(Lane& l)
{
    if (l.imageId == 0) {
        // 1 << 5 = NVG_IMAGE_NEAREST (pixel-exact, drawn 1:1)
        auto h = fNvg.createImageFromRGBA((uint)l.w, (uint)l.h, l.rgba.data(), 1 << 5);
        l.imageId = h.imageId;
        l.image = h;
        l.dirty = false;
    } else if (l.dirty) {
        nvgUpdateImage(fNvg.getContext(), l.imageId, l.rgba.data());
        l.dirty = false;
    }
}

void DistrhoUICheeExcitation::drawLane(Lane& l, float x, float y)
{
    uploadLane(l);
    if (l.imageId == 0) return;
    fNvg.beginPath();
    fNvg.rect(x, y, (float)l.w, (float)l.h);
    fNvg.fillPaint(fNvg.imagePattern(x, y, (float)l.w, (float)l.h, 0.0f, l.image, 1.0f));
    fNvg.fill();
}

// -----------------------------------------------------------------------
// Drawing

void DistrhoUICheeExcitation::drawPanel(float x, float y, float w, float h, const char* title)
{
    fNvg.beginPath();
    fNvg.roundedRect(x + 0.5f, y + 0.5f, w - 1, h - 1, 5);
    fNvg.fillColor(col(C_PANEL));
    fNvg.fill();
    fNvg.strokeColor(col(C_BORDER));
    fNvg.strokeWidth(1.0f);
    fNvg.stroke();
    if (title) {
        fNvg.fontSize(10);
        fNvg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_TOP);
        fNvg.fillColor(col(C_MUTED));
        fNvg.text(x + 8, y + 5, title, nullptr);
    }
}

void DistrhoUICheeExcitation::drawHeader()
{
    fNvg.fontSize(22);
    fNvg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE);
    fNvg.fillColor(col(C_GREEN));
    fNvg.text(M, 22, "ALGEBRAIC CHEETAH", nullptr);
    fNvg.fontSize(12);
    fNvg.fillColor(col(C_MUTED));
    fNvg.text(242, 24, "EXCITATION L-P", nullptr);

    // codec status chip
    char buf[128];
    const int t = typeIndex(fValues[paramCodecType]);
    if (t == 0) {
        std::snprintf(buf, sizeof(buf), "TETRA ACELP  ·  8 kHz  ·  4.6 kbit/s  ·  fixed point (ETSI)");
    } else if (isTetraPlus(t)) {
        static const char* const levels[3] = { "low", "mid", "high" };
        const int level = rateMode(t, fValues[paramCodecBitrate]);
        const hdacelp::Config c = uiConfig(t, level);
        const int fec = clampi((int)std::lround(fValues[paramFec]), 0, 4);
        const double chanKbps = channelSymbols(c, fec) * (double)c.fs / c.frameLen / 1000.0;
        std::snprintf(buf, sizeof(buf), "TETRA+  ·  %d kHz  ·  order %d  ·  %s  ·  %.1f kbit/s  ·  FEC %s -> %.1f kbit/s  ·  %s",
                      c.fs / 1000, c.order, levels[level], c.bitrate() / 1000.0, FEC_NAMES[fec], chanKbps,
                      fValues[paramHdMath] >= 0.5f ? "16-bit fixed" : "float");
    } else {
        const int mode = rateMode(t, fValues[paramCodecBitrate]);
        const hdacelp::Config c = uiConfig(t, mode);
        std::snprintf(buf, sizeof(buf), "HD ACELP  ·  %d kHz  ·  order %d  ·  mode %d  ·  %.1f kbit/s  ·  %s",
                      c.fs / 1000, c.order, mode, c.bitrate() / 1000.0,
                      fValues[paramHdMath] >= 0.5f ? "16-bit fixed" : "float");
    }
    fNvg.fontSize(11);
    DGL_NAMESPACE::Rectangle<float> bounds;
    fNvg.textBounds(0, 0, buf, nullptr, bounds);
    const float cw = bounds.getWidth() + 20, cx = 360;
    fNvg.beginPath();
    fNvg.roundedRect(cx, 12, cw, 20, 10);
    fNvg.fillColor(col(C_PANEL2));
    fNvg.fill();
    fNvg.strokeColor(col(C_BORDER));
    fNvg.stroke();
    fNvg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE);
    fNvg.fillColor(col(C_TEXT));
    fNvg.text(cx + 10, 22, buf, nullptr);

    // MIDI corruption keys (C3..B3), lit while held
    PluginACELP* p = plugin();
    const uint16_t held = p ? p->getDSP().heldMidiCorruptKeys() : 0;
    const float kx = 852, ky = 9, kw = 12, kh = 26;
    static const int whiteKeys[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int blackKeys[5] = { 1, 3, 6, 8, 10 };
    static const int blackPos[5]  = { 1, 2, 4, 5, 6 };
    for (int i = 0; i < 7; ++i) {
        const bool on = held & (1u << whiteKeys[i]);
        fNvg.beginPath();
        fNvg.rect(kx + i * kw, ky, kw - 1, kh);
        fNvg.fillColor(on ? col(C_RED) : Color(150, 170, 158));
        fNvg.fill();
    }
    for (int i = 0; i < 5; ++i) {
        const bool on = held & (1u << blackKeys[i]);
        fNvg.beginPath();
        fNvg.rect(kx + blackPos[i] * kw - 4, ky, 8, kh * 0.6f);
        fNvg.fillColor(on ? col(C_RED) : Color(20, 30, 24));
        fNvg.fill();
    }
    fNvg.fontSize(9);
    fNvg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE);
    fNvg.fillColor(col(C_MUTED));
    fNvg.text(kx + 7 * kw + 6, 16, "MIDI", nullptr);
    fNvg.text(kx + 7 * kw + 6, 28, "C3-B3", nullptr);

    // MIDI learn status
    const int armed = p ? p->learnArmed.load() : -1;
    fNvg.textAlign(NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE);
    fNvg.fontSize(10);
    if (armed > 0) {
        const int a = 140 + (int)(115 * (0.5 + 0.5 * std::sin(fTime * 8.0)));
        std::snprintf(buf, sizeof(buf), "MIDI LEARN: move a CC for %s", PARAMS[armed].name);
        fNvg.fillColor(col(C_AMBER, a));
        fNvg.text(844, 40, buf, nullptr);
    } else {
        fNvg.fillColor(col(C_MUTED, 150));
        fNvg.text(844, 40, "right-click a control: MIDI learn", nullptr);
    }

    // reset button (two-click)
    {
        const bool armed = fResetArmedUntil > fTime;
        fNvg.beginPath();
        fNvg.roundedRect(RESET_X, 12, RESET_W, 20, 4);
        fNvg.fillColor(armed ? col(C_RED, 70) : col(C_PANEL2));
        fNvg.fill();
        fNvg.strokeColor(armed ? col(C_RED) : col(C_BORDER));
        fNvg.stroke();
        fNvg.fontSize(10);
        fNvg.textAlign(NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE);
        fNvg.fillColor(armed ? col(C_TEXT) : col(C_MUTED));
        fNvg.text(RESET_X + RESET_W / 2, 22, armed ? "CONFIRM?" : "RESET", nullptr);
    }

    // about button
    fNvg.beginPath();
    fNvg.circle(UI_W - 24, 22, 11);
    fNvg.strokeColor(col(C_MUTED));
    fNvg.stroke();
    fNvg.fontSize(13);
    fNvg.textAlign(NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE);
    fNvg.fillColor(col(C_MUTED));
    fNvg.text(UI_W - 24, 23, "?", nullptr);
}

// Encode -> channel -> decode, as a two-row snake: encoder left to right,
// decoder right to left. Every block shows a live value from the last frame.
void DistrhoUICheeExcitation::drawChain()
{
    drawPanel(M, CHAIN_Y, UI_W - 2 * M, CHAIN_H);

    enum { kNormal, kEffect, kCorrupt, kOff };
    struct Block { char title[20]; char value[28]; int state; };
    Block row1[11], row2[8];
    auto set = [](Block& b, const char* t, int state, const char* fmt, ...) {
        std::snprintf(b.title, sizeof(b.title), "%s", t);
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(b.value, sizeof(b.value), fmt, ap);
        va_end(ap);
        b.state = state;
    };

    const CodecVizRecord& r = fLast;
    const int t = typeIndex(fValues[paramCodecType]);
    const bool hd = t != 0;
    const int host = (int)std::lround(getSampleRate() / 1000.0);
    const int fsK = TYPE_RATES[t] / 1000;
    float f0 = 0.0f, gp = 0.0f;
    int npulses = 0;
    for (int s = 0; s < VIZ_NSUB; ++s) {
        if (r.f0[s] > 0.0f) { f0 = r.f0[s]; gp = r.gp[s]; }
        npulses += r.npulses[s];
    }
    const bool live = fHaveRec;
    const int corrState = r.bitCorr && r.nflipped > 0 ? kCorrupt : kOff;

    if (hd) {
        // encoder side vs decoder side (they differ when a group is split);
        // blocks where the two sides disagree are drawn amber
        const bool codecSplit = fValues[paramCodecSplit] >= 0.5f;
        const bool algoSplit = fValues[paramAlgoSplit] >= 0.5f;
        auto modeOf = [t](float v) { return rateMode(t, v); };
        const int mode = modeOf(fValues[paramCodecBitrate]);
        const int decMode = codecSplit ? modeOf(fValues[paramDecBitrate]) : mode;
        const bool encFx = fValues[paramHdMath] >= 0.5f;
        const bool decFx = codecSplit ? fValues[paramDecMath] >= 0.5f : encFx;
        auto dv = [&](uint32_t enc, uint32_t dec) { return algoSplit ? fValues[dec] : fValues[enc]; };
        const float encPre = fValues[paramPreEmph], decPre = dv(paramPreEmph, paramDecPreEmph);
        const float encPred = fValues[paramLsfPred], decPred = dv(paramLsfPred, paramDecLsfPred);
        const float encOfs = fValues[paramGainOfs], decOfs = dv(paramGainOfs, paramDecGainOfs);
        const float encSh = fValues[paramSharpen], decSh = dv(paramSharpen, paramDecSharpen);
        const hdacelp::Config c = uiConfig(t, mode);
        const hdacelp::Config cd = uiConfig(t, decMode);
        int lsfBits = 0;
        for (int i = 0; i < c.order; ++i) lsfBits += c.lsfBits(i);
        const bool lpEnc = live && r.lpEnc;
        const bool lpDec = live && r.lpDec;
        auto mis = [](bool differ) { return differ ? kEffect : kNormal; };
        set(row1[0], "IN", kNormal, live ? "%.0f dBFS" : "-", r.inDb);
        set(row1[1], "RESAMPLE", kNormal, "%dk -> %dk", host, fsK);
        set(row1[2], "PRE-EMPH", mis(encPre != decPre), "HPF + %.2f", encPre);
        set(row1[3], "LPC", kNormal, "order %d", c.order);
        set(row1[4], "LP MOD", lpEnc ? kEffect : kOff, lpEnc ? "in-loop" : "off");
        set(row1[5], "LSF QUANT", mis(encPred != decPred), "%d b  p%.2f", lsfBits, encPred);
        if (f0 > 0.0f) set(row1[6], "PITCH", kNormal, "%.0f Hz", f0);
        else           set(row1[6], "PITCH", kNormal, "unvoiced");
        set(row1[7], "ALG. CODEBOOK", kNormal, "%d pulses", c.tracks * c.pulsesPerTrack * 4);
        set(row1[8], "GAIN QUANT", mis(encOfs != decOfs), encOfs != 0.0f ? "gp %.2f %+.0fdB" : "gp %.2f", gp, encOfs);
        set(row1[9], "PARAM CORR.", r.paramCorr ? kCorrupt : kOff, r.paramCorr ? "active" : "off");
        set(row1[10], "PACK", kNormal, "%d bits", c.numBits());
        if (live && r.target != 0 && r.bitCorr)
            set(row2[0], "BIT CORR.", r.nTargeted ? kCorrupt : kEffect, "%d in %s", r.nTargeted, TARGET_NAMES[clampi(r.target, 0, 5)]);
        else
            set(row2[0], "BIT CORR.", corrState, r.bitCorr ? "%d flipped" : "off", r.nflipped);
        if (decMode != mode) set(row2[1], "UNPACK", kEffect, "as mode %d", decMode);
        else                 set(row2[1], "UNPACK", kNormal, "%d fields", c.numFields());
        if (isTetraPlus(t)) {
            // the protected channel replaces PACK / UNPACK
            const int fec = clampi((int)std::lround(fValues[paramFec]), 0, 4);
            const hdacelp::FecRates fr = hdacelp::fec_rates(fec);
            if (fr.d1) set(row1[10], "CRC + CONV", kNormal, "r%d/%d  %d sym", fr.n1, fr.d1, channelSymbols(c, fec));
            else       set(row1[10], "NO FEC", kOff, "%d raw bits", c.numBits());
            if (!fr.d1)            set(row2[1], "HARD BITS", kOff, "no CRC");
            else if (live && r.bfi) set(row2[1], "VITERBI+BFI", kCorrupt, "BAD -> conceal %d", r.nbad);
            else                   set(row2[1], "VITERBI+BFI", kNormal, decMode != mode ? "ok, as lvl %d" : "CRC ok", decMode);
        }
        if (r.excActive)        set(row2[2], "EXCITATION", kEffect, "%+.1f st %d%%", fValues[paramExcPitch], (int)fValues[paramExcVoice]);
        else if (decSh != encSh || decOfs != encOfs)
                                set(row2[2], "EXCITATION", kEffect, "sh %.2f %+.0fdB", decSh, decOfs);
        else                    set(row2[2], "EXCITATION", kNormal, "gp*v + gc*c");
        set(row2[3], "LP MOD", lpDec ? kEffect : kOff, lpDec ? "resynth" : "off");
        if (decFx != encFx) set(row2[4], "1/A(z)", kEffect, decFx ? "fixed (enc float)" : "float (enc fixed)");
        else                set(row2[4], "1/A(z)", decFx ? kEffect : kNormal, decFx ? "16-bit fixed" : "float");
        set(row2[5], "DE-EMPH", mis(encPre != decPre), "1/(1-%.2fz)", decPre);
        set(row2[6], "RESAMPLE", kNormal, "%dk -> %dk", fsK, host);
        if (live && r.guardDb < -0.5f) set(row2[7], "OUT GUARD", kEffect, "%.0f dB  (%.0f)", r.outDb, r.guardDb);
        else                           set(row2[7], "OUT", kNormal, live ? "%.0f dBFS" : "-", r.outDb);
    } else {
        set(row1[0], "IN", kNormal, live ? "%.0f dBFS" : "-", r.inDb);
        set(row1[1], "RESAMPLE", kNormal, "%dk -> 8k", host);
        set(row1[2], "PRE-PROC", kNormal, "HPF");
        set(row1[3], "LPC", kNormal, "order 10");
        set(row1[4], "LSP VQ", kNormal, "3-split");
        if (f0 > 0.0f) set(row1[5], "PITCH", kNormal, "~%.0f Hz", f0);
        else           set(row1[5], "PITCH", kNormal, "unvoiced");
        set(row1[6], "ACELP", kNormal, "4 pulses/sf");
        set(row1[7], "GAIN VQ", kNormal, "6 bits/sf");
        set(row1[8], "PARAM CORR.", r.paramCorr ? kCorrupt : kOff, r.paramCorr ? "active" : "off");
        set(row1[9], "CRC + CONV", kNormal, "2 x 137 bits");
        set(row1[10], "INTERLEAVE", kNormal, "432 bits");
        if (live && r.target != 0 && r.bitCorr)
            set(row2[0], "BIT CORR.", r.nTargeted ? kCorrupt : kEffect, "%d in %s", r.nTargeted, TARGET_NAMES[clampi(r.target, 0, 5)]);
        else
            set(row2[0], "BIT CORR.", corrState, r.bitCorr ? "%d flipped" : "off", r.nflipped);
        set(row2[1], "DEINTERLEAVE", kNormal, "432 bits");
        set(row2[2], "VITERBI+BFI", kNormal, "error conceal");
        set(row2[3], "EXCITATION", kNormal, "adapt + alg");
        set(row2[4], "1/A(z)", kEffect, "16-bit (ETSI)");
        set(row2[5], "POST-PROC", kNormal, "de-emph");
        set(row2[6], "RESAMPLE", kNormal, "8k -> %dk", host);
        set(row2[7], "OUT", kNormal, live ? "%.0f dBFS" : "-", r.outDb);
    }

    const float x0 = M + 10, gap = 13;
    const float bw = (UI_W - 2 * M - 20 - 10 * gap) / 11.0f, bh = 34;
    const float y1 = CHAIN_Y + 10, y2 = CHAIN_Y + CHAIN_H - 10 - bh;
    auto slotX = [&](int i) { return x0 + i * (bw + gap); };

    auto drawBlock = [&](const Block& b, float x, float y) {
        RGB border = C_BORDER, title = C_MUTED;
        int fillA = 255;
        if (b.state == kEffect)  { border = C_AMBER; title = C_AMBER; }
        if (b.state == kCorrupt) { border = C_RED;   title = C_RED; }
        if (b.state == kOff)     { fillA = 120; }
        fNvg.beginPath();
        fNvg.roundedRect(x, y, bw, bh, 4);
        if (b.state == kCorrupt) {
            const int a = 50 + (int)(40 * (0.5 + 0.5 * std::sin(fTime * 10.0)));
            fNvg.fillColor(col(C_RED, a));
        } else {
            fNvg.fillColor(col(C_PANEL2, fillA));
        }
        fNvg.fill();
        fNvg.strokeColor(col(border, b.state == kOff ? 110 : 255));
        fNvg.strokeWidth(1.0f);
        fNvg.stroke();
        fNvg.textAlign(NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE);
        fNvg.fontSize(8.5f);
        fNvg.fillColor(col(title, b.state == kOff ? 130 : 255));
        fNvg.text(x + bw / 2, y + 10, b.title, nullptr);
        fNvg.fontSize(10.5f);
        fNvg.fillColor(col(C_TEXT, b.state == kOff ? 120 : 255));
        fNvg.text(x + bw / 2, y + 24, b.value, nullptr);
    };
    // arrow from (ax,ay) to (bx,by) with a travelling dot
    int arrowNo = 0;
    auto drawArrow = [&](float ax, float ay, float bx, float by) {
        fNvg.beginPath();
        fNvg.moveTo(ax, ay);
        fNvg.lineTo(bx, by);
        fNvg.strokeColor(col(C_BORDER));
        fNvg.strokeWidth(1.5f);
        fNvg.stroke();
        const float dx = bx - ax, dy = by - ay, len = std::sqrt(dx * dx + dy * dy);
        const float ux = dx / len, uy = dy / len;
        fNvg.beginPath();
        fNvg.moveTo(bx, by);
        fNvg.lineTo(bx - ux * 5 - uy * 3, by - uy * 5 + ux * 3);
        fNvg.lineTo(bx - ux * 5 + uy * 3, by - uy * 5 - ux * 3);
        fNvg.closePath();
        fNvg.fillColor(col(C_MUTED));
        fNvg.fill();
        if (live) {
            const float ph = (float)std::fmod(fTime * 1.2 + arrowNo * 0.09, 1.0);
            fNvg.beginPath();
            fNvg.circle(ax + dx * ph, ay + dy * ph, 1.8f);
            fNvg.fillColor(col(C_GREEN, 200));
            fNvg.fill();
        }
        ++arrowNo;
    };

    for (int i = 0; i < 11; ++i) {
        drawBlock(row1[i], slotX(i), y1);
        if (i < 10) drawArrow(slotX(i) + bw + 1, y1 + bh / 2, slotX(i + 1) - 1, y1 + bh / 2);
    }
    // channel: PACK -> BIT CORR (down the right edge)
    drawArrow(slotX(10) + bw / 2, y1 + bh + 1, slotX(10) + bw / 2, y2 - 1);
    for (int i = 0; i < 8; ++i) {
        const int slot = 10 - i;
        drawBlock(row2[i], slotX(slot), y2);
        if (i < 7) drawArrow(slotX(slot) - 1, y2 + bh / 2, slotX(slot - 1) + bw + 1, y2 + bh / 2);
    }

    // labels in the free slots of the decoder row
    fNvg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE);
    fNvg.fontSize(10);
    fNvg.fillColor(col(C_GREEN));
    fNvg.text(slotX(0), y2 + 6, "ENCODE  ->", nullptr);
    fNvg.fillColor(col(C_MUTED));
    fNvg.text(slotX(0), y2 + 20, "<-  DECODE", nullptr);
    char buf[64];
    if (live && r.netMode == 1)      std::snprintf(buf, sizeof(buf), "%.1f kbit/s  ·  LAN TX ch %d", r.kbps, r.netChannel);
    else if (live && r.netMode >= 2) std::snprintf(buf, sizeof(buf), "%.1f kbit/s  ·  LAN RX ch %d  ·  loss %.0f%%", r.kbps, r.netChannel, r.netLossPct);
    else if (live) std::snprintf(buf, sizeof(buf), "%.1f kbit/s  ·  %d-bit frames", r.kbps, r.nbits);
    else      std::snprintf(buf, sizeof(buf), "waiting for audio");
    fNvg.fillColor(col(C_TEXT));
    fNvg.text(slotX(1) + 10, y2 + 6, "CHANNEL", nullptr);
    fNvg.fillColor(col(C_MUTED));
    fNvg.text(slotX(1) + 10, y2 + 20, buf, nullptr);
}

void DistrhoUICheeExcitation::drawLanes()
{
    struct L { Lane* lane; float y, h; const char* title; };
    const L lanes[4] = {
        { &fLaneSpec,  SPEC_Y,  SPEC_H,  "ENVELOPE SPECTROGRAM  |1/A(z)| + level" },
        { &fLanePitch, PITCH_Y, PITCH_H, "PITCH (green, bright = voiced)  ·  CODEBOOK GAIN (amber)" },
        { &fLanePulse, PULSE_Y, PULSE_H, "ALGEBRAIC PULSES  (position in subframe, green + / amber -)" },
        { &fLaneBits,  BITS_Y,  BITS_H,  "BITSTREAM  (colour = meaning, red = corrupted)" },
    };
    for (const L& l : lanes) {
        drawLane(*l.lane, LANE_X, l.y);
        fNvg.beginPath();
        fNvg.rect(LANE_X + 0.5f, l.y + 0.5f, LANE_W - 1, l.h - 1);
        fNvg.strokeColor(col(C_BORDER));
        fNvg.strokeWidth(1.0f);
        fNvg.stroke();
        // title on a translucent strip
        fNvg.fontSize(9.5f);
        DGL_NAMESPACE::Rectangle<float> b;
        fNvg.textBounds(0, 0, l.title, nullptr, b);
        fNvg.beginPath();
        fNvg.rect(LANE_X + 1, l.y + 1, b.getWidth() + 10, 13);
        fNvg.fillColor(Color(0, 0, 0, 0.59f));
        fNvg.fill();
        fNvg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_TOP);
        fNvg.fillColor(col(C_TEXT, 220));
        fNvg.text(LANE_X + 5, l.y + 2, l.title, nullptr);
    }

    // time ticks (1 s) along the bottom lane
    fNvg.fontSize(8.5f);
    fNvg.textAlign(NanoVG::ALIGN_CENTER | NanoVG::ALIGN_TOP);
    for (int s = 1; s * PX_PER_SECOND < LANE_W; ++s) {
        const float x = LANE_X + LANE_W - s * PX_PER_SECOND;
        fNvg.beginPath();
        fNvg.moveTo(x, SPEC_Y);
        fNvg.lineTo(x, BITS_Y + BITS_H);
        fNvg.strokeColor(Color(255, 255, 255, 0.07f));
        fNvg.stroke();
        char buf[8];
        std::snprintf(buf, sizeof(buf), "-%ds", s);
        fNvg.fillColor(col(C_MUTED, 200));
        fNvg.text(x, BITS_Y + BITS_H - 11, buf, nullptr);
    }

    // frequency labels on the spectrogram (fixed 50 Hz..24 kHz log axis)
    fNvg.textAlign(NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE);
    static const int marks[] = { 100, 300, 1000, 3000, 8000, 16000 };
    static const char* const names[] = { "100", "300", "1k", "3k", "8k", "16k" };
    for (int m = 0; m < 6; ++m) {
        const float t = (float)(std::log(marks[m] / VIZ_ENV_FMIN) / std::log(VIZ_ENV_FMAX / VIZ_ENV_FMIN));
        const float y = SPEC_Y + (1.0f - t) * SPEC_H;
        fNvg.fillColor(Color(0, 0, 0, 0.55f));
        fNvg.beginPath();
        fNvg.rect(LANE_X + LANE_W - 26, y - 6, 24, 12);
        fNvg.fill();
        fNvg.fillColor(col(C_TEXT, 200));
        fNvg.text(LANE_X + LANE_W - 4, y, names[m], nullptr);
    }
    // pitch axis
    static const int pm[] = { 100, 200, 400 };
    for (int m = 0; m < 3; ++m) {
        const float t = std::log(pm[m] / 50.0f) / std::log(800.0f / 50.0f);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d Hz", pm[m]);
        fNvg.fillColor(col(C_MUTED, 220));
        fNvg.text(LANE_X + LANE_W - 4, PITCH_Y + (1.0f - t) * PITCH_H, buf, nullptr);
    }

    // bitstream section labels for the current layout
    if (fHaveRec && fLast.nbits > 0) {
        const int n = fLast.nbits;
        auto yOfBit = [&](int b) { return BITS_Y + (float)b / n * BITS_H; };
        fNvg.textAlign(NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_TOP);
        if (fLast.kind == 2) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "channel slot, FEC %s (interleaved)", FEC_NAMES[clampi(fLast.fec, 0, 4)]);
            fNvg.fillColor(col(C_TEXT, 200));
            fNvg.text(LANE_X + LANE_W - 4, BITS_Y + 1, buf, nullptr);
        } else if (fLast.kind == 1) {
            hdacelp::Config c = hdacelp::Config::make(fLast.fs, fLast.mode);
            c.gainVq = fLast.gainVq;
            hdacelp::Field f[hdacelp::HD_MAX_FIELDS];
            c.layout(f);
            int bit = 0, field = 0;
            for (int i = 0; i < c.order; ++i) bit += f[field++].bits;
            fNvg.fillColor(col(C_TEXT, 200));
            fNvg.text(LANE_X + LANE_W - 4, yOfBit(0) + 1, "LSF", nullptr);
            for (int s = 0; s < hdacelp::HD_NSUB; ++s) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "SF%d", s + 1);
                const float y = yOfBit(bit);
                fNvg.beginPath();
                fNvg.moveTo(LANE_X + LANE_W - 40, y);
                fNvg.lineTo(LANE_X + LANE_W, y);
                fNvg.strokeColor(col(C_TEXT, 90));
                fNvg.stroke();
                fNvg.text(LANE_X + LANE_W - 4, y + 1, buf, nullptr);
                const int fields = 1 + c.gainFields() + 2 * c.tracks * c.pulsesPerTrack;
                for (int k = 0; k < fields; ++k) bit += f[field++].bits;
            }
        } else {
            fNvg.fillColor(col(C_TEXT, 200));
            fNvg.text(LANE_X + LANE_W - 4, BITS_Y + 1, "FEC-coded slot", nullptr);
        }
    }
}

void DistrhoUICheeExcitation::drawEnvelope(float x0, float y0, float w, float h)
{
    drawPanel(x0, y0, w, h);
    const LpcVizFrame& f = fFrame;
    const float px = x0 + 6, pw = w - 12, py = y0 + 18, ph = h - 32;
    fNvg.fontSize(9.5f);
    fNvg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_TOP);
    fNvg.fillColor(col(C_TEXT, 220));
    char buf[64];
    if (!fHaveFrame) {
        fNvg.text(x0 + 8, y0 + 4, "CURRENT ENVELOPE  1/A(z)", nullptr);
        return;
    }
    std::snprintf(buf, sizeof(buf), "CURRENT ENVELOPE  1/A(z)  ·  order %d%s", f.order, f.estimated ? "  (est.)" : "");
    fNvg.text(x0 + 8, y0 + 4, buf, nullptr);
    if (f.modActive) {
        fNvg.textAlign(NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_TOP);
        fNvg.fillColor(col(C_AMBER));
        fNvg.text(x0 + w - 8, y0 + 4, "LP MOD", nullptr);
    }

    const double nyq = f.fs * 0.5;
    float top = -1e9f;
    for (int i = 0; i < LPC_VIZ_BINS; ++i) {
        top = std::fmax(top, f.envRef[i]);
        if (f.modActive) top = std::fmax(top, f.envMod[i]);
    }
    top += 3.0f;
    const float RANGE = 54.0f;
    auto yOf = [&](float db) { return py + std::fmin(1.0f, std::fmax(0.0f, (top - db) / RANGE)) * ph; };
    auto xOfBin = [&](int i) { return px + (i + 0.5f) / LPC_VIZ_BINS * pw; };
    auto xOfHz = [&](double hz) { return px + (float)lpc_viz_x(hz, nyq) * pw; };

    static const int marks[] = { 100, 250, 500, 1000, 2000, 4000, 8000, 16000 };
    static const char* const names[] = { "100", "250", "500", "1k", "2k", "4k", "8k", "16k" };
    fNvg.fontSize(8);
    fNvg.textAlign(NanoVG::ALIGN_CENTER | NanoVG::ALIGN_TOP);
    for (int m = 0; m < 8 && marks[m] < nyq * 0.97; ++m) {
        const float x = xOfHz(marks[m]);
        fNvg.beginPath();
        fNvg.moveTo(x, py);
        fNvg.lineTo(x, py + ph);
        fNvg.strokeColor(Color(255, 255, 255, 0.06f));
        fNvg.stroke();
        fNvg.fillColor(col(C_MUTED));
        fNvg.text(x, py + ph + 2, names[m], nullptr);
    }

    fNvg.beginPath();
    fNvg.moveTo(px, py + ph);
    for (int i = 0; i < LPC_VIZ_BINS; ++i) fNvg.lineTo(xOfBin(i), yOf(f.envRef[i]));
    fNvg.lineTo(px + pw, py + ph);
    fNvg.closePath();
    fNvg.fillPaint(fNvg.linearGradient(0, py, 0, py + ph, col(C_GREEN, 110), col(C_GREEN, 10)));
    fNvg.fill();
    auto curve = [&](const float* env, const RGB& c) {
        fNvg.beginPath();
        for (int i = 0; i < LPC_VIZ_BINS; ++i) {
            if (i == 0) fNvg.moveTo(xOfBin(i), yOf(env[i]));
            else        fNvg.lineTo(xOfBin(i), yOf(env[i]));
        }
        fNvg.strokeColor(col(c));
        fNvg.strokeWidth(1.6f);
        fNvg.stroke();
    };
    curve(f.envRef, C_GREEN);
    if (f.modActive) curve(f.envMod, C_AMBER);
    fNvg.strokeWidth(1.0f);
    for (int i = 0; i < f.order && i < LPC_VIZ_MAX_ORDER; ++i) {
        fNvg.beginPath();
        fNvg.moveTo(xOfHz(f.lsfRef[i]), py + ph - 5);
        fNvg.lineTo(xOfHz(f.lsfRef[i]), py + ph);
        fNvg.strokeColor(col(C_GREEN, 220));
        fNvg.stroke();
        if (f.modActive) {
            fNvg.beginPath();
            fNvg.moveTo(xOfHz(f.lsfMod[i]), py + ph - 11);
            fNvg.lineTo(xOfHz(f.lsfMod[i]), py + ph - 6);
            fNvg.strokeColor(col(C_AMBER, 220));
            fNvg.stroke();
        }
    }
}

void DistrhoUICheeExcitation::drawExcitation(float x0, float y0, float w, float h)
{
    drawPanel(x0, y0, w, h);
    fNvg.fontSize(9.5f);
    fNvg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_TOP);
    fNvg.fillColor(col(C_TEXT, 220));
    if (!fHaveFrame) {
        fNvg.text(x0 + 8, y0 + 4, "CURRENT EXCITATION", nullptr);
        return;
    }
    const LpcVizFrame& f = fFrame;
    fNvg.text(x0 + 8, y0 + 4, f.estimated ? "CURRENT EXCITATION  ·  LP residual (est.)"
                                          : "CURRENT EXCITATION  ·  pitch (green) + pulses (amber)", nullptr);
    const float px = x0 + 6, pw = w - 12, py = y0 + 30, ph = h - 36;
    const int N = f.frameLen > 0 && f.frameLen <= LPC_VIZ_MAX_FRAME ? f.frameLen : 1;
    float peak = 1e-9f, peakF = 0.0f;
    for (int n = 0; n < N; ++n) {
        peak = std::fmax(peak, std::fabs(f.excAdaptive[n] + f.excFixed[n]));
        peak = std::fmax(peak, std::fabs(f.excAdaptive[n]));
        peakF = std::fmax(peakF, std::fabs(f.excFixed[n]));
    }
    const float cy = py + ph * 0.5f, amp = (ph * 0.5f - 2.0f) / peak;

    fNvg.beginPath();
    fNvg.moveTo(px, cy);
    fNvg.lineTo(px + pw, cy);
    for (int s = 1; s < LPC_VIZ_NSUB; ++s) {
        fNvg.moveTo(px + pw * s / LPC_VIZ_NSUB, py - 12);
        fNvg.lineTo(px + pw * s / LPC_VIZ_NSUB, py + ph);
    }
    fNvg.strokeColor(Color(255, 255, 255, 0.09f));
    fNvg.stroke();

    fNvg.beginPath();
    const int cols = (int)pw;
    for (int c = 0; c < cols; ++c) {
        const int n0 = c * N / cols, n1 = std::max(n0 + 1, (c + 1) * N / cols);
        float lo = 0.0f, hi = 0.0f;
        for (int n = n0; n < n1 && n < N; ++n) {
            lo = std::fmin(lo, f.excAdaptive[n]);
            hi = std::fmax(hi, f.excAdaptive[n]);
        }
        fNvg.moveTo(px + c + 0.5f, cy - hi * amp);
        fNvg.lineTo(px + c + 0.5f, cy - lo * amp + 0.5f);
    }
    fNvg.strokeColor(col(C_GREEN, 230));
    fNvg.stroke();

    if (peakF > 0.0f) {
        fNvg.beginPath();
        for (int n = 0; n < N; ++n) {
            if (std::fabs(f.excFixed[n]) < 0.08f * peakF) continue;
            const float x = px + (n + 0.5f) / N * pw;
            fNvg.moveTo(x, cy);
            fNvg.lineTo(x, cy - f.excFixed[n] * amp);
        }
        fNvg.strokeColor(col(C_AMBER, 230));
        fNvg.stroke();
    }

    fNvg.fontSize(8.5f);
    fNvg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_TOP);
    for (int s = 0; s < LPC_VIZ_NSUB; ++s) {
        char buf[32];
        if (f.pitch[s] > 0) std::snprintf(buf, sizeof(buf), "%.0f Hz  g%.2f", (double)f.fs / f.pitch[s], (double)f.gp[s]);
        else                std::snprintf(buf, sizeof(buf), "unvoiced");
        fNvg.fillColor(col(C_MUTED));
        fNvg.text(px + pw * s / LPC_VIZ_NSUB + 3, py - 12, buf, nullptr);
    }
}

void DistrhoUICheeExcitation::drawAnatomy(float x0, float y0, float w, float h)
{
    drawPanel(x0, y0, w, h);
    fNvg.fontSize(9.5f);
    fNvg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_TOP);
    fNvg.fillColor(col(C_TEXT, 220));
    char buf[80];
    if (fHaveRec) std::snprintf(buf, sizeof(buf), "FRAME ANATOMY  ·  %d bits  ·  %d corrupted", fLast.nbits, fLast.nflipped);
    else          std::snprintf(buf, sizeof(buf), "FRAME ANATOMY");
    fNvg.text(x0 + 8, y0 + 4, buf, nullptr);
    if (!fHaveRec || fLast.nbits == 0) return;

    const float gx = x0 + 8, gy = y0 + 18, gw = w - 16, gh = h - 40;
    const int n = fLast.nbits;
    int cols = 1;
    while (cols * cols * gh < n * gw) ++cols;
    const int rows = (n + cols - 1) / cols;
    const float cw = gw / cols, ch = std::fmin(gh / rows, cw * 1.6f);
    const float inset = cw > 4 ? 0.5f : 0.0f;
    for (int b = 0; b < n; ++b) {
        const uint8_t v = fLast.bits[b];
        const RGB& c = BIT_COL[clampi((v >> 1) & 7, 0, kVizBitCount - 1)];
        const bool one = v & 1, bad = (v >> 4) & 1;
        fNvg.beginPath();
        fNvg.rect(gx + (b % cols) * cw + inset, gy + (b / cols) * ch + inset, cw - 2 * inset, ch - 2 * inset);
        if (bad)      fNvg.fillColor(col(C_RED));
        else if (one) fNvg.fillColor(col(c));
        else          fNvg.fillColor(col(c, 55));
        fNvg.fill();
    }

    // legend
    fNvg.fontSize(8.5f);
    fNvg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE);
    float lx = gx;
    const float ly = y0 + h - 11;
    const int cats[] = { kVizBitLsf, kVizBitPitch, kVizBitGain, kVizBitPulse, kVizBitSign, kVizBitFec, kVizBitFec0 };
    for (int cat : cats) {
        const bool chanCat = cat == kVizBitFec || cat == kVizBitFec0;
        if (fLast.kind == 1 && chanCat) continue;
        if (fLast.kind == 0 && cat != kVizBitFec) continue;
        if (fLast.kind == 2 && !chanCat && cat != kVizBitPulse) continue;
        const char* name = (fLast.kind == 2 && cat == kVizBitPulse) ? "raw" : (fLast.kind == 0 ? "FEC" : BIT_NAME[cat]);
        fNvg.beginPath();
        fNvg.rect(lx, ly - 4, 8, 8);
        fNvg.fillColor(col(BIT_COL[cat]));
        fNvg.fill();
        fNvg.fillColor(col(C_MUTED));
        fNvg.text(lx + 11, ly, name, nullptr);
        DGL_NAMESPACE::Rectangle<float> b;
        fNvg.textBounds(0, 0, name, nullptr, b);
        lx += b.getWidth() + 22;
    }
    fNvg.beginPath();
    fNvg.rect(lx, ly - 4, 8, 8);
    fNvg.fillColor(col(C_RED));
    fNvg.fill();
    fNvg.fillColor(col(C_MUTED));
    fNvg.text(lx + 11, ly, "corrupted", nullptr);
}

void DistrhoUICheeExcitation::drawKnob(const Control& c, int idx)
{
    PluginACELP* p = plugin();
    const uint32_t ep = effParam(c);
    const bool decSide = ep != c.param;
    const RGB& gc = GROUP_COL[c.group];
    float lo, hi;
    controlRange(c, lo, hi);
    const float v = std::fmin(hi, getVal(ep));
    const float t = hi > lo ? (v - lo) / (hi - lo) : 0.0f;
    const float cx = c.x + c.w / 2, cy = c.y + 40, r = 17;
    const float a0 = 0.75f * (float)M_PI, a1 = 2.25f * (float)M_PI, a = a0 + t * (a1 - a0);
    const bool active = fDrag == idx;

    fNvg.fontSize(9.5f);
    fNvg.textAlign(NanoVG::ALIGN_CENTER | NanoVG::ALIGN_TOP);
    fNvg.fillColor(col(active ? gc : (decSide ? C_AMBER : C_MUTED)));
    fNvg.text(cx, c.y, c.label, nullptr);

    // track + value arc
    fNvg.lineCap(NanoVG::ROUND);
    fNvg.beginPath();
    fNvg.arc(cx, cy, r + 5, a0, a1, NanoVG::CW);
    fNvg.strokeColor(Color(28, 50, 38));
    fNvg.strokeWidth(3.5f);
    fNvg.stroke();
    const float start = c.bipolar ? 1.5f * (float)M_PI : a0;
    if (std::fabs(a - start) > 0.01f) {
        fNvg.beginPath();
        fNvg.arc(cx, cy, r + 5, std::fmin(start, a), std::fmax(start, a), NanoVG::CW);
        fNvg.strokeColor(col(decSide ? C_AMBER : gc));
        fNvg.strokeWidth(3.5f);
        fNvg.stroke();
    }
    // body
    fNvg.beginPath();
    fNvg.circle(cx, cy, r);
    fNvg.fillPaint(fNvg.radialGradient(cx - 4, cy - 6, 2, r + 2, Color(44, 74, 56), Color(13, 25, 18)));
    fNvg.fill();
    fNvg.strokeColor(col(active ? gc : C_BORDER));
    fNvg.strokeWidth(1.2f);
    fNvg.stroke();
    // pointer
    fNvg.beginPath();
    fNvg.moveTo(cx + std::cos(a) * r * 0.3f, cy + std::sin(a) * r * 0.3f);
    fNvg.lineTo(cx + std::cos(a) * (r - 3), cy + std::sin(a) * (r - 3));
    fNvg.strokeColor(col(C_TEXT));
    fNvg.strokeWidth(2.2f);
    fNvg.stroke();
    fNvg.lineCap(NanoVG::BUTT);
    fNvg.strokeWidth(1.0f);

    char buf[32];
    formatValue(c, buf, sizeof(buf));
    fNvg.fontSize(10.5f);
    fNvg.fillColor(col(C_TEXT));
    fNvg.text(cx, cy + r + 9, buf, nullptr);

    // MIDI learn: armed = pulsing ring, mapped = CC tag
    if (p != nullptr && ep < (uint32_t)paramCount) {
        if (p->learnArmed.load() == (int)ep) {
            const int al = 120 + (int)(135 * (0.5 + 0.5 * std::sin(fTime * 8.0)));
            fNvg.beginPath();
            fNvg.circle(cx, cy, r + 10);
            fNvg.strokeColor(col(C_AMBER, al));
            fNvg.strokeWidth(2.0f);
            fNvg.stroke();
            fNvg.strokeWidth(1.0f);
        }
        const int cc = p->ccForParam(ep);
        if (cc >= 0) {
            std::snprintf(buf, sizeof(buf), "CC %d", cc);
            fNvg.fontSize(8.5f);
            fNvg.fillColor(col(C_CYAN));
            fNvg.text(cx, cy + r + 23, buf, nullptr);
        }
    }
}

void DistrhoUICheeExcitation::drawSegment(const Control& c, int)
{
    PluginACELP* p = plugin();
    const uint32_t ep = effParam(c);
    const bool decSide = ep != c.param;
    const RGB& gc = (c.param >= kEditCodec && c.param <= kEditAlgo) ? C_AMBER : GROUP_COL[c.group];
    // (kPageLp uses the group colour)
    float gx, gy, gw, gh;
    segRect(c, gx, gy, gw, gh);
    if (!c.mini) {
        fNvg.fontSize(9.5f);
        fNvg.textAlign(NanoVG::ALIGN_CENTER | NanoVG::ALIGN_TOP);
        fNvg.fillColor(col(decSide ? C_AMBER : C_MUTED));
        fNvg.text(c.x + c.w / 2, c.y, c.label, nullptr);
    }
    const int cols = c.segCols, rows = (c.nseg + cols - 1) / cols;
    const float cw = gw / cols, ch = gh / rows;
    const int sel = clampi((int)std::lround(getVal(ep)), 0, c.nseg - 1);
    const float pad = c.mini ? 1.0f : 1.5f;
    for (int s = 0; s < c.nseg; ++s) {
        const float x = gx + (s % cols) * cw + pad, y = gy + (s / cols) * ch + pad;
        fNvg.beginPath();
        fNvg.roundedRect(x, y, cw - 2 * pad, ch - 2 * pad, c.mini ? 3 : 4);
        fNvg.fillColor(s == sel ? col(gc, 60) : col(C_PANEL2));
        fNvg.fill();
        fNvg.strokeColor(s == sel ? col(gc) : col(C_BORDER));
        fNvg.strokeWidth(1.0f);
        fNvg.stroke();
        fNvg.fontSize(c.mini ? 8.0f : (ch < 24 ? 9.5f : 10.5f));
        fNvg.textAlign(NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE);
        fNvg.fillColor(s == sel ? col(C_TEXT) : col(C_MUTED));
        fNvg.text(x + (cw - 2 * pad) / 2, y + (ch - 2 * pad) / 2 + 1, c.segNames[s], nullptr);
    }
    if (p != nullptr && !c.mini && ep < (uint32_t)paramCount) {
        char buf[16];
        fNvg.textAlign(NanoVG::ALIGN_CENTER | NanoVG::ALIGN_TOP);
        fNvg.fontSize(8.5f);
        if (p->learnArmed.load() == (int)ep) {
            const int al = 120 + (int)(135 * (0.5 + 0.5 * std::sin(fTime * 8.0)));
            fNvg.fillColor(col(C_AMBER, al));
            fNvg.text(c.x + c.w / 2, gy + gh + 4, "LEARN", nullptr);
        } else {
            const int cc = p->ccForParam(ep);
            if (cc >= 0) {
                std::snprintf(buf, sizeof(buf), "CC %d", cc);
                fNvg.fillColor(col(C_CYAN));
                fNvg.text(c.x + c.w / 2, gy + gh + 4, buf, nullptr);
            }
        }
    }
}

void DistrhoUICheeExcitation::drawMidiPanel(float x0, float y0, float w, float h)
{
    PluginACELP* p = plugin();
    if (p == nullptr) return;
    CheetahDSP& dsp = p->getDSP();
    (void)w;

    // per-key envelope meters (C3..B3), labels rotated underneath
    const float mx = x0 + 12, my = y0 + 22, mh = h - 70, bw = 11, gap = 3;
    for (int k = 0; k < CheetahDSP::MIDI_CORRUPT_KEYS; ++k) {
        const float lvl = dsp.keyLevel[k].load(std::memory_order_relaxed) / 255.0f;
        const float x = mx + k * (bw + gap);
        const RGB& c = k < 10 ? C_RED : C_AMBER;
        fNvg.beginPath();
        fNvg.roundedRect(x, my, bw, mh, 2);
        fNvg.fillColor(col(C_PANEL2));
        fNvg.fill();
        if (lvl > 0.0f) {
            fNvg.beginPath();
            fNvg.roundedRect(x, my + mh * (1.0f - lvl), bw, mh * lvl, 2);
            fNvg.fillColor(col(c, 220));
            fNvg.fill();
        }
        fNvg.save();
        fNvg.translate(x + bw / 2 + 3, my + mh + 4);
        fNvg.rotate(-1.5707963f);
        fNvg.fontSize(7.5f);
        fNvg.textAlign(NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE);
        fNvg.fillColor(col(lvl > 0.0f ? C_TEXT : C_MUTED));
        fNvg.text(0, 0, KEY_NAMES[k], nullptr);
        fNvg.restore();
    }
    // header: aftertouch + learned CCs
    int mapped = 0;
    for (int cc = 0; cc < 128; ++cc) mapped += p->ccMap[cc].load(std::memory_order_relaxed) >= 0;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "AT %d  ·  %d CC", dsp.midiPressure.load(std::memory_order_relaxed), mapped);
    fNvg.fontSize(8.5f);
    fNvg.textAlign(NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_TOP);
    fNvg.fillColor(col(C_MUTED));
    fNvg.text(x0 + w - 8, y0 + 6, buf, nullptr);
}

void DistrhoUICheeExcitation::drawNetStatus(float x0, float y0, float w, float h)
{
    const CodecVizRecord& r = fLast;
    const int mode = clampi((int)std::lround(fValues[paramNetMode]), 0, 3);
    char buf[96];
    RGB c = C_MUTED;
    const bool live = fHaveRec && r.netMode == mode && mode != 0;
    if (mode == 0) {
        std::snprintf(buf, sizeof(buf), "off  ·  LAN multicast, 16 channels");
    } else if (!live) {
        std::snprintf(buf, sizeof(buf), typeIndex(fValues[paramCodecType]) == 0 && mode != 2
                      ? "HD / T+ codecs only" : "starting...");
    } else if (!r.netOk) {
        std::snprintf(buf, sizeof(buf), "socket error (no multicast route?)");
        c = C_RED;
    } else if (mode == 1) {
        std::snprintf(buf, sizeof(buf), "TX  ·  stream %08x  ·  %u pkts", r.netStream, r.netTx);
        c = C_TEAL;
    } else {
        if (!r.netPlaying) std::snprintf(buf, sizeof(buf), "RX  ·  waiting for a stream on ch %d", r.netChannel);
        else std::snprintf(buf, sizeof(buf), "RX %08x  ·  %s  ·  buf %d  ·  loss %.0f%%", r.netStream,
                           r.kind == 2 ? (r.fs == 8000 ? "T+8" : "T+16") : (r.fs == 16000 ? "HD16" : r.fs == 32000 ? "HD32" : "HD48"),
                           r.netDepth, r.netLossPct);
        c = r.netPlaying ? (r.netConceal ? C_AMBER : C_TEAL) : C_MUTED;
    }
    (void)w;
    fNvg.fontSize(9.0f);
    fNvg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_BOTTOM);
    fNvg.fillColor(col(c));
    fNvg.text(x0 + 8, y0 + h - 5, buf, nullptr);
}

void DistrhoUICheeExcitation::drawControls()
{
    for (int g = 0; g < kNumGroups; ++g) {
        const float* r = GROUP_RECT[g];
        drawPanel(r[0], r[1], r[2], r[3]);
        fNvg.fontSize(10);
        fNvg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_TOP);
        fNvg.fillColor(col(GROUP_COL[g]));
        fNvg.text(r[0] + 8, r[1] + 5, GROUP_NAME[g], nullptr);
    }
    const bool hd = typeIndex(fValues[paramCodecType]) != 0;
    if (!hd) {
        fNvg.fontSize(9);
        fNvg.textAlign(NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_TOP);
        fNvg.fillColor(col(C_MUTED));
        const int dims[3] = { kGroupLp, kGroupExc, kGroupAlgo };
        for (int g : dims)
            fNvg.text(GROUP_RECT[g][0] + GROUP_RECT[g][2] - 8, GROUP_RECT[g][1] + 6, "HD only", nullptr);
    }
    for (size_t i = 0; i < fControls.size(); ++i) {
        const Control& c = fControls[i];
        if (!controlVisible(c)) continue;
        // mini switches sit in the header; hide them on TETRA (nothing to split)
        if (c.mini && !hd) continue;
        fNvg.globalAlpha(controlEnabled(c) ? 1.0f : 0.35f);
        if (c.kind == kKnob) drawKnob(c, (int)i);
        else                 drawSegment(c, (int)i);
    }
    fNvg.globalAlpha(1.0f);
    drawMidiPanel(GROUP_RECT[kGroupMidi][0], GROUP_RECT[kGroupMidi][1], GROUP_RECT[kGroupMidi][2], GROUP_RECT[kGroupMidi][3]);
    drawNetStatus(GROUP_RECT[kGroupNet][0], GROUP_RECT[kGroupNet][1], GROUP_RECT[kGroupNet][2], GROUP_RECT[kGroupNet][3]);
}

void DistrhoUICheeExcitation::onDisplay()
{
    fNvg.beginFrame(this);

    fNvg.beginPath();
    fNvg.rect(0, 0, UI_W, UI_H);
    fNvg.fillColor(col(C_BG));
    fNvg.fill();

    drawHeader();
    drawChain();
    drawLanes();
    drawEnvelope(SIDE_X, ENV_Y, SIDE_W, ENV_H);
    drawExcitation(SIDE_X, EXC_Y, SIDE_W, EXC_H);
    drawAnatomy(SIDE_X, ANAT_Y, SIDE_W, ANAT_H);
    drawControls();

    fNvg.endFrame();
}

// -----------------------------------------------------------------------

UI* createUI()
{
    return new DistrhoUICheeExcitation();
}

// -----------------------------------------------------------------------

END_NAMESPACE_DISTRHO

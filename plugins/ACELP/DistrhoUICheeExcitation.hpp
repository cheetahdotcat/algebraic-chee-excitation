/*
 * Algebraic Chee Excitation — plugin UI.
 *
 * Everything is drawn with NanoVG: header, the live encode -> decode chain,
 * scrolling codec history (envelope spectrogram, pitch/gains, algebraic pulses,
 * bitstream map), current-frame panels and the controls (knobs + segmented
 * switches, with MIDI CC learn on right-click).
 *
 * Codec data comes from the plugin instance through DPF direct access
 * (CheetahDSP::vizFeed / lpcViz).
 */

#ifndef DISTRHO_UI_CHEE_EXCITATION_HPP_INCLUDED
#define DISTRHO_UI_CHEE_EXCITATION_HPP_INCLUDED

#include "DistrhoUI.hpp"
#include "NanoVG.hpp"
#include "ImageWidgets.hpp"
#include "DistrhoPluginInfo.h"
#include "lpc_viz.hpp"
#include "viz_feed.hpp"

#include <vector>

using DGL_NAMESPACE::ImageAboutWindow;

START_NAMESPACE_DISTRHO

class PluginACELP;

// -----------------------------------------------------------------------

class DistrhoUICheeExcitation : public UI,
                                public IdleCallback
{
public:
    DistrhoUICheeExcitation();
    ~DistrhoUICheeExcitation() override;

protected:
    // DSP callbacks
    void parameterChanged(uint32_t index, float value) override;
    void stateChanged(const char* key, const char* value) override;
    void programLoaded(uint32_t) override {}

    // Widget events
    void onDisplay() override;
    bool onMouse(const MouseEvent& ev) override;
    bool onMotion(const MotionEvent& ev) override;
    bool onScroll(const ScrollEvent& ev) override;

    void idleCallback() override;

private:
    // ---- controls -----------------------------------------------------
    enum ControlKind { kKnob, kSegment };
    // Groups: 0 codec, 1 corruption, 2 LP, 3 excitation, 4 algorithm, 5 MIDI
    enum { kGroupCodec, kGroupCorrupt, kGroupLp, kGroupExc, kGroupAlgo, kGroupNet, kGroupMidi, kNumGroups };
    // UI-only "parameters": which side (0 ENC, 1 DEC) a split group edits
    enum { kEditCodec = 1000, kEditLp, kEditAlgo, kPageLp };
    static const uint32_t kNoParam = 0xFFFFFFFFu;

    struct Control {
        uint32_t param;          // parameter (or kEdit* UI state)
        uint32_t paramDec;       // decoder-side parameter when the group is split
        const char* label;
        ControlKind kind;
        int group;
        float x, y, w, h;        // hit rect
        int nseg;                // segments (kSegment)
        const char* const* segNames;
        int segCols;             // segment grid columns
        bool bipolar;            // knob arc drawn from the centre
        bool mini;               // small header switch
        int page;                // 0 always, 1 LP ENV page, 2 LP LATTICE page
    };
    std::vector<Control> fControls;
    float fValues[paramCount];
    int   fEditSide[3];          // kEditCodec/Lp/Algo: 0 = ENC, 1 = DEC

    bool     groupSplit(int group) const;
    bool     controlVisible(const Control& c) const;
    uint32_t effParam(const Control& c) const;   // param actually edited
    float    getVal(uint32_t p) const;
    void     segRect(const Control& c, float& x, float& y, float& w, float& h) const;

    int   fDrag;                 // control index being dragged, -1 none
    float fDragStartY, fDragStartValue;
    int   fLastClickControl;
    uint  fLastClickTime;

    void buildControls();
    int  hitControl(double x, double y) const;
    void controlRange(const Control& c, float& lo, float& hi) const;
    void setControlValue(const Control& c, float v, bool notify);
    void drawMidiPanel(float x, float y, float w, float h);
    void resetToDefaults();
    void drawNetStatus(float x, float y, float w, float h);
    double fResetArmedUntil;     // RESET armed until this fTime
    int    fLpPage;              // LP group page: 0 ENV, 1 LATTICE
    int    fBuildPage;           // page assigned to controls while building
    void formatValue(const Control& c, char* buf, size_t n) const;
    bool controlEnabled(const Control& c) const;

    // ---- codec data -----------------------------------------------------
    PluginACELP* plugin() const;
    void drainFeed();
    void scrollLanes(int px);
    void paintLaneColumn(const CodecVizRecord& r, int px);

    uint32_t       fFeedNext;     // next record number to read
    bool           fHaveRec;
    CodecVizRecord fLast;         // latest record (chain, anatomy, header)
    LpcVizFrame    fFrame;        // latest current-frame data
    bool           fHaveFrame;

    struct Lane {
        int w, h;
        std::vector<unsigned char> rgba;
        NanoImage image;
        int imageId;
        bool dirty;
    };
    Lane fLaneSpec, fLanePitch, fLanePulse, fLaneBits;
    void initLane(Lane& l, int w, int h);
    void uploadLane(Lane& l);
    void drawLane(Lane& l, float x, float y);

    // ---- drawing --------------------------------------------------------
    NanoVG fNvg;
    double fTime;                 // seconds, for animations
    void drawPanel(float x, float y, float w, float h, const char* title = nullptr);
    void drawHeader();
    void drawChain();
    void drawLanes();
    void drawEnvelope(float x, float y, float w, float h);
    void drawExcitation(float x, float y, float w, float h);
    void drawAnatomy(float x, float y, float w, float h);
    void drawControls();
    void drawKnob(const Control& c, int idx);
    void drawSegment(const Control& c, int idx);

    ImageAboutWindow fAboutWindow;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistrhoUICheeExcitation)
};

// -----------------------------------------------------------------------

END_NAMESPACE_DISTRHO

#endif // DISTRHO_UI_CHEE_EXCITATION_HPP_INCLUDED

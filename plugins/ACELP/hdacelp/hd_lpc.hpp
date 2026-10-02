// HD ACELP - linear prediction toolkit (floating point, order <= HD_MAX_ORDER)
//
// Conventions: A(z) = 1 + a[1] z^-1 + ... + a[M] z^-M, stored as a[0..M] with
// a[0] == 1. Line spectral frequencies are normalized angular frequencies in
// (0, pi), ascending.
#ifndef HD_LPC_HPP_INCLUDED
#define HD_LPC_HPP_INCLUDED

namespace hdacelp {

static const int HD_MAX_ORDER = 32;

// r[0..M] from x[0..n-1] * win[0..n-1].
void autocorr(const float *x, const float *win, int n, int M, double *r);

// Gaussian lag window (bandwidth f0 Hz at sample rate fs) + white-noise
// correction, in place.
void lag_window(double *r, int M, double fs, double f0 = 60.0, double wnc = 1.0001);

// Levinson-Durbin. Returns false (and leaves a untouched) if unstable.
bool levinson(const double *r, int M, double *a);

// LPC <-> LSF. a2lsf returns false when it cannot find M interlaced roots.
bool a2lsf(const double *a, int M, double *lsf);
void lsf2a(const double *lsf, int M, double *a);

// True when all reflection coefficients of A(z) are inside the unit circle.
bool is_stable(const double *a, int M);

// lsf2a() plus a numerical safety net: tightly clustered LSFs at high orders
// give ill-conditioned polynomials whose rounding errors can push poles out of
// the unit circle, so progressively bandwidth-expand until A(z) is stable.
void lsf2a_stable(const double *lsf, int M, double *a);

// Step-down: reduce A(z) from order M to order N (N <= M) through the
// reflection coefficients. Fractional orders blend in the next reflection
// coefficient, so the result is always stable for a stable input.
bool truncate_order(const double *a, int M, double order, double *out);

// Lattice (reflection-coefficient) shaping of A(z). Steps, in order:
//   order cut   coefficients above `order` removed (fractional = partial)
//   taper       soft fade towards the cut instead of a cliff (0 = hard,
//               1 = linear fade over the whole kept range)
//   band        the lowest `band` coefficients removed (fractional = partial):
//               keeps the fine formant structure without the coarse tilt
//   resonance   scale in log-area-ratio space (<1 broader, >1 peakier);
//               tanh keeps every |k| < 1, so the result is always stable
//   jitter      per-coefficient offsets in log-area-ratio space (may be null)
struct LatticeShape {
    double order = 1e9;
    double taper = 0.0;
    double band = 0.0;
    double resonance = 1.0;
    const double *jitter = nullptr;   // [1..M]
};
bool shape_lattice(const double *a, int M, const LatticeShape &s, double *out);

// Sort, clamp to (minGap, pi - minGap) and enforce minimum spacing.
void lsf_stabilize(double *lsf, int M, double minGap);

// Energy of the first n samples of the impulse response of 1/A(z).
double synth_energy(const double *a, int M, int n);

// 10*log10 |1/A(e^jw)|^2 at the n normalized angular frequencies w[].
void lpc_response_db(const double *a, int M, const double *w, int n, float *out);

// y[i] = a[i] * g^i
void weight_lpc(const double *a, int M, double g, double *out);

} // namespace hdacelp

#endif

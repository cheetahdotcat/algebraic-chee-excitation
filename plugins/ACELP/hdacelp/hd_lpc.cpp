// The plugin Makefile builds with -O0 for debugging; the codec is far too heavy
// for that at 32/48 kHz, so always optimize this translation unit.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif

#include "hd_lpc.hpp"
#include <math.h>
#include <string.h>

namespace hdacelp {

void autocorr(const float *x, const float *win, int n, int M, double *r) {
    double xw[4096];
    if (n > 4096) n = 4096;
    for (int i = 0; i < n; i++) xw[i] = (double)x[i] * (double)win[i];
    for (int k = 0; k <= M; k++) {
        double s = 0.0;
        for (int i = k; i < n; i++) s += xw[i] * xw[i - k];
        r[k] = s;
    }
}

void lag_window(double *r, int M, double fs, double f0, double wnc) {
    r[0] = r[0] * wnc + 1e-10; // noise floor keeps silence well-conditioned
    for (int k = 1; k <= M; k++) {
        const double t = 2.0 * M_PI * f0 * k / fs;
        r[k] *= exp(-0.5 * t * t);
    }
}

bool levinson(const double *r, int M, double *a) {
    double tmp[HD_MAX_ORDER + 1], cur[HD_MAX_ORDER + 1];
    cur[0] = 1.0;
    for (int i = 1; i <= M; i++) cur[i] = 0.0;
    double err = r[0];
    if (err <= 0.0) return false;
    for (int m = 1; m <= M; m++) {
        double acc = r[m];
        for (int i = 1; i < m; i++) acc += cur[i] * r[m - i];
        const double k = -acc / err;
        if (!(fabs(k) < 0.9999)) return false;
        memcpy(tmp, cur, sizeof(double) * (m + 1));
        for (int i = 1; i < m; i++) cur[i] = tmp[i] + k * tmp[m - i];
        cur[m] = k;
        err *= (1.0 - k * k);
    }
    memcpy(a, cur, sizeof(double) * (M + 1));
    return true;
}

// Evaluate sum_{n=0}^{N} c[n] T_n(x) (Clenshaw).
static double cheb_eval(const double *c, int N, double x) {
    double b1 = 0.0, b2 = 0.0;
    for (int n = N; n >= 1; n--) {
        const double b0 = 2.0 * x * b1 - b2 + c[n];
        b2 = b1;
        b1 = b0;
    }
    return x * b1 - b2 + c[0];
}

bool a2lsf(const double *a, int M, double *lsf) {
    // Symmetric P'(z) and antisymmetric Q'(z) with the trivial roots at z=-1
    // and z=+1 divided out. Both are order M, symmetric, so each can be written
    // as a cosine series in w with M/2 + 1 terms.
    const int H = M / 2;
    double p[HD_MAX_ORDER + 2], q[HD_MAX_ORDER + 2];
    double P[HD_MAX_ORDER + 2], Q[HD_MAX_ORDER + 2];
    for (int i = 0; i <= M + 1; i++) {
        const double ai  = (i <= M) ? a[i] : 0.0;
        const double ari = (M + 1 - i <= M) ? a[M + 1 - i] : 0.0;
        P[i] = ai + ari;
        Q[i] = ai - ari;
    }
    // Deflate: P(z) = P'(z)(1 + z^-1), Q(z) = Q'(z)(1 - z^-1)
    p[0] = P[0];
    q[0] = Q[0];
    for (int i = 1; i <= M; i++) {
        p[i] = P[i] - p[i - 1];
        q[i] = Q[i] + q[i - 1];
    }
    // P'(w) = 2 e^{-jHw} [ sum_{k=0}^{H-1} p[k] cos((H-k)w) + p[H]/2 ]
    double cp[HD_MAX_ORDER / 2 + 1], cq[HD_MAX_ORDER / 2 + 1];
    for (int n = 1; n <= H; n++) {
        cp[n] = p[H - n];
        cq[n] = q[H - n];
    }
    cp[0] = 0.5 * p[H];
    cq[0] = 0.5 * q[H];

    const int GRID = 2048;
    int found = 0;
    // Roots of P' and Q' interlace, starting with P'. Scan w from 0 to pi and
    // alternate between the two polynomials.
    const double *coef[2] = { cp, cq };
    int which = 0;
    double xPrev = 1.0;
    double fPrev = cheb_eval(coef[which], H, xPrev);
    for (int g = 1; g <= GRID && found < M; g++) {
        const double w = M_PI * g / GRID;
        const double x = cos(w);
        const double f = cheb_eval(coef[which], H, x);
        if ((fPrev <= 0.0 && f > 0.0) || (fPrev >= 0.0 && f < 0.0)) {
            double lo = xPrev, hi = x, flo = fPrev;
            for (int it = 0; it < 30; it++) {
                const double mid = 0.5 * (lo + hi);
                const double fm = cheb_eval(coef[which], H, mid);
                if ((flo <= 0.0 && fm <= 0.0) || (flo > 0.0 && fm > 0.0)) {
                    lo = mid;
                    flo = fm;
                } else {
                    hi = mid;
                }
            }
            const double xr = 0.5 * (lo + hi);
            lsf[found++] = acos(xr);
            which ^= 1;
            xPrev = xr;
            fPrev = cheb_eval(coef[which], H, xPrev);
            // continue scanning from the root towards the current grid point
            const double f2 = cheb_eval(coef[which], H, x);
            if ((fPrev <= 0.0 && f2 > 0.0) || (fPrev >= 0.0 && f2 < 0.0)) {
                g--; // re-examine this cell for the other polynomial
                continue;
            }
            xPrev = x;
            fPrev = f2;
        } else {
            xPrev = x;
            fPrev = f;
        }
    }
    return found == M;
}

void lsf2a(const double *lsf, int M, double *a) {
    const int H = M / 2;
    double p[HD_MAX_ORDER + 2], q[HD_MAX_ORDER + 2];
    // Build P'(z) = prod (1 - 2cos(w_2i) z^-1 + z^-2), Q' from odd indices.
    p[0] = 1.0;
    q[0] = 1.0;
    for (int i = 1; i <= M; i++) p[i] = q[i] = 0.0;
    for (int k = 0; k < H; k++) {
        const double cP = -2.0 * cos(lsf[2 * k]);
        const double cQ = -2.0 * cos(lsf[2 * k + 1]);
        const int n = 2 * k; // current polynomial order
        for (int i = n + 2; i >= 2; i--) {
            p[i] += cP * p[i - 1] + p[i - 2];
            q[i] += cQ * q[i - 1] + q[i - 2];
        }
        p[1] += cP * p[0];
        q[1] += cQ * q[0];
    }
    // P = P'(1 + z^-1), Q = Q'(1 - z^-1), A = (P + Q) / 2
    a[0] = 1.0;
    for (int i = 1; i <= M; i++) {
        const double Pi = p[i] + p[i - 1];
        const double Qi = q[i] - q[i - 1];
        a[i] = 0.5 * (Pi + Qi);
    }
}

bool is_stable(const double *a, int M) {
    double cur[HD_MAX_ORDER + 1], tmp[HD_MAX_ORDER + 1];
    memcpy(cur, a, sizeof(double) * (M + 1));
    for (int m = M; m >= 1; m--) {
        const double k = cur[m];
        if (!(fabs(k) < 0.9999)) return false;
        const double den = 1.0 - k * k;
        memcpy(tmp, cur, sizeof(double) * (m + 1));
        for (int i = 1; i < m; i++) cur[i] = (tmp[i] - k * tmp[m - i]) / den;
    }
    return true;
}

void lsf2a_stable(const double *lsf, int M, double *a) {
    lsf2a(lsf, M, a);
    double g = 0.998;
    for (int tries = 0; tries < 12 && !is_stable(a, M); tries++) {
        weight_lpc(a, M, g, a);
        g -= 0.004;
    }
    if (!is_stable(a, M)) {
        a[0] = 1.0;
        for (int i = 1; i <= M; i++) a[i] = 0.0;
    }
}

bool shape_lattice(const double *a, int M, const LatticeShape &s, double *out) {
    double cur[HD_MAX_ORDER + 1], tmp[HD_MAX_ORDER + 1], k[HD_MAX_ORDER + 1];
    memcpy(cur, a, sizeof(double) * (M + 1));
    for (int m = M; m >= 1; m--) {           // step-down: A(z) -> k[1..M]
        k[m] = cur[m];
        if (!(fabs(k[m]) < 1.0)) return false;
        const double den = 1.0 - k[m] * k[m];
        memcpy(tmp, cur, sizeof(double) * (m + 1));
        for (int i = 1; i < m; i++) cur[i] = (tmp[i] - k[m] * tmp[m - i]) / den;
    }
    const double order = s.order < M ? (s.order < 0.0 ? 0.0 : s.order) : (double)M;
    const int N = (int)floor(order);
    const double frac = order - N;
    const int L = (int)floor(s.band < 0.0 ? 0.0 : s.band);
    const double lf = s.band - L;
    for (int i = 1; i <= M; i++) {
        double w = 1.0;
        if (i > N + 1 || (i == N + 1 && order < M)) w = (i == N + 1) ? frac : 0.0;
        if (s.taper > 1e-6) {
            const double width = s.taper * (order > 1.0 ? order : 1.0);
            double t = (order + 0.5 - i) / width;
            w *= t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        }
        if (i <= L) w = 0.0;
        else if (i == L + 1) w *= 1.0 - lf;
        double kk = k[i] * w;
        if (w > 0.0 && (fabs(s.resonance - 1.0) > 1e-6 || s.jitter)) {
            const double c = kk > 0.999 ? 0.999 : (kk < -0.999 ? -0.999 : kk);
            double g = atanh(c) * s.resonance;
            if (s.jitter) g += s.jitter[i];
            kk = tanh(g);
        }
        if (kk > 0.995) kk = 0.995;
        if (kk < -0.995) kk = -0.995;
        k[i] = kk;
    }
    out[0] = 1.0;                             // step-up: k -> A(z)
    for (int i = 1; i <= M; i++) out[i] = 0.0;
    for (int m = 1; m <= M; m++) {
        memcpy(tmp, out, sizeof(double) * (m + 1));
        for (int i = 1; i < m; i++) out[i] = tmp[i] + k[m] * tmp[m - i];
        out[m] = k[m];
    }
    return true;
}

bool truncate_order(const double *a, int M, double order, double *out) {
    if (order >= M) {
        memcpy(out, a, sizeof(double) * (M + 1));
        return true;
    }
    if (order < 0.0) order = 0.0;
    const int N = (int)floor(order);
    const double frac = order - N;
    double cur[HD_MAX_ORDER + 1], tmp[HD_MAX_ORDER + 1];
    memcpy(cur, a, sizeof(double) * (M + 1));
    double kNext = 0.0;
    for (int m = M; m > N; m--) {
        const double k = cur[m];
        if (!(fabs(k) < 1.0)) return false;
        const double den = 1.0 - k * k;
        memcpy(tmp, cur, sizeof(double) * (m + 1));
        for (int i = 1; i < m; i++) cur[i] = (tmp[i] - k * tmp[m - i]) / den;
        cur[m] = 0.0;
        if (m == N + 1) kNext = k;
    }
    // Step back up to N+1 with a scaled reflection coefficient.
    if (frac > 0.0 && N < M) {
        const double k = kNext * frac;
        memcpy(tmp, cur, sizeof(double) * (N + 2));
        for (int i = 1; i <= N; i++) cur[i] = tmp[i] + k * tmp[N + 1 - i];
        cur[N + 1] = k;
    }
    memcpy(out, cur, sizeof(double) * (M + 1));
    return true;
}

void lsf_stabilize(double *lsf, int M, double minGap) {
    // insertion sort (M is small)
    for (int i = 1; i < M; i++) {
        const double v = lsf[i];
        int j = i - 1;
        while (j >= 0 && lsf[j] > v) {
            lsf[j + 1] = lsf[j];
            j--;
        }
        lsf[j + 1] = v;
    }
    if (minGap * (M + 1) > M_PI) minGap = M_PI / (M + 1);
    if (lsf[0] < minGap) lsf[0] = minGap;
    for (int i = 1; i < M; i++)
        if (lsf[i] < lsf[i - 1] + minGap) lsf[i] = lsf[i - 1] + minGap;
    if (lsf[M - 1] > M_PI - minGap) {
        lsf[M - 1] = M_PI - minGap;
        for (int i = M - 2; i >= 0; i--)
            if (lsf[i] > lsf[i + 1] - minGap) lsf[i] = lsf[i + 1] - minGap;
    }
}

double synth_energy(const double *a, int M, int n) {
    double y[HD_MAX_ORDER] = { 0 }; // circular past outputs
    double e = 0.0;
    for (int t = 0; t < n; t++) {
        double v = (t == 0) ? 1.0 : 0.0;
        for (int k = 1; k <= M; k++) {
            const int idx = t - k;
            if (idx < 0) break;
            v -= a[k] * y[idx % HD_MAX_ORDER];
        }
        y[t % HD_MAX_ORDER] = v;
        e += v * v;
    }
    return e;
}

void lpc_response_db(const double *a, int M, const double *w, int n, float *out) {
    for (int i = 0; i < n; i++) {
        double re = 0.0, im = 0.0;
        for (int k = 0; k <= M; k++) {
            re += a[k] * cos(k * w[i]);
            im -= a[k] * sin(k * w[i]);
        }
        out[i] = (float)(-10.0 * log10(re * re + im * im + 1e-20));
    }
}

void weight_lpc(const double *a, int M, double g, double *out) {
    double f = 1.0;
    for (int i = 0; i <= M; i++) {
        out[i] = a[i] * f;
        f *= g;
    }
}

} // namespace hdacelp

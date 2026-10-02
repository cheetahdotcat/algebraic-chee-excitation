/*
 * Vendored speexdsp resampler (BSD, see speex/COPYING), built in its
 * "outside speex" embedding mode with prefixed symbols, so the plugin needs
 * no system libspeexdsp and cannot clash with a host's own copy.
 */
#ifndef ACE_RESAMPLER_H_INCLUDED
#define ACE_RESAMPLER_H_INCLUDED

#ifndef OUTSIDE_SPEEX
#define OUTSIDE_SPEEX 1
#endif
#ifndef RANDOM_PREFIX
#define RANDOM_PREFIX ace_speex
#endif
#include "speex_resampler.h"

#endif

/* Builds the vendored speexdsp resampler (see ace_resampler.h). */
#define OUTSIDE_SPEEX 1
#define RANDOM_PREFIX ace_speex
#define FLOATING_POINT 1
#define EXPORT
#include "resample.c"

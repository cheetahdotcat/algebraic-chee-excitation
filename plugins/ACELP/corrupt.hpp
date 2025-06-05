
#include "math.h"
#include "codec/source.h"

void bit_desync_shift_left_Word16(Word16 *array, int len);
void burst_error(uint8_t *frame, int len, int burst_len);
void corrupt_by_overflow(short *acelp_array, int len);

void corrupt_by_wrong_interleave(short *coded_array, int len);
void random_bit_desync_Word16(Word16 *array, int len, int max_shift_bits, int flip_probability_percent);
void corrupt_bit_flips_Word16(Word16 *frame, int len, int bit_flip_percent);
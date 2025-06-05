
#include "math.h"
#include "corrupt.hpp"

#pragma region "Corruption Methods"
//
void bit_desync_shift_left_Word16(Word16 *array, int len) {
    uint16_t carry = 0;
    for (int i = 0; i < len; i++) {
        uint16_t val = (uint16_t)array[i];            // Treat signed as unsigned for bit ops
        uint16_t new_carry = (val & 0x8000) >> 15;    // Extract MSB before shift
        val = (val << 1) | carry;                      // Shift left + insert carry bit from prev element
        array[i] = (Word16)val;                        // Store back as signed Word16
        carry = new_carry;                             // Update carry for next word
    }
}
void burst_error(uint8_t *frame, int len, int burst_len) {
    int start = rand() % (len * 8 - burst_len); // random bit start
    for (int i = 0; i < burst_len; i++) {
        int bit_pos = start + i;
        int byte_idx = bit_pos / 8;
        int bit_idx = bit_pos % 8;
        frame[byte_idx] ^= (1 << bit_idx);
    }
}
void corrupt_by_overflow(short *acelp_array, int len) {
    for (int i = 0; i <= len; i++) {  // Off-by-one error, should be < len
        acelp_array[i] = i;  // Writes one element past end
    }
}
//
void corrupt_by_wrong_interleave(short *coded_array, int len) {
    for (int i = 0; i < len; i++) {
        int target_index = (i * 2) % len;  // If len is odd, this may cause issues
        coded_array[target_index] = i;
    }
}
void random_bit_desync_Word16(Word16 *array, int len, int max_shift_bits, int flip_probability_percent) {
    // max_shift_bits: max number of bits to shift left or right per element (e.g., 1 or 2)
    // flip_probability_percent: chance (0-100) to flip a random bit in the element
    
    for (int i = 0; i < len; i++) {
        uint16_t val = (uint16_t)array[i];
        int r2 = rand() % 100;
		if (r2 < 4) continue;
        // Randomly decide shift direction: -1=right, 0=no shift, 1=left
        int shift_dir = (rand() % 3) - 1;

        // Random shift amount between 0 and max_shift_bits
        int shift_amt = rand() % (max_shift_bits + 1);

        if (shift_dir == 1) {
            val = val << shift_amt;
        } else if (shift_dir == -1) {
            val = val >> shift_amt;
        }

        // Random bit flip based on probability
        int r = rand() % 100;
        if (r < flip_probability_percent) {
            int bit_to_flip = rand() % 16;
            val ^= (1 << bit_to_flip);
        }

        array[i] = (Word16)val;
    }
}
void corrupt_bit_flips_Word16(Word16 *frame, int len, int bit_flip_percent) {
    for (int i = 0; i < len; i++) {
        uint16_t val = (uint16_t)frame[i];
        for (int b = 0; b < 16; b++) {
            int r = rand() % 100;
            if (r < bit_flip_percent) {
                val ^= (1 << b);
            }
        }
        frame[i] = (Word16)val;
    }
}
//
#pragma endregion
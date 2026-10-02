/*
 * Dragonfly Reverb, copyright (c) 2019 Michael Willis, Rob van den Berg
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of
 * the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * For a full copy of the GNU General Public License see the LICENSE file.
 */

#include "math.h"
#include "corrupt.hpp"

#include "DistrhoPlugin.hpp"
#include "DistrhoPluginInfo.h"
#include "DSP.hpp"
#if defined(__SSE__)
#include <xmmintrin.h>
#endif

// 256-tap asymmetric Hamming LPC analysis window for the native 8kHz codec.
// Provides `static Word16 window[256]` and `#define L_window 256`, passed to
// encoder_Coder_Tetra(). (window512.tab holds the 512-tap variant for a future
// HD mode.)
#include "codec/window.tab"

#pragma region "mein kram"

Word16 encoder_last_ener_pit;
Word16 encoder_last_ener_cod;
Word16 sdec_last_ener_pit;
Word16 sdec_last_ener_cod;
Word16 speech_frameCache[(L_frame+40+10)];
Word16 *encoder_speech, *encoder_p_window;
Word16 *encoder_new_speech;                    /* Global variable */
Word16 *encoder_old_speech;                    /* points at speech_frameCache; declared extern in codec/source.h */

Word16 encoder_FS_Flag = 0; /* Frame Stealing Flag :
				 0 = no stealing in the time-slot,
				!0 = stealing of the first speech frame
						in the time-slot
				This flag is set/reset by external world */
Word32 encoder_Loop_counter = 0;
short encoder_first_pass = true;
Word16 encoder_i;
Word16 encoder_Vocod_array[dual_serial_size-2]; /* Input Buffer : 2 vocoder frames */
Word16 encoder_Coded_array[TS7k2_size];
Word16 encoder_Interleaved_coded_array[TS7k2_size]; /* Output Buffer */

// scoder

Word16 encoder_frame;

// extern Word16 *encoder_new_speech; /* Pointer on encoder_new_speech. */
// extern Word16 *encoder_speech; /* Pointer on encoder_speech. */

Word16 encoder_syn[L_frame];		/* Local synthesis.       */
Word16 encoder_ana[ana_size];		/* Analysis parameters.   */
Word16 encoder_serial[serial_size]; /* Serial stream.         */

// Encoder Stop

// Decoder Start
Word16 decoder_Frame_stealing = 0; /* Frame Stealing Flag :
				   0 = Inactive,
				   !0 = First Frame in time-slot stolen
				   This flag is set/reset by external world */

// speechdecoder
Word16 decoder_frame;
Word16 decoder_synth[L_frame];		/* Synthesis              */
Word16 decoder_synth_frame1[L_frame];		/* Synthesis              */
Word16 decoder_synth_frame2[L_frame];		/* Synthesis              */
Word16 decoder_parm[prm_size];		/* Synthesis parameters   */
Word16 decoder_serial[serial_size]; /* Serial stream          */
FILE *decoder_f_syn, *decoder_f_serial;

// channeldecoder
FILE *decoder_fin, *decoder_fout;
Word32 decoder_Loop_counter = 0;
short decoder_first_pass = true;
Word16 decoder_i;
Word16 decoder_bfi1 = 0;
Word16 decoder_bfi2 = 0; /* Reset Bad Frame Indicator :
	0 = correct data, 1 = Corrupted frame */

Word16 decoder_Reordered_array[s286_size];		 /* 2 frames vocoder + 8 + 4 */
Word16 decoder_Interleaved_coded_array[TS7k2_size]; /*time-slot length at 7.2 kb/s*/
Word16 decoder_Coded_array[TS7k2_size];
//
int speex_err;
void convert_buffer_int16_to_float(Word16 *in, float *out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        out[i] = in[i] / 32768.0f;
    }
}
void convert_buffer_float_to_int16(float *in, Word16 *out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        float x = in[i];
        if (x > 1.0f) x = 1.0f;
        else if (x < -1.0f) x = -1.0f;
        out[i] = (Word16)(x * 32000.0f); //32767.0f);
    }
}
//
void print_float_hex(const float *data, int len) {
    for (int i = 0; i < len; ++i) {
        uint32_t hex;
        memcpy(&hex, &data[i], sizeof(float)); // Safely reinterpret float as uint32_t
        DEBUG_PRINTF("%08X%s", hex, (i < len - 1) ? " " : "\n");
        if ((i + 1) % 32 == 0) DEBUG_PRINTF("\n"); // optional: newline every 8 entries
    }
}
void print_word16_hex(const Word16 *data, int len) {
    for (int i = 0; i < len; ++i) {
        printf("%04X%s", (uint16_t)data[i], (i < len - 1) ? "" : "\n");
        if ((i + 1) % 32 == 0) printf("\n"); // optional: newline every 8 entries
    }
}
//


// #define ACELP_DUAL_CHAN_FRAME_SIZE 432 // 60ms @ 8kHz
// #define ACELP_DUAL_CHAN_AUDIO_SIZE 480 // 8kHz // L_frame*2

// #define ACELP_FRAME_SIZE 286 // 60ms @ 8kHz
// #define ACELP_VOCODER_SAMPLE_COUNT 286
// #define UPSAMPLE_RATIO 6
// #define DOWNSAMPLE_RATIO 6
// #define UPSAMPLED_FRAME_SIZE (ACELP_FRAME_SIZE * UPSAMPLE_RATIO)
Word16 samples[ACELP_DUAL_CHAN_AUDIO_SIZE];

#if 0 // window512: 512-tap LPC window reserved for a future 16k/32k "HD" mode
static Word16 window512[512] = {
   2621,  2622,  2626,  2632,  2640,  2650,  2662,  2677,
   2694,  2714,  2735,  2759,  2785,  2814,  2844,  2877,
   2912,  2949,  2989,  3031,  3075,  3121,  3169,  3220,
   3273,  3328,  3385,  3444,  3506,  3569,  3635,  3703,
   3773,  3845,  3919,  3996,  4074,  4155,  4237,  4321,
   4408,  4496,  4587,  4680,  4774,  4870,  4969,  5069,
   5171,  5275,  5381,  5489,  5599,  5710,  5824,  5939,
   6056,  6174,  6295,  6417,  6541,  6666,  6793,  6922,
   7052,  7185,  7318,  7453,  7590,  7728,  7868,  8010,
   8152,  8296,  8442,  8589,  8737,  8887,  9038,  9191,
   9344,  9499,  9655,  9813,  9971, 10131, 10292, 10454,
  10617, 10781, 10946, 11113, 11280, 11448, 11617, 11787,
  11958, 12130, 12303, 12476, 12650, 12825, 13001, 13178,
  13355, 13533, 13711, 13890, 14070, 14250, 14431, 14612,
  14793, 14975, 15158, 15341, 15524, 15708, 15892, 16076,
  16260, 16445, 16629, 16814, 16999, 17185, 17370, 17555,
  17741, 17926, 18111, 18296, 18481, 18667, 18851, 19036,
  19221, 19405, 19589, 19773, 19956, 20139, 20322, 20504,
  20686, 20867, 21048, 21229, 21409, 21588, 21767, 21945,
  22122, 22299, 22475, 22651, 22825, 22999, 23172, 23344,
  23516, 23686, 23856, 24025, 24192, 24359, 24525, 24689,
  24853, 25016, 25177, 25337, 25496, 25654, 25811, 25967,
  26121, 26274, 26426, 26576, 26725, 26873, 27019, 27164,
  27308, 27450, 27590, 27729, 27867, 28003, 28137, 28270,
  28401, 28531, 28659, 28785, 28910, 29033, 29154, 29274,
  29391, 29507, 29622, 29734, 29845, 29953, 30060, 30165,
  30268, 30370, 30469, 30566, 30662, 30755, 30847, 30936,
  31024, 31109, 31193, 31274, 31354, 31431, 31506, 31579,
  31650, 31719, 31786, 31851, 31913, 31974, 32032, 32088,
  32142, 32194, 32243, 32291, 32336, 32379, 32419, 32458,
  32494, 32528, 32560, 32589, 32617, 32642, 32664, 32685,
  32703, 32719, 32733, 32744, 32753, 32760, 32764, 32767,
  32767, 32764, 32757, 32744, 32726, 32704, 32676, 32643,
  32606, 32563, 32516, 32463, 32406, 32344, 32277, 32205,
  32128, 32047, 31960, 31870, 31774, 31674, 31570, 31461,
  31347, 31229, 31107, 30980, 30850, 30715, 30576, 30433,
  30286, 30135, 29980, 29821, 29659, 29493, 29323, 29150,
  28974, 28794, 28610, 28424, 28235, 28042, 27846, 27648,
  27447, 27243, 27036, 26827, 26615, 26401, 26185, 25967,
  25746, 25523, 25298, 25072, 24844, 24613, 24382, 24149,
  23914, 23678, 23440, 23202, 22962, 22722, 22480, 22238,
  21995, 21751, 21506, 21261, 21016, 20770, 20524, 20278,
  20031, 19785, 19538, 19292, 19046, 18800, 18554, 18309,
  18065, 17820, 17577, 17334, 17092, 16850, 16610, 16370,
  16131, 15894, 15657, 15422, 15188, 14955, 14723, 14493,
  14264, 14037, 13811, 13586, 13364, 13142, 12923, 12705,
  12489, 12275, 12063, 11852, 11643, 11437, 11232, 11029,
  10828, 10629, 10433, 10238, 10045,  9855,  9666,  9480,
   9296,  9114,  8934,  8757,  8582,  8409,  8238,  8069,
   7903,  7738,  7577,  7417,  7259,  7104,  6951,  6801,
   6652,  6506,  6362,  6220,  6081,  5944,  5808,  5676,
   5545,  5416,  5290,  5166,  5044,  4924,  4806,  4690,
   4577,  4465,  4355,  4248,  4142,  4039,  3937,  3838,
   3740,  3645,  3551,  3459,  3369,  3281,  3194,  3110,
   3027,  2946,  2867,  2789,  2713,  2639,  2566,  2495,
   2426,  2358,  2291,  2227,  2163,  2102,  2041,  1982,
   1925,  1869,  1814,  1760,  1708,  1657,  1608,  1559,
   1512,  1466,  1422,  1378,  1336,  1294,  1254,  1215,
   1177,  1140,  1103,  1068,  1034,  1001,   969,   937,
    907,   877,   848,   820,   793,   766,   741,   716,
    692,   668,   645,   623,   602,   581,   561,   541,
    522,   504,   486,   469,   452,   436,   421,   405,
    391,   377,   363,   350,   337,   324,   312,   301,
    289,   279,   268,   258,   248,   239,   230,   221,
};
#endif // window512

void upsample_6x(float *in, float *out, size_t out_len) {
    size_t num_input_samples = out_len / UPSAMPLE_RATIO;

    for (size_t i = 0; i < num_input_samples; ++i) {
        float sample = in[i];
        for (int j = 0; j < UPSAMPLE_RATIO; ++j) {
            out[i * UPSAMPLE_RATIO + j] = sample;
        }
    }
}
void downsample_6x(const float *in, float *out, size_t in_len) {
    size_t out_len = in_len / DOWNSAMPLE_RATIO;

    for (size_t i = 0; i < out_len; ++i) {
        out[i] = in[i * DOWNSAMPLE_RATIO];
    }
}

// takes 480 samples and converts them into 432 16bit channel-frame
static int fieldBitDiff(const Word16 *a, const Word16 *b, int n);

// Returns the number of bits flipped inside targeted codec fields.
int encode_acelp(float *input_8khz, Word16 *out, const CorruptCfg *cfgs, int ncfg, unsigned targetMask) {
	int targeted = 0;
	Word16 syn[L_frame];		/* Local synthesis.       */
	Word16 ana[ana_size];		/* Analysis parameters.   */
	// static Word16 serial[serial_size]; /* Serial stream.         */


	convert_buffer_float_to_int16(input_8khz, samples, (size_t)ACELP_DUAL_CHAN_AUDIO_SIZE);
	Word16 firstHalf[L_frame];
	Word16 secondHalf[L_frame];
	// static Word16 out2[ACELP_DUAL_CHAN_AUDIO_SIZE];
	Word16 vocoder_ChannelFrame[TS7k2_size];

	// static Word16 outInterleaved_coded_array[ACELP_DUAL_CHAN_FRAME_SIZE]; /* Output Buffer */
	// print_float_hex(input_8khz, 480);
	for (int i = 0; i < L_frame; i++) {
		firstHalf[i] = samples[i];
		secondHalf[i] = samples[i+L_frame];
	}
	// memcpy(out, &samples, sizeof(Word16) * ACELP_DUAL_CHAN_AUDIO_SIZE);
	// return;
		// print_float_hex(down_out, 480);
	// // print_word16_hex(firstHalf, L_frame);
	// print_word16_hex(samples, 480);
	Word16 vocoderArray[dual_serial_size-2];
	Word16 vocoderSerial[serial_size];

	{ // 1st 137 bits
		// print_word16_hex(firstHalf, 240);
		memcpy(encoder_new_speech, firstHalf, L_frame*sizeof(Word16));
		Pre_Process(firstHalf, (Word16)L_frame); /* Pre processing of input speech */
		// print_word16_hex(firstHalf, L_frame);
		encoder_Coder_Tetra(ana, syn, L_window, window);					  /* Find speech parameters         */
		for (int c = 0; c < ncfg; c++)
			corrupt_apply_params(ana, ana_size, cfgs[c]);     /* musical parameter-domain corruption */
		if (targetMask) {                                 /* targeted bit errors in the speech frame */
			Word16 before[ana_size];
			memcpy(before, ana, sizeof(before));
			for (int c = 0; c < ncfg; c++) corrupt_apply_bitstream_ana(ana, ana_size, cfgs[c], targetMask);
			targeted += fieldBitDiff(before, ana, ana_size);
		}
		// print_word16_hex(ana, 23);
		// print_word16_hex(syn, 240);
		Post_Process(syn, (Word16)L_frame);		  /* Post processing of synthesis   */
		Prm2bits_Tetra(ana, vocoderSerial);			  /* Parameters to serial bits      */
		// fwrite(syn,    sizeof(Word16), L_frame    ,  f_syn);
		//fwrite(serial, sizeof(Word16), serial_size, f_serial);
		// print_word16_hex(vocoderSerial, 138);
		// TODO: copy serial to Vocod_array
		for (encoder_i = 0; encoder_i < serial_size-1; encoder_i++)
			vocoderArray[encoder_i] = vocoderSerial[encoder_i+1];
	}
	{ // second 137 bits
		Pre_Process(secondHalf, (Word16)L_frame); /* Pre processing of input speech */
		// print_word16_hex(secondHalf, L_frame);
		memcpy(encoder_new_speech, secondHalf, L_frame*sizeof(Word16));
		encoder_Coder_Tetra(ana, syn, L_window, window);					  /* Find speech parameters         */
		for (int c = 0; c < ncfg; c++)
			corrupt_apply_params(ana, ana_size, cfgs[c]);     /* musical parameter-domain corruption */
		if (targetMask) {                                 /* targeted bit errors in the speech frame */
			Word16 before[ana_size];
			memcpy(before, ana, sizeof(before));
			for (int c = 0; c < ncfg; c++) corrupt_apply_bitstream_ana(ana, ana_size, cfgs[c], targetMask);
			targeted += fieldBitDiff(before, ana, ana_size);
		}
		// print_word16_hex(ana, 23);
		// print_word16_hex(syn, 240);
		Post_Process(syn, (Word16)L_frame);		  /* Post processing of synthesis   */
		Prm2bits_Tetra(ana, vocoderSerial);			  /* Parameters to serial bits      */
		// fwrite(syn,    sizeof(Word16), L_frame    ,  f_syn);
		//fwrite(serial, sizeof(Word16), serial_size, f_serial);
		// print_word16_hex(vocoderSerial, 138);
		// TODO: copy serial to Vocod_array
		for (encoder_i = 0; encoder_i < serial_size-1; encoder_i++)
			vocoderArray[encoder_i+(serial_size-1)] = vocoderSerial[encoder_i+1];
	}
	// print_word16_hex(vocoderArray, dual_serial_size-2);  // realtime hazard: printf per frame in the audio thread
	{ // Encode
		/* Channel Encoding */
		Channel_Encoding(encoder_first_pass, encoder_FS_Flag, vocoderArray, vocoder_ChannelFrame);
		encoder_first_pass = false;
		/* Interleaving */
		if (!encoder_FS_Flag)
			Interleaving_Speech(vocoder_ChannelFrame, out);
		else
		{
			Interleaving_Signalling(vocoder_ChannelFrame + 216, out + 216);
			for (encoder_i = 0; encoder_i < 216; encoder_i++)
				out[encoder_i] = vocoder_ChannelFrame[encoder_i];
		}
		/* Increment Loop counter */
		encoder_Loop_counter++;
		// print_word16_hex(out, ACELP_DUAL_CHAN_FRAME_SIZE);
		// memcpy(out, &outInterleaved_coded_array, sizeof(Word16) * ACELP_DUAL_CHAN_FRAME_SIZE);
	}
	return targeted;
}
// takes 432 16bit channel frames and converts them into 480 samples
void decode_acelp(Word16 *dualChannelFrames, float *output_8kHz) {
	{ // Decoder
		if (decoder_Frame_stealing) {
			Desinterleaving_Signalling(dualChannelFrames + 216,
										decoder_Coded_array + 216);
			/* When Frame Stealing occurs, recopy first half slot : */
			for (decoder_i = 0; decoder_i < 216; decoder_i++)
				decoder_Coded_array[decoder_i] =
					dualChannelFrames[decoder_i];
		} else {
			Desinterleaving_Speech(dualChannelFrames, decoder_Coded_array);
		}
		decoder_bfi1 = decoder_Frame_stealing;
		/* "Interleaved_coded_array" has been desinterleaved and result put in "Coded_array" */
		/* Message in case the Frame was stolen */
		// if (decoder_bfi1)
		// 	fDEBUG_PRINTF(stderr, "Frame Nb %ld was stolen\n", decoder_Loop_counter + 1);
		/* Channel Decoding */
		decoder_bfi2 = Channel_Decoding(decoder_first_pass, decoder_Frame_stealing,
								decoder_Coded_array, decoder_Reordered_array);
		decoder_first_pass = false;
		if ((decoder_Frame_stealing == 0) && (decoder_bfi2 == 1))
			decoder_bfi1 = 1;
		/* Increment Loop counter */
		decoder_Loop_counter++;
		/* Message in case the Bad Frame Indicator was set */
		// if (decoder_bfi2)
		// 	fDEBUG_PRINTF(stderr, "Frame Nb %ld Bfi active\n\n", decoder_Loop_counter);
		/* writing  Reordered_array to output file */
		{ // Speech Frame 1
			int decoder_serial_index = 0;
			/* bfi bit */
			decoder_serial[decoder_serial_index++] = decoder_bfi1;
			/* 1st speech frame */
			for (int i = 0; i < serial_size-1; i++) {
				decoder_serial[decoder_serial_index++] = decoder_Reordered_array[i];
			}
			Bits2prm_Tetra(decoder_serial, decoder_parm);	/* serial to parameters */
			sdec_Decod_Tetra(decoder_parm, decoder_synth_frame1);		/* decoder */
			Post_Process(decoder_synth_frame1, (Word16)L_frame);	/* Post processing of synthesis  */
		}
		{ // Speech Frame 2
			int decoder_serial_index = 0;
			/* bfi bit */
			decoder_serial[decoder_serial_index++] = decoder_bfi2;
			/* 2nd speech frame */
			for (int i = serial_size-1; i < (dual_serial_size-2); i++) {
				decoder_serial[decoder_serial_index++] = decoder_Reordered_array[i];
			}
			Bits2prm_Tetra(decoder_serial, decoder_parm);	/* serial to parameters */
			sdec_Decod_Tetra(decoder_parm, decoder_synth_frame2);		/* decoder */
			Post_Process(decoder_synth_frame2, (Word16)L_frame);	/* Post processing of synthesis  */
		}
	}
	Word16 fullFrame[ACELP_DUAL_CHAN_AUDIO_SIZE];
	for (size_t i = 0; i < L_frame; ++i) {
		fullFrame[i] = decoder_synth_frame1[i] ;
		fullFrame[i+L_frame] = decoder_synth_frame2[i];
	}
	convert_buffer_int16_to_float(fullFrame, output_8kHz, ACELP_DUAL_CHAN_AUDIO_SIZE);
}


const char *fuckFrame = "216b81ff81ff81ff81ff81ff81ff81ff81ff81ff7f0081ff81ff81ff81ff81ff7f0081ff7f007f0081ff81ff7f007f007f0081ff81ff81ff81ff7f007f007f007f007f007f007f007f0081ff81ff7f0081ff81ff7f0081ff7f0081ff7f007f007f0081ff81ff7f0081ff7f007f0081ff7f007f007f0081ff81ff7f0081ff81ff7f007f007f007f007f007f007f007f007f0081ff81ff81ff81ff7f0081ff7f007f007f0081ff81ff81ff81ff7f0081ff81ff81ff7f0081ff7f0081ff7f007f007f0081ff7f007f007f007f0081ff81ff81ff7f0081ff7f007f007f007f007f007f0081ff7f00226b7f0081ff81ff7f007f0081ff81ff7f0081ff7f007f007f0081ff81ff81ff7f0081ff81ff7f0081ff81ff7f007f007f0081ff81ff81ff7f007f0081ff81ff7f007f007f0081ff7f007f0081ff81ff7f0081ff7f007f007f0081ff7f0081ff7f0081ff7f007f007f007f0081ff81ff7f0081ff81ff81ff7f007f0081ff81ff81ff7f0081ff81ff81ff7f0081ff7f007f0081ff81ff7f007f007f0081ff81ff81ff81ff7f0081ff81ff81ff7f0081ff7f0081ff81ff81ff81ff81ff81ff81ff7f0081ff7f0081ff7f007f0081ff81ff81ff7f007f0081ff7f007f0081ff7f0081ff81ff7f00236b7f0081ff81ff81ff7f0081ff7f0081ff7f007f007f007f0081ff81ff7f007f0081ff7f007f007f007f0081ff7f007f007f007f007f0081ff7f0081ff81ff7f0081ff7f007f007f0081ff81ff7f007f0081ff7f007f007f007f0081ff81ff81ff81ff81ff81ff81ff81ff81ff81ff81ff7f007f007f007f007f0081ff7f007f0081ff81ff7f007f007f0081ff81ff7f0081ff81ff81ff7f0081ff7f007f0081ff81ff7f0081ff7f0081ff81ff7f007f007f0081ff7f007f007f007f007f007f0081ff7f007f007f0081ff7f0081ff7f007f007f0081ff7f0081ff7f007f0081ff81ff7f00246b7f0081ff7f007f0081ff7f0081ff81ff81ff7f007f0081ff81ff7f007f007f0081ff7f007f007f007f007f0081ff81ff81ff7f007f0081ff81ff7f0081ff81ff81ff81ff7f007f0081ff81ff7f007f0081ff7f007f0081ff81ff7f0081ff7f0081ff7f007f007f007f007f007f007f007f007f007f007f0081ff81ff81ff7f007f007f0081ff7f0081ff7f0081ff7f007f007f007f0081ff81ff7f0081ff81ff81ff81ff7f0081ff81ff7f0081ff7f0081ff7f00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000256b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000266b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";
void hex_to_word16_array(const char *hex_str, Word16 *out, size_t max_out_len) {
    size_t hex_len = strlen(hex_str);
    size_t bytes_len = hex_len / 2;

    if (bytes_len < max_out_len * 2) {
        DEBUG_PRINTF("Hex string too short for 432 Word16s\n");
        return;
    }

    for (size_t i = 0; i < max_out_len; ++i) {
        // Parse two bytes per Word16
        char byte_str[3] = {0};
        byte_str[0] = hex_str[i * 4 + 0];
        byte_str[1] = hex_str[i * 4 + 1];
        uint8_t hi = (uint8_t)strtol(byte_str, NULL, 16);

        byte_str[0] = hex_str[i * 4 + 2];
        byte_str[1] = hex_str[i * 4 + 3];
        uint8_t lo = (uint8_t)strtol(byte_str, NULL, 16);

        int16_t word = (hi << 8) | lo;
        out[i] = word;
    }
}


#pragma endregion

// Increase the late level by approx 8dB
#define LATE_GAIN 2.5f

CheetahDSP::CheetahDSP(double sampleRate): stopThread(false), threadRunning(false) {
  for (uint32_t param = 0; param < paramCount; param++) {
    newParams[param] = banks[DEFAULT_BANK].presets[DEFAULT_PRESET].params[param];
    oldParams[param] = -1.0;
  }
    finalized = false;
	encoded_frame_queue = NULL;
	ring2 = NULL;
	ring_48k_incoming = NULL;
	pcm_output_buffer = NULL;
	resampler_down = NULL;
	resampler_up = NULL;

    stopThread.store(false, std::memory_order_release);
    DEBUG_PRINTF("activate 3\n");

#define  p        (Word16)10
#define  L_next   (Word16)40
#define  L_total  (Word16)(L_frame+L_next+p)
// L_window comes from codec/window.tab (256); it must match the window passed
// to encoder_Coder_Tetra so encoder_p_window is positioned correctly.

    sampleRateChanged(sampleRate);
	if (!finalized) {
		finalized = true;
		DEBUG_PRINTF("activate\n");
        DEBUG_PRINTF("initialized initialACELPSetup 1\n");
        encoded_frame_queue = cat_ringbuffer_create(sizeof(Word16) * ACELP_DUAL_CHAN_FRAME_SIZE * PIPELINE_BUFFER_FRAMES);
        ring_48k_incoming = cat_ringbuffer_create((VST_SampleRate * sizeof(float))); // 1s 48khz
        ring2 = cat_ringbuffer_create(RINGBUFFER_SIZE);
        pcm_output_buffer = cat_ringbuffer_create(sizeof(float) * ACELP_DUAL_CHAN_AUDIO_SIZE * UPSAMPLE_RATIO * PIPELINE_BUFFER_FRAMES);
		// encoder_old_speech is now a pointer (renamed from a static array); it MUST point
		// at our backing store before encoder_Init_Coder_Tetra() runs, since that init both
		// derives the other speech pointers from it and zeroes it. Assigning it after init
		// (as before) leaves it NULL during init -> NULL deref / crash on load.
		encoder_old_speech = speech_frameCache;
        /* Initialization of decoder  */
        sdec_Init_Decod_Tetra();
        /* Initialization of the coder */
        Init_Pre_Process();
        encoder_Init_Coder_Tetra();
        /* Loop for each "L_frame" speech data. */
        encoder_frame = 0;
        decoder_frame = 0;
		// custom framesizes
		encoder_new_speech = encoder_old_speech + L_total - L_frame;	/* New speech     */
		encoder_speech     = encoder_new_speech - L_next;			/* Present frame  */
		encoder_p_window   = encoder_old_speech + L_total - L_window;	/* For LPC window */
        //
        resampler_down = speex_resampler_init(1, VST_SampleRate, TETRA_SampleRate, 5, &speex_err);
        resampler_up = speex_resampler_init(1, TETRA_SampleRate, VST_SampleRate, 5, &speex_err);
        // HD ACELP state is large (~0.5 MB); allocate it here, never on the
        // audio thread. It is configured lazily by the background thread.
        hd = new HdState();

        DEBUG_PRINTF("initialized initialACELPSetup 2\n");

		DEBUG_PRINTF("activate 2\n");
		DEBUG_PRINTF("activated\n");
	}

    // Start the background decode/upsample thread ONLY after every resource it
    // touches (ringbuffers, codec state, resamplers) has been created. Spawning
    // it earlier races the init below and dereferences a garbage resampler_up
    // pointer -> segfault on load.
    if (!threadRunning.load(std::memory_order_acquire)) {
        DEBUG_PRINTF("starting background thread");
        backgroundThread = std::thread(&CheetahDSP::threadFunction, this);
    }
}

CheetahDSP::~CheetahDSP() {
    // delete smooth_gain;
	stopThread.store(true, std::memory_order_release);
	if (backgroundThread.joinable())
		backgroundThread.join();
	if (finalized) {
		finalized = false;
	memset(&tetraCurrent, 0, sizeof(tetraCurrent));
		DEBUG_PRINTF("deactivate 1\n");
		stopThread.store(true, std::memory_order_release);
		DEBUG_PRINTF("deactivate 2\n");
		// DEBUG_PRINTF("deactivate 2\n");
		if (backgroundThread.joinable())
			backgroundThread.join();
		DEBUG_PRINTF("deactivate 2.1\n");
		if (encoded_frame_queue != NULL) {
			DEBUG_PRINTF("encoded frame queue is not null, freeing");
			cat_ringbuffer_free(encoded_frame_queue);
		}
		DEBUG_PRINTF("deactivate 2.2\n");
		if (ring_48k_incoming != NULL) {
			DEBUG_PRINTF("ring_48k_incoming is not null, freeing");
			cat_ringbuffer_free(ring_48k_incoming);
		}
		DEBUG_PRINTF("deactivate 2.3\n");
		if (ring2 != NULL) {
			DEBUG_PRINTF("ring2 is not null, freeing");
			cat_ringbuffer_free(ring2);
		}
		DEBUG_PRINTF("deactivate 2.5\n");
		if (pcm_output_buffer != NULL) {
			DEBUG_PRINTF("pcm_output_buffer is not null, freeing");
			cat_ringbuffer_free(pcm_output_buffer);
		}
		DEBUG_PRINTF("deactivated 3\n");
	}
	if (hd != nullptr) {
		hdCloseResamplers();
		delete hd;
		hd = nullptr;
	}
}
float CheetahDSP::getParameterValue(uint32_t index) const {
  if (index < paramCount) {
    return newParams[index];
  }
  return 0.0;
}
void CheetahDSP::setParameterValue(uint32_t index, float value) {
  if (index < paramCount) {
    newParams[index] = value;
  }
}

// Build the current corruption settings from the live parameters. Intensity
// and magnitude are normalized 0..1 (rate / depth). See CorruptMode for how the
// mode maps to a parameter-domain (musical) or bitstream-domain (extreme) stage.
CorruptCfg CheetahDSP::corruptCfg() const {
	CorruptCfg cfg;
	cfg.mode      = (int)lround(newParams[paramCorruptionMode]);
	float in = newParams[paramCorruptionIntensity] / 100.0f;
	float mg = newParams[paramCorruptionMagnitude] / 100.0f;
	in = in < 0.0f ? 0.0f : (in > 1.0f ? 1.0f : in);
	mg = mg < 0.0f ? 0.0f : (mg > 1.0f ? 1.0f : mg);
	// Squared rate curve: most of the knob travel is in the subtle range.
	cfg.intensity  = corrupt_rate_curve(in);
	cfg.magnitude  = mg;
	cfg.calibrated = true;
	return cfg;
}

int CheetahDSP::corruptCfgs(CorruptCfg *out) {
	int n = 0;
	const CorruptCfg knob = corruptCfg();
	if (knob.mode != kCorruptOff) out[n++] = knob;
	for (int k = 0; k < 10; k++) {
		KeyEnv &ke = keyEnv[k];
		if (ke.e <= 0.0f && !ke.fired) continue;
		CorruptCfg c;
		c.mode = kCorruptLsp + k;
		c.calibrated = true;
		c.magnitude = ke.e;
		c.trigger = ke.fired;
		switch (c.mode) {
			case kCorruptBitFlips:
			case kCorruptBurst:    c.intensity = 1.0f;          break; // continuous error stream
			case kCorruptBitSlip:  c.intensity = 0.03f * ke.e;  break; // the hit itself + rare re-slips
			case kCorruptOverflow:
			case kCorruptReinterleave: c.intensity = 0.3f * ke.e; break;
			case kCorruptFreeze:
				// hold the frame captured at the hit; velocity = depth while
				// held (127 = complete freeze), envelope fade after release
				c.intensity = 1.0f;
				c.magnitude = ke.held ? ke.peak : ke.e;
				c.hold = true;
				break;
			default:               c.intensity = ke.e * ke.e;   break; // parameter modes
		}
		ke.fired = false;
		out[n++] = c;
	}
	return n;
}

void CheetahDSP::updateKeyEnvelopes(float dt) {
	const float sustain = 0.4f + 0.6f * midiPressure.load(std::memory_order_relaxed) / 127.0f;
	uint16_t mask = 0;
	for (int k = 0; k < MIDI_CORRUPT_KEYS; k++) {
		KeyEnv &ke = keyEnv[k];
		const int vel = midiKeyVel[k].load(std::memory_order_relaxed);
		const uint32_t trig = midiKeyTrig[k].load(std::memory_order_acquire);
		if (trig != ke.trigSeen) {
			ke.trigSeen = trig;
			if (vel > 0) {
				ke.peak = vel / 127.0f;
				ke.t = 0.0f;
				ke.fired = true;
			}
		}
		ke.held = vel > 0;
		if (ke.held) {
			ke.e = ke.peak * (sustain + (1.0f - sustain) * expf(-ke.t / 0.25f));
			ke.t += dt;
		} else {
			ke.e *= expf(-dt / 0.12f);
			if (ke.e < 0.01f) ke.e = 0.0f;
		}
		if (ke.e > 0.0f) mask |= (uint16_t)(1u << k);
		keyLevel[k].store((uint8_t)lrintf(fminf(1.0f, ke.e) * 255.0f), std::memory_order_relaxed);
	}
	keyActiveMask.store(mask, std::memory_order_relaxed);
}

void CheetahDSP::midiCorruptNoteOn(int key, int velocity) {
	if (key < 0 || key >= MIDI_CORRUPT_KEYS) return;
	if (velocity <= 0) { midiCorruptNoteOff(key); return; }
	midiKeyVel[key].store((uint8_t)(velocity > 127 ? 127 : velocity), std::memory_order_relaxed);
	midiKeyTrig[key].fetch_add(1u, std::memory_order_release);
}

void CheetahDSP::midiCorruptNoteOff(int key) {
	if (key < 0 || key >= MIDI_CORRUPT_KEYS) return;
	midiKeyVel[key].store(0, std::memory_order_relaxed);
}

void CheetahDSP::setMidiCorruptPressure(int pressure) {
	midiPressure.store((uint8_t)(pressure < 0 ? 0 : (pressure > 127 ? 127 : pressure)), std::memory_order_relaxed);
}

void CheetahDSP::clearMidiCorruptKeys() {
	for (int k = 0; k < MIDI_CORRUPT_KEYS; k++) midiKeyVel[k].store(0, std::memory_order_relaxed);
	midiPressure.store(0, std::memory_order_relaxed);
}

void CheetahDSP::process_buff_ring1_audio(size_t nframes) {
	// One codec frame consumes ACELP_DUAL_CHAN_AUDIO_SIZE samples at TETRA_SampleRate,
	// which is UPSAMPLE_RATIO (host/tetra) samples at the host rate. This MUST match the
	// resampler ratio: if we read more than the downsampler consumes for one 960-sample
	// output frame, the surplus is discarded every iteration and half the audio is dropped
	// (choppy output / underruns). 48000/16000 = 3, not the old 8kHz-era value of 6.
	const int samples48kSize = ACELP_DUAL_CHAN_AUDIO_SIZE * UPSAMPLE_RATIO;

	float samples48k[samples48kSize];
	float down_out[ACELP_DUAL_CHAN_AUDIO_SIZE];
	//
	size_t available = cat_ringbuffer_read_space(ring_48k_incoming);
    size_t samples_available = available / sizeof(float);
	// DEBUG_PRINTF("process_buff_ring1_audio: nframes=%d samples_available=%d\n", nframes, samples_available);
    // if (samples_available > MAX_FRAME_SAMPLES) samples_available = MAX_FRAME_SAMPLES;
	while (samples_available >= samples48kSize) {
		cat_ringbuffer_read(ring_48k_incoming, (char *)samples48k, samples48kSize*sizeof(float));
		// DEBUG_PRINTF("2880 48k samples available for encoding, output 480 8k samples, output 432byte channel-frame\n");
		// 8kHz float ^
		// --- Step 1: Downsample input ---
		spx_uint32_t in_len = samples48kSize;
		spx_uint32_t out_len = ACELP_DUAL_CHAN_AUDIO_SIZE;
		speex_resampler_process_float(resampler_down, 0, samples48k, &in_len, down_out, &out_len);
		static Word16 channelFrameOutput[ACELP_DUAL_CHAN_FRAME_SIZE];
		// downsample_6x(samples48k, down_out, samples48kSize);
		// print_float_hex(down_out, 480);
		updateKeyEnvelopes(0.06f); // one TETRA slot = 60 ms
		CorruptCfg cfgs[MAX_CORRUPT_CFGS];
		const int ncfg = corruptCfgs(cfgs);
		// TARGET: bitstream modes hit only the selected fields of the speech
		// frames (before channel coding, so the FEC carries them through).
		const unsigned targetMask = corrupt_target_mask((int)lround(newParams[paramCorruptTarget]));
		const int targeted = encode_acelp(down_out, channelFrameOutput, cfgs, ncfg, targetMask);
		// print_word16_hex(channelFrameOutput, 432);
		static Word16 cleanFrame[ACELP_DUAL_CHAN_FRAME_SIZE];
		memcpy(cleanFrame, channelFrameOutput, sizeof(cleanFrame));
		if (!targetMask)
			for (int c = 0; c < ncfg; c++)
				corrupt_apply_bitstream(channelFrameOutput, TS7k2_size, cfgs[c], true);  // soft symbols: one per channel bit

		// Visualization info for this frame, consumed when it gets decoded.
		TetraPending pend;
		pend.nflipped = 0;
		pend.nTargeted = targeted;
		pend.paramCorr = pend.bitCorr = false;
		for (int c = 0; c < ncfg; c++) {
			if (cfgs[c].mode >= kCorruptLsp && cfgs[c].mode <= kCorruptFreeze) pend.paramCorr = true;
			if (cfgs[c].mode >= kCorruptBitFlips) pend.bitCorr = true;
		}
		for (int i = 0; i < TS7k2_size; i++) {
			// channel-coded symbols are soft +/- values
			const bool b = channelFrameOutput[i] > 0, b0 = cleanFrame[i] > 0;
			const bool flipped = channelFrameOutput[i] != cleanFrame[i];
			pend.nflipped += flipped;
			pend.bits[i] = VIZ_BIT(b, kVizBitFec, flipped || (b != b0));
		}
		{
			double e = 1e-12;
			for (int i = 0; i < ACELP_DUAL_CHAN_AUDIO_SIZE; i++) e += (double)down_out[i] * down_out[i];
			pend.inDb = (float)(10.0 * log10(e / ACELP_DUAL_CHAN_AUDIO_SIZE));
		}
		
        // print_word16_hex(channelFrameOutput, ACELP_DUAL_CHAN_FRAME_SIZE);
		// Write downsampled to ring1
		size_t space_needed = sizeof(Word16)*ACELP_DUAL_CHAN_FRAME_SIZE;
		if (cat_ringbuffer_write_space(encoded_frame_queue) >= space_needed) {
			cat_ringbuffer_write(encoded_frame_queue, (char *)channelFrameOutput, space_needed);
			if (tetraPendCount < PIPELINE_BUFFER_FRAMES) {
				tetraPending[(tetraPendHead + tetraPendCount) % PIPELINE_BUFFER_FRAMES] = pend;
				tetraPendCount++;
			}
			// DEBUG_PRINTF("sending frame:\n");
			// print_word16_hex(channelFrameOutput, 432);
		}
		available = cat_ringbuffer_read_space(ring_48k_incoming);
    	samples_available = available / sizeof(float);
	}

}
void CheetahDSP::run(const float** inputs, float** outputs, uint32_t frames) {
    const float* const in = inputs[0];
    float* const out = outputs[0];
	//
	// Realtime audio thread: ONLY move samples in/out of the lock-free rings.
	// The expensive codec encode/decode + resampling happens on the background
	// thread (process_buff_ring1_audio + threadFunction). Doing the ACELP encode
	// here previously spiked the RT callback every ~60ms and caused xruns/cracks.
	cat_ringbuffer_write(ring_48k_incoming, (char *)in, frames * sizeof(float));

    // Output: read from the ring, or output silence on underrun. Wait until the
    // background thread has built up OUTPUT_PREBUFFER_FRAMES of slack before we
    // start playing, so ordinary jitter doesn't immediately drain the ring.
    size_t to_read = sizeof(float) * frames;
    // TETRA delivers 60 ms frames, HD ACELP 20 ms frames: size the pre-buffer
    // per codec so HD gets its lower latency. On a codec switch drop whatever
    // the old codec left in the ring (we are its reader, so this is RT-safe)
    // and prime again.
    const int published = publishedType.load(std::memory_order_relaxed);
    const int codecType = published >= 0 ? published : codecTypeForParams();
    if (codecType != rtCodecType) {
        if (rtCodecType != -1)
            cat_ringbuffer_read_advance(pcm_output_buffer, cat_ringbuffer_read_space(pcm_output_buffer));
        rtCodecType = codecType;
        outputPrimed = false;
    }
    // codec frame length at the host rate: TETRA 60 ms slot, T+ 30 ms, HD 20 ms
    const size_t frame_bytes = codecType == 0
        ? sizeof(float) * ACELP_DUAL_CHAN_AUDIO_SIZE * UPSAMPLE_RATIO
        : sizeof(float) * (size_t)ceil(sampleRate * (codecType >= 4 ? 0.03 : 0.02));
    const size_t prebuffer_bytes = frame_bytes * OUTPUT_PREBUFFER_FRAMES;
    if (!outputPrimed) {
        if (cat_ringbuffer_read_space(pcm_output_buffer) >= prebuffer_bytes)
            outputPrimed = true;
    }
    if (outputPrimed && cat_ringbuffer_read_space(pcm_output_buffer) >= to_read) {
        cat_ringbuffer_read(pcm_output_buffer, (char *)out, to_read);
		// DEBUG_PRINTF("output ringbuffer serving %lu samples\n", frames);
    } else {
        memset(out, 0, to_read); // underrun fallback (or still pre-buffering)
    }
    // apply gain against all samples
    // for (uint32_t i=0; i < frames; ++i) {
    //     float gainval = smooth_gain->process(gain);
    //     outR[i] = monoOutput[i] * gainval;
    // }

  /*const ScopedDenormalDisable sdd;
  for (uint32_t index = 0; index < paramCount; index++) {
    if (d_isNotEqual(oldParams[index], newParams[index])) {
      oldParams[index] = newParams[index];
      float value = newParams[index];

      switch(index) {
        case           paramDry: dryLevel        = (value / 100.0); break;
        case         paramEarly: earlyLevel      = (value / 100.0); break;
        case     paramEarlySend: early_send       = (value / 100.0); break;
        case          paramLate: lateLevel       = (value / 100.0); break;
        case          paramSize: early.setRSFactor  (value /  10.0);
                                 late.setRSFactor   (value /  10.0);
                                 late.setbassboost( newParams[paramBoost] / 20.0 / pow(newParams[paramDecay], 1.5) * (newParams[paramSize] / 10.0) ); break;
        case         paramWidth: early.setwidth     (value / 120.0);
                                 late.setwidth      (value / 100.0); break;
        case      paramPredelay:
          // Freeverb doesn't handle zero predelay properly
          // Instead of modifying the library, avoid it here
          if (value < 0.1) {
            value = 0.1;
          }
          late.setPreDelay   (value);
          break;
        case         paramDecay: late.setrt60       (value);
                                 late.setbassboost( newParams[paramBoost] / 20.0 / pow(newParams[paramDecay], 1.5) * (newParams[paramSize] / 10.0) ); break;
        case       paramDiffuse: late.setidiffusion1(value / 120.0);
                                 late.setodiffusion1(value / 120.0); break;
        case          paramSpin: late.setspin       (value);
                                 late.setspin2      (std::sqrt(100.0 - (10.0 - value) * (10.0 - value)) / 2.0);
                                 break;
        case        paramWander: late.setwander     (value / 200.0 + 0.1);
                                 late.setwander2    (value / 200.0 + 0.1); break;
        case     paramInHighCut: setInputLPF        (value);         break;
        case     paramEarlyDamp: early.setoutputlpf (value);         break;
        case      paramLateDamp: late.setdamp       (value);
                                 late.setoutputdamp (value);         break;
        case         paramBoost: late.setbassboost( newParams[paramBoost] / 20.0 / pow(newParams[paramDecay], 1.5) * (newParams[paramSize] / 10.0)  ); break;
        case      paramBoostLPF: late.setdamp2      (newParams[paramBoostLPF]); break;
        case      paramInLowCut: setInputHPF        (value);         break;
      }
    }
  }

  for (uint32_t offset = 0; offset < frames; offset += BUFFER_SIZE) {
    long int buffer_frames = frames - offset < BUFFER_SIZE ? frames - offset : BUFFER_SIZE;

    for (uint32_t i = 0; i < buffer_frames; i++) {
      filtered_input_buffer[0][i] = input_lpf_0.process(input_hpf_0.process(inputs[0][offset + i]));
      filtered_input_buffer[1][i] = input_lpf_1.process(input_hpf_1.process(inputs[1][offset + i]));
    }

    early.processreplace(
      const_cast<float *>(filtered_input_buffer[0]),
      const_cast<float *>(filtered_input_buffer[1]),
      early_out_buffer[0],
      early_out_buffer[1],
      buffer_frames);
    
    for (uint32_t i = 0; i < buffer_frames; i++) {
      late_in_buffer[0][i] = early_send * early_out_buffer[0][i] + filtered_input_buffer[0][i];
      late_in_buffer[1][i] = early_send * early_out_buffer[1][i] + filtered_input_buffer[1][i];
    }
    
    late.processreplace(
      const_cast<float *>(late_in_buffer[0]),
      const_cast<float *>(late_in_buffer[1]),
      late_out_buffer[0],
      late_out_buffer[1],
      buffer_frames);

    for (uint32_t i = 0; i < buffer_frames; i++) {
      outputs[0][offset + i] = dryLevel   * inputs[0][offset + i];
      outputs[1][offset + i] = dryLevel   * inputs[1][offset + i];
    }
    
    if( earlyLevel > 0.0 ){
      for (uint32_t i = 0; i < buffer_frames; i++) {
        outputs[0][offset + i] += earlyLevel * early_out_buffer[0][i];
        outputs[1][offset + i] += earlyLevel * early_out_buffer[1][i];
      }
    }
    
    if( lateLevel > 0.0 ){
      for (uint32_t i = 0; i < buffer_frames; i++) {
        outputs[0][offset + i] += lateLevel  * late_out_buffer[0][i];
        outputs[1][offset + i] += lateLevel  * late_out_buffer[1][i];
      }
    }
  }*/
}

void CheetahDSP::threadFunction() {
	DEBUG_PRINTF("entering background thread\n");
	threadRunning.store(true, std::memory_order_release);
#if defined(__SSE__)
	// Flush denormals: decaying codec filter memories during silence would
	// otherwise hit the slow denormal path.
	_mm_setcsr(_mm_getcsr() | 0x8040);
#endif

	Word16 playback_frame[ACELP_DUAL_CHAN_FRAME_SIZE];         // Temp buffer for each new frame
	// Word16 playback_frame[ACELP_DUAL_CHAN_AUDIO_SIZE];         // TEST NEW!!
    Word16 last_good_frame[ACELP_DUAL_CHAN_AUDIO_SIZE] = { 0 }; // Backup of last valid frame
	hex_to_word16_array(fuckFrame, last_good_frame, ACELP_DUAL_CHAN_FRAME_SIZE);
	//
    float decode_buf[ACELP_DUAL_CHAN_AUDIO_SIZE];
    float upsample_buf[ACELP_DUAL_CHAN_AUDIO_SIZE * UPSAMPLE_RATIO];
	//
	// Start with no valid frame: wait for the encoder to deliver a real frame
	// before decoding. Otherwise we'd decode an all-zero / degenerate bitstream
	// (the hardcoded fuckFrame is the old 432-word size and no longer loads),
	// which produces garbage or crashes in the TETRA decoder.
	int has_valid_frame = 0;
	//
    // Last fully-upsampled PCM frame, kept so we can conceal a genuine underrun
    // by repeating audio rather than re-decoding a held bitstream (re-decoding
    // the same TETRA frame lets the decoder's adaptive state decay to silence,
    // which produced the ~30ms silence notches at the frame rate).
    float last_pcm[ACELP_DUAL_CHAN_AUDIO_SIZE * UPSAMPLE_RATIO] = { 0 };
    int   have_last_pcm = 0;

    const size_t bytes_needed = sizeof(float) * ACELP_DUAL_CHAN_AUDIO_SIZE * UPSAMPLE_RATIO;
    // Only conceal (repeat the last PCM) when the output ring is about to run
    // dry — i.e. genuine starvation. Crucially we do NOT decode ahead just to
    // reach a fill target: the decoder must stay paced 1:1 with the encoder,
    // otherwise it drains the frame queue and starts repeating frames, gating
    // the audio at the frame rate.
    const size_t underrun_bytes = bytes_needed; // < 1 output frame buffered

    while (!stopThread.load(std::memory_order_acquire)) {
        int type = codecTypeForParams();
        const int netMode = (int)lround(newParams[paramNetMode]);
        // a receiver runs whatever codec the stream it hears uses
        if (netMode == 2) type = hd->rxType ? hd->rxType : (type ? type : 2);
        publishedType.store(type, std::memory_order_relaxed);
        if (type != 0) {
            // HD ACELP / TETRA+: encode + decode inline, one frame at a time.
            if (type != hdActiveType) hdConfigure(type);
            if (!processHd())
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        hdActiveType = 0;
        hd->net.configure(false, false, 1); // the ETSI TETRA path is not networked

        // Drain freshly-arrived input and encode it into the frame queue. This
        // is the heavy ACELP work, kept here on the background thread instead of
        // in the realtime audio callback (which caused periodic xruns/cracks).
        process_buff_ring1_audio(0);

        // Need room in the output ring before we produce another frame.
        if (cat_ringbuffer_write_space(pcm_output_buffer) < bytes_needed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        if (cat_ringbuffer_read_space(encoded_frame_queue) >= sizeof(playback_frame)) {
            // A real encoded frame is available: decode it (1:1 with encoder).
			cat_ringbuffer_read(encoded_frame_queue, (char *)playback_frame, sizeof(playback_frame));
			memcpy(&last_good_frame, &playback_frame, sizeof(playback_frame));
            has_valid_frame = 1;

            decode_acelp(playback_frame, decode_buf);
            if (tetraPendCount > 0) {
                tetraCurrent = tetraPending[tetraPendHead];
                tetraPendHead = (tetraPendHead + 1) % PIPELINE_BUFFER_FRAMES;
                tetraPendCount--;
            }
            publishTetraViz(decode_buf, ACELP_DUAL_CHAN_AUDIO_SIZE);
            spx_uint32_t in_len = ACELP_DUAL_CHAN_AUDIO_SIZE;
            spx_uint32_t out_len = ACELP_DUAL_CHAN_AUDIO_SIZE * UPSAMPLE_RATIO;
            speex_resampler_process_float(resampler_up, 0, decode_buf, &in_len, upsample_buf, &out_len);

            memcpy(last_pcm, upsample_buf, bytes_needed);
            have_last_pcm = 1;
            cat_ringbuffer_write(pcm_output_buffer, (char *)upsample_buf, bytes_needed);
        } else if (have_last_pcm &&
                   cat_ringbuffer_read_space(pcm_output_buffer) < underrun_bytes) {
            // No fresh frame AND the ring is about to underrun: conceal by
            // repeating the last decoded PCM frame to avoid an audible gap.
            cat_ringbuffer_write(pcm_output_buffer, (char *)last_pcm, bytes_needed);
        } else {
            // No fresh frame but the ring still has slack: wait for the encoder
            // to deliver the next real frame instead of running ahead.
        	std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
	}
	//
	threadRunning.store(false, std::memory_order_release);
	DEBUG_PRINTF("bg: leaving thread\n");
	return;
}

#pragma region "Visualization"

// Plot frequencies (normalized angular) for the log-like display axis.
static void lpcVizOmegas(int fs, double *w) {
	const double nyq = fs * 0.5;
	for (int i = 0; i < LPC_VIZ_BINS; i++)
		w[i] = M_PI * lpc_viz_freq((i + 0.5) / LPC_VIZ_BINS, nyq) / nyq;
}

// Both envelopes are shifted by the reference envelope's mean so they share
// one dB scale; `modGainDb` is the decoder's loudness compensation.
static void lpcVizEnvelopes(LpcVizFrame &f, const double *aRef, const double *aMod, int M, double modGainDb) {
	double w[LPC_VIZ_BINS];
	lpcVizOmegas(f.fs, w);
	hdacelp::lpc_response_db(aRef, M, w, LPC_VIZ_BINS, f.envRef);
	hdacelp::lpc_response_db(aMod, M, w, LPC_VIZ_BINS, f.envMod);
	double mean = 0.0;
	for (int i = 0; i < LPC_VIZ_BINS; i++) mean += f.envRef[i];
	mean /= LPC_VIZ_BINS;
	for (int i = 0; i < LPC_VIZ_BINS; i++) {
		f.envRef[i] -= (float)mean;
		f.envMod[i] += (float)(modGainDb - mean);
	}
}

static float levelDb(const float *x, int n) {
	double e = 1e-12;
	for (int i = 0; i < n; i++) e += (double)x[i] * x[i];
	return (float)(10.0 * log10(e / (n > 0 ? n : 1)));
}

// History envelope: |1/A| on the fixed 50 Hz..24 kHz log axis, mean-removed
// and lifted to the frame's output level, so the scrolling display reads like
// a spectrogram of the synthesis filter.
static void recordEnvelope(CodecVizRecord &r, const double *a, int M, int fs, float outDb) {
	double w[VIZ_ENV_BINS];
	float db[VIZ_ENV_BINS];
	int nIn = 0;
	for (int i = 0; i < VIZ_ENV_BINS; i++) {
		const double f = VIZ_ENV_FMIN * pow(VIZ_ENV_FMAX / VIZ_ENV_FMIN, (i + 0.5) / VIZ_ENV_BINS);
		if (f < fs * 0.5) nIn = i + 1;
		w[i] = 2.0 * M_PI * f / fs;
	}
	hdacelp::lpc_response_db(a, M, w, nIn, db);
	double mean = 0.0;
	for (int i = 0; i < nIn; i++) mean += db[i];
	mean /= (nIn > 0 ? nIn : 1);
	for (int i = 0; i < VIZ_ENV_BINS; i++)
		r.env[i] = i < nIn ? (float)(db[i] - mean + outDb) : VIZ_ENV_NONE;
}

uint16_t CheetahDSP::heldMidiCorruptKeys() const {
	return keyActiveMask.load(std::memory_order_relaxed);
}

void CheetahDSP::publishViz(const hdacelp::FrameTrace &t, const hdacelp::Config &c, const hdacelp::Config &cd,
                            const float *in, const float *pcm, bool paramCorr, bool bitCorr,
                            bool lpEnc, bool lpDec, bool excActive, float guardDb) {
	const HdState &h = *hd;
	const bool tp = c.profile == hdacelp::PROFILE_TETRA_PLUS;
	const bool chanView = h.vizChannel; // channel symbols exist only for local T+ frames
	// --- current-frame panels
	static LpcVizFrame f; // codec thread only
	const int M = t.order;
	f.fs = c.fs;
	f.order = M;
	f.frameLen = t.frameLen;
	f.estimated = false;
	f.modActive = t.modActive;
	lpcVizEnvelopes(f, t.aRef, t.modActive ? t.aMod : t.aRef, M,
	                t.modActive ? 20.0 * log10(t.compGain > 1e-6f ? t.compGain : 1e-6f) : 0.0);
	for (int i = 0; i < M; i++) {
		f.lsfRef[i] = (float)(t.lsfRef[i] / M_PI * c.fs * 0.5);
		f.lsfMod[i] = (float)(t.lsfMod[i] / M_PI * c.fs * 0.5);
	}
	memcpy(f.excAdaptive, t.excAdaptive, sizeof(float) * t.frameLen);
	memcpy(f.excFixed, t.excFixed, sizeof(float) * t.frameLen);
	for (int s = 0; s < LPC_VIZ_NSUB; s++) {
		f.pitch[s] = t.pitch[s];
		f.gp[s] = t.gp[s];
		f.gcDb[s] = t.gcDb[s];
	}
	lpc_viz_write(lpcViz, f);

	// --- history record
	static CodecVizRecord r;
	r.kind = tp ? 2 : 1;
	r.math = (uint8_t)c.math;
	r.mode = (uint8_t)c.mode;
	r.order = (uint8_t)M;
	r.estimated = 0;
	r.lpEnc = lpEnc;
	r.lpDec = lpDec;
	r.decMode = (uint8_t)cd.mode;
	r.decMath = (uint8_t)cd.math;
	r.guardDb = guardDb;
	r.excActive = excActive;
	r.paramCorr = paramCorr;
	r.bitCorr = bitCorr;
	r.heldKeys = heldMidiCorruptKeys();
	r.fs = c.fs;
	r.frameMs = c.frameMs();
	r.kbps = (float)(c.bitrate() / 1000.0);
	r.fec = tp && chanView ? (uint8_t)h.chanFec : 255;
	r.bfi = tp && h.bfi;
	r.nbad = (uint16_t)(tp ? h.nbad : 0);
	r.srcBits = c.numBits();
	r.target = (uint8_t)lround(newParams[paramCorruptTarget]);
	r.nTargeted = h.nTargeted;
	r.gainVq = c.gainVq;
	r.netMode = (uint8_t)lround(newParams[paramNetMode]);
	r.netOk = h.net.ok();
	r.netPlaying = h.jbStarted;
	r.netConceal = h.netConceal;
	{
		int depth = 0;
		for (int i = 0; i < 32; i++) depth += h.jbValid[i];
		r.netDepth = (uint8_t)depth;
	}
	r.netChannel = (uint8_t)lround(newParams[paramNetChannel]);
	r.netLossPct = h.jbLossPct;
	r.netStream = r.netMode == 1 ? h.net.streamId() : h.net.lockedStream.load();
	r.netTx = h.net.txPackets.load();
	r.netRx = h.net.rxPackets.load();
	r.inDb = levelDb(in, c.frameLen);
	r.outDb = levelDb(pcm, c.frameLen);
	recordEnvelope(r, t.modActive ? t.aMod : t.aRef, M, c.fs, r.outDb);
	for (int s = 0; s < VIZ_NSUB; s++) {
		r.f0[s] = t.pitch[s] > 0 ? (float)c.fs / t.pitch[s] : 0.0f;
		r.gp[s] = t.gp[s];
		r.gcDb[s] = t.gcDb[s];
		const int np = t.nPulses[s] < VIZ_MAX_PULSES ? t.nPulses[s] : VIZ_MAX_PULSES;
		r.npulses[s] = (uint8_t)np;
		for (int pi = 0; pi < np; pi++) {
			const int v = t.pulses[s][pi];
			const int pos = (v < 0 ? -v : v) - 1;
			const int q = 1 + pos * 1000 / c.subLen;
			r.pulse[s][pi] = (int16_t)(v < 0 ? -q : q);
		}
	}
	int bit = 0, flips = 0;
	if (chanView) {
		// channel symbols as sent over the "air", coloured by protection class
		for (; bit < h.nsym && bit < VIZ_MAX_BITS; bit++) {
			const bool v = h.sym[bit] < 0, v0 = h.symClean[bit] < 0;
			const bool hit = h.sym[bit] != h.symClean[bit];
			const int sc = h.chan.symbolClass(bit);
			const uint8_t cat = sc == hdacelp::TP_SYM_CLASS1 ? kVizBitFec
			                  : (sc == hdacelp::TP_SYM_CLASS0_CODED ? kVizBitFec0 : kVizBitPulse);
			flips += hit;
			r.bits[bit] = VIZ_BIT(v, cat, hit || v != v0);
		}
	} else {
		hdacelp::Field fl[hdacelp::HD_MAX_FIELDS];
		c.layout(fl);
		const Word16 *words = h.wordsRx, *wordsClean = h.wordsClean;
		for (int i = 0; i < c.numFields() && bit < VIZ_MAX_BITS; i++) {
			static const uint8_t catMap[6] = { kVizBitLsf, kVizBitPitch, kVizBitPulse, kVizBitSign, kVizBitShift, kVizBitGain };
			const uint8_t cat = catMap[fl[i].cat < 6 ? fl[i].cat : 0];
			for (int b = 0; b < fl[i].bits && bit < VIZ_MAX_BITS; b++, bit++) {
				const int v  = ((uint16_t)words[bit >> 4] >> (15 - (bit & 15))) & 1;
				const int v0 = ((uint16_t)wordsClean[bit >> 4] >> (15 - (bit & 15))) & 1;
				flips += v != v0;
				r.bits[bit] = VIZ_BIT(v, cat, v != v0);
			}
		}
	}
	r.nbits = bit;
	r.nflipped = flips;
	vizFeed.push(r);
}

// The TETRA decoder is fixed-point ETSI code with its LPC buried in globals,
// so re-analyse its 8 kHz output instead: order-10 LPC + LP residual, pitch by
// autocorrelation, and the strongest residual samples standing in for pulses.
void CheetahDSP::publishTetraViz(const float *pcm, int n) {
	static LpcVizFrame f; // codec thread only
	static float win[ACELP_DUAL_CHAN_AUDIO_SIZE];
	static bool winReady = false;
	const int M = 10;
	if (n > ACELP_DUAL_CHAN_AUDIO_SIZE) n = ACELP_DUAL_CHAN_AUDIO_SIZE;
	if (!winReady) {
		for (int i = 0; i < ACELP_DUAL_CHAN_AUDIO_SIZE; i++)
			win[i] = (float)(0.54 - 0.46 * cos(2.0 * M_PI * i / (ACELP_DUAL_CHAN_AUDIO_SIZE - 1)));
		winReady = true;
	}
	double r[M + 1], a[M + 1], lsf[M];
	hdacelp::autocorr(pcm, win, n, M, r);
	hdacelp::lag_window(r, M, TETRA_SampleRate);
	if (!hdacelp::levinson(r, M, a)) {
		a[0] = 1.0;
		for (int i = 1; i <= M; i++) a[i] = 0.0;
	}
	f.fs = TETRA_SampleRate;
	f.order = M;
	f.frameLen = n;
	f.estimated = true;
	f.modActive = false;
	lpcVizEnvelopes(f, a, a, M, 0.0);
	if (hdacelp::a2lsf(a, M, lsf)) {
		for (int i = 0; i < M; i++) f.lsfRef[i] = f.lsfMod[i] = (float)(lsf[i] / M_PI * TETRA_SampleRate * 0.5);
	} else {
		f.order = 0; // no LSF ticks this frame
	}
	for (int i = 0; i < n; i++) {
		double e = pcm[i];
		for (int k = 1; k <= M && k <= i; k++) e += a[k] * pcm[i - k];
		f.excAdaptive[i] = (float)e;
		f.excFixed[i] = 0.0f;
	}

	static CodecVizRecord rec;
	rec.kind = 0;
	rec.math = 1;          // the ETSI codec is fixed point
	rec.mode = 0;
	rec.order = M;
	rec.estimated = 1;
	rec.lpEnc = rec.lpDec = rec.excActive = 0;
	rec.decMode = 0;
	rec.decMath = 1;
	rec.guardDb = 0.0f;
	rec.fec = 255;
	rec.bfi = 0;
	rec.nbad = 0;
	rec.srcBits = 2 * 137;
	rec.target = (uint8_t)lround(newParams[paramCorruptTarget]);
	rec.nTargeted = tetraCurrent.nTargeted;
	rec.gainVq = 1;     // ETSI: joint energy VQ
	rec.netMode = 0;    // the network link carries HD / TETRA+ only
	rec.paramCorr = tetraCurrent.paramCorr;
	rec.bitCorr = tetraCurrent.bitCorr;
	rec.heldKeys = heldMidiCorruptKeys();
	rec.fs = TETRA_SampleRate;
	rec.frameMs = 60;
	rec.kbps = 2.0f * 137 / 60.0f;   // two 137-bit speech frames per 60 ms
	rec.inDb = tetraCurrent.inDb;
	rec.outDb = levelDb(pcm, n);
	recordEnvelope(rec, a, M, TETRA_SampleRate, rec.outDb);
	const int slot = n / LPC_VIZ_NSUB; // 120 samples = two TETRA subframes
	for (int s = 0; s < LPC_VIZ_NSUB; s++) {
		const float *x = pcm + s * slot;
		// normalized autocorrelation pitch, 54..400 Hz
		float best = 0.0f;
		int bestT = 0;
		for (int T = 20; T <= 147; T++) {
			double num = 0.0, e0 = 1e-9, e1 = 1e-9;
			for (int i = 0; i < slot; i++) {
				const int j = s * slot + i - T;
				const float y = j >= 0 ? pcm[j] : 0.0f;
				num += x[i] * y;
				e0 += x[i] * x[i];
				e1 += y * y;
			}
			const float c = (float)(num / sqrt(e0 * e1));
			if (c > best) { best = c; bestT = T; }
		}
		rec.f0[s] = (best > 0.35f && bestT) ? (float)TETRA_SampleRate / bestT : 0.0f;
		rec.gp[s] = best > 0.0f ? best : 0.0f;
		f.pitch[s] = rec.f0[s] > 0.0f ? bestT : 0;
		f.gp[s] = rec.gp[s];
		f.gcDb[s] = 0.0f;
		// four strongest residual samples per slot
		float res[ACELP_DUAL_CHAN_AUDIO_SIZE / LPC_VIZ_NSUB];
		for (int i = 0; i < slot; i++) res[i] = f.excAdaptive[s * slot + i];
		rec.npulses[s] = 4;
		double eres = 1e-12;
		for (int i = 0; i < slot; i++) eres += res[i] * res[i];
		rec.gcDb[s] = (float)(10.0 * log10(eres / slot));
		for (int pi = 0; pi < 4; pi++) {
			int bi = 0;
			for (int i = 1; i < slot; i++) if (fabsf(res[i]) > fabsf(res[bi])) bi = i;
			const int q = 1 + bi * 1000 / slot;
			rec.pulse[s][pi] = (int16_t)(res[bi] < 0.0f ? -q : q);
			res[bi] = 0.0f;
		}
	}
	lpc_viz_write(lpcViz, f);

	memcpy(rec.bits, tetraCurrent.bits, TS7k2_size);
	rec.nbits = TS7k2_size;
	rec.nflipped = tetraCurrent.nflipped;
	vizFeed.push(rec);
}

#pragma endregion

#pragma region "HD ACELP path"

int CheetahDSP::codecTypeForParams() const {
	long t = lround(newParams[paramCodecType]);
	return t < 0 ? 0 : (t > 5 ? 5 : (int)t);
}

static int typeFs(int type) {
	static const int fs[6] = { 8000, 16000, 32000, 48000, 8000, 16000 };
	return fs[type < 0 ? 0 : (type > 5 ? 5 : type)];
}
static bool typeIsTetraPlus(int type) { return type >= 4; }

static int tpLevelForParams(float bitratePercent) {
	const long l = lround(bitratePercent / 100.0f * (hdacelp::TP_NUM_LEVELS - 1));
	return l < 0 ? 0 : (l >= hdacelp::TP_NUM_LEVELS ? hdacelp::TP_NUM_LEVELS - 1 : (int)l);
}

static int hdModeForParams(float bitratePercent) {
	const long m = lround(bitratePercent / 100.0f * (hdacelp::HD_NUM_MODES - 1));
	return m < 0 ? 0 : (m >= hdacelp::HD_NUM_MODES ? hdacelp::HD_NUM_MODES - 1 : (int)m);
}

static int hdMathForParams(float v) {
	return v >= 0.5f ? hdacelp::MATH_FIXED16 : hdacelp::MATH_FLOAT;
}

static void hdLayoutFields(const hdacelp::Config &c, CorruptField *out) {
	hdacelp::Field f[hdacelp::HD_MAX_FIELDS];
	c.layout(f);
	// scalar gains come in pairs (pitch gain, codebook gain); the indexed
	// codebook has one field per subframe, codebook-gain major (~ dB order)
	bool pitchGain = true;
	for (int i = 0; i < c.numFields(); i++) {
		unsigned char cat = f[i].cat;
		if (cat == hdacelp::CAT_GAIN) {
			if (c.gainVq) cat = kFieldGainPVQ;
			else {
				cat = pitchGain ? kFieldGainPitch : kFieldGain;
				pitchGain = !pitchGain;
			}
		}
		out[i] = { f[i].bits, cat };
	}
}

// Bits that differ between two field arrays (for the targeted-corruption count).
static int fieldBitDiff(const Word16 *a, const Word16 *b, int n) {
	int d = 0;
	for (int i = 0; i < n; i++) d += __builtin_popcount((unsigned)(uint16_t)(a[i] ^ b[i]));
	return d;
}

// Decoder config for a stream produced with `src`: follows the encoder unless
// the codec / algorithm groups are split, then the decoder-side values apply.
hdacelp::Config CheetahDSP::decoderConfigFrom(const hdacelp::Config& src) const {
	hdacelp::Config c = src;
	if (newParams[paramCodecSplit] >= 0.5f) {
		const int math = hdMathForParams(newParams[paramDecMath]);
		const float rate = newParams[paramDecBitrate];
		hdacelp::Config m = src.profile == hdacelp::PROFILE_TETRA_PLUS
			? hdacelp::Config::makeTetraPlus(src.fs, tpLevelForParams(rate), math)
			: hdacelp::Config::make(src.fs, hdModeForParams(rate), math);
		m.preEmph = c.preEmph; m.lsfPred = c.lsfPred; m.gcOffsetDb = c.gcOffsetDb;
		m.sharpenMax = c.sharpenMax; m.gainVq = c.gainVq;
		c = m;
	}
	if (newParams[paramAlgoSplit] >= 0.5f) {
		c.preEmph    = newParams[paramDecPreEmph];
		c.lsfPred    = newParams[paramDecLsfPred];
		c.gcOffsetDb = newParams[paramDecGainOfs];
		c.sharpenMax = newParams[paramDecSharpen];
		c.gainVq     = newParams[paramDecGainVq] >= 0.5f;
	}
	return c;
}

hdacelp::Config CheetahDSP::netFrameConfig(const acenet::NetFrame& f) const {
	hdacelp::Config c = typeIsTetraPlus(f.type)
		? hdacelp::Config::makeTetraPlus(typeFs(f.type), f.mode, f.math)
		: hdacelp::Config::make(typeFs(f.type), f.mode, f.math);
	c.gainVq = f.gainVq != 0;
	c.preEmph = f.preEmph;
	c.lsfPred = f.lsfPred;
	c.gcOffsetDb = f.gcOffsetDb;
	c.sharpenMax = f.sharpenMax;
	return c;
}

// Jitter buffer, clocked by the local audio: one call per codec frame.
int CheetahDSP::netPull(acenet::NetFrame& out) {
	HdState &h = *hd;
	static acenet::NetFrame f;
	const double t = acenet::now_seconds();
	while (h.net.receive(f)) {
		h.jbLastRx = t;
		h.rxType = f.type;
		const int32_t d = (int32_t)(f.seq - h.jbExpected);
		if (h.jbStarted && d < 0) { h.jbLost++; h.jbFrames++; continue; }   // late
		if (h.jbStarted && d >= 32) {                                        // jumped: resync
			for (int i = 0; i < 32; i++) h.jbValid[i] = false;
			h.jbStarted = false;
		}
		h.jb[f.seq % 32] = f;
		h.jbValid[f.seq % 32] = true;
	}
	if (h.jbStarted && t - h.jbLastRx > 1.0) {                               // stream gone
		for (int i = 0; i < 32; i++) h.jbValid[i] = false;
		h.jbStarted = false;
		h.haveRxCfg = false;
	}
	const int frameMs = typeIsTetraPlus(h.rxType) ? 30 : 20;
	const int target = 2 + (int)ceilf(newParams[paramNetJitter] / frameMs);
	int count = 0;
	uint32_t oldest = 0, newest = 0;
	for (int i = 0; i < 32; i++) {
		if (!h.jbValid[i]) continue;
		const uint32_t s = h.jb[i].seq;
		if (count == 0 || (int32_t)(s - oldest) < 0) oldest = s;
		if (count == 0 || (int32_t)(s - newest) > 0) newest = s;
		count++;
	}
	if (!h.jbStarted) {
		if (count < target) return 0;
		h.jbExpected = oldest;
		h.jbStarted = true;
	}
	// Adaptive depth: frames buffered at or after the playout point
	int ahead = 0;
	for (int i = 0; i < 32; i++)
		if (h.jbValid[i] && (int32_t)(h.jb[i].seq - h.jbExpected) >= 0) ahead++;
	if ((int32_t)(newest - h.jbExpected) > target + 8) {
		// far behind (burst, clock drift): jump
		h.jbExpected = newest - (uint32_t)target;
	} else if (ahead > target + 1) {
		// running long: drop one frame to shorten the delay
		const int s = h.jbExpected % 32;
		if (h.jbValid[s] && h.jb[s].seq == h.jbExpected) h.jbValid[s] = false;
		h.jbExpected++;
	} else if (ahead + 1 < target) {
		// running short (jitter went up): hold one frame, i.e. conceal
		// without advancing, to build up depth
		h.jbFrames++;
		return 2;
	}
	const uint32_t want = h.jbExpected++;
	h.jbFrames++;
	const int slot = want % 32;
	int result = 2;
	if (h.jbValid[slot] && h.jb[slot].seq == want) {
		out = h.jb[slot];
		h.jbValid[slot] = false;
		result = 1;
	} else {
		h.jbLost++;
	}
	if (h.jbFrames >= 50) {
		h.jbLossPct = 100.0f * h.jbLost / h.jbFrames;
		h.jbLost = h.jbFrames = 0;
	}
	return result;
}

// Bad-frame concealment (as ETSI) for channel errors and network losses:
// repeat the last good parameters with decaying gains, mute after ~8 bad
// frames. Good frames are remembered.
void CheetahDSP::concealOrRemember(Word16* idx, const hdacelp::Config& cd, bool bad) {
	HdState &h = *hd;
	const int sig = cd.numFields() * 64 + cd.mode * 2 + cd.gainVq;
	if (!bad) {
		h.nbad = 0;
		memcpy(h.lastGood, idx, sizeof(int16_t) * cd.numFields());
		h.haveGood = true;
		h.lastGoodSig = sig;
		return;
	}
	if (!h.haveGood || h.lastGoodSig != sig) return; // nothing compatible to repeat
	h.nbad++;
	hdacelp::Field fl[hdacelp::HD_MAX_FIELDS];
	cd.layout(fl);
	memcpy(idx, h.lastGood, sizeof(int16_t) * cd.numFields());
	bool gp = true;
	for (int k = 0; k < cd.numFields(); k++) {
		if (fl[k].cat != hdacelp::CAT_GAIN) continue;
		if (cd.gainVq) {
			// indexed codebook: lower the gain correction, shrink the pitch gain
			const int cl = std::max(0, (idx[k] >> 3) - 2 * h.nbad);
			const int gl = (int)lrintf((idx[k] & 7) * powf(0.85f, (float)h.nbad));
			idx[k] = (int16_t)(h.nbad > 8 ? 0 : cl * 8 + gl);
			continue;
		}
		if (gp) idx[k] = (int16_t)(h.nbad > 8 ? 0 : lrintf(idx[k] * powf(0.85f, (float)h.nbad)));
		else    idx[k] = (int16_t)(h.nbad > 8 ? 0 : std::max(0, idx[k] - 3 * h.nbad));
		gp = !gp; // gain fields alternate: pitch gain, codebook gain
	}
}

hdacelp::Config CheetahDSP::hdConfigFor(int type, bool dec) const {
	if (dec) return decoderConfigFrom(hdConfigFor(type, false));
	const float rate = newParams[paramCodecBitrate];
	const int math = hdMathForParams(newParams[paramHdMath]);
	hdacelp::Config c = typeIsTetraPlus(type)
		? hdacelp::Config::makeTetraPlus(typeFs(type), tpLevelForParams(rate), math)
		: hdacelp::Config::make(typeFs(type), hdModeForParams(rate), math);
	c.preEmph    = newParams[paramPreEmph];
	c.lsfPred    = newParams[paramLsfPred];
	c.gcOffsetDb = newParams[paramGainOfs];
	c.sharpenMax = newParams[paramSharpen];
	c.gainVq     = newParams[paramGainVq] >= 0.5f;
	return c;
}

void CheetahDSP::hdCloseResamplers() {
	if (hd->down) speex_resampler_destroy(hd->down);
	if (hd->up) speex_resampler_destroy(hd->up);
	hd->down = hd->up = nullptr;
}

void CheetahDSP::hdConfigure(int type) {
	HdState &h = *hd;
	const int fs = typeFs(type);
	h.cfg = hdConfigFor(type, false);
	h.decCfg = hdConfigFor(type, true);
	h.guardGain = 1.0f;
	h.chanMode = h.chanFec = -1;
	h.haveGood = false;
	h.nbad = 0;
	h.bfi = false;
	h.enc.init(h.cfg);
	h.dec.init(h.decCfg);
	hdLayoutFields(h.cfg, h.fields);
	h.cst.haveFreeze = 0;
	h.cst.gainOfs = 0.0f;
	h.nTargeted = 0;
	h.inFill = 0;
	// Input that piled up while the codec was being switched would otherwise
	// stay in the pipeline as permanent extra latency: drop it (we are the
	// reader of this ring).
	cat_ringbuffer_read_advance(ring_48k_incoming, cat_ringbuffer_read_space(ring_48k_incoming) / sizeof(float) * sizeof(float));
	hdCloseResamplers();
	h.hostRate = sampleRate;
	int err = 0;
	h.down = speex_resampler_init(1, (spx_uint32_t)sampleRate, (spx_uint32_t)fs, 5, &err);
	h.up   = speex_resampler_init(1, (spx_uint32_t)fs, (spx_uint32_t)sampleRate, 5, &err);
	hdActiveType = type;
	DEBUG_PRINTF("%s: %d Hz, order %d, %.1f kbps\n", typeIsTetraPlus(type) ? "TETRA+" : "HD ACELP",
	             fs, h.cfg.order, h.cfg.bitrate() / 1000.0);
}

// Returns true if at least one block was processed.
bool CheetahDSP::processHd() {
	HdState &h = *hd;
	if (h.hostRate != sampleRate || !h.down || !h.up) hdConfigure(hdActiveType);
	const bool tp = typeIsTetraPlus(hdActiveType);
	const int N = h.cfg.frameLen;

	static const int MAX_HOST_BLOCK = 4096;
	size_t hostBlock = (size_t)ceil(sampleRate / 50.0); // 20 ms at the host rate
	if (hostBlock > MAX_HOST_BLOCK) hostBlock = MAX_HOST_BLOCK;
	const size_t outRoom = sizeof(float) * (hostBlock + 64);

	float hostIn[MAX_HOST_BLOCK];
	float hostOut[MAX_HOST_BLOCK * 3];
	float pcm[hdacelp::HD_MAX_FRAME];
	Word16 idx[hdacelp::HD_MAX_FIELDS];
	Word16 words[hdacelp::HD_MAX_WORDS];

	bool did = false;
	while (cat_ringbuffer_read_space(ring_48k_incoming) >= hostBlock * sizeof(float) &&
	       cat_ringbuffer_write_space(pcm_output_buffer) >= outRoom) {
		cat_ringbuffer_read(ring_48k_incoming, (char *)hostIn, hostBlock * sizeof(float));
		spx_uint32_t inLen = (spx_uint32_t)hostBlock;
		spx_uint32_t outLen = (spx_uint32_t)(sizeof(h.inFifo) / sizeof(float) - h.inFill);
		speex_resampler_process_float(h.down, 0, hostIn, &inLen, h.inFifo + h.inFill, &outLen);
		h.inFill += (int)outLen;
		did = true;

		while (h.inFill >= N) {
			// ---- network role for this frame
			const int netMode = (int)lround(newParams[paramNetMode]);
			const bool netSend = netMode == 1 || netMode == 3;
			const bool netRecv = netMode == 2 || netMode == 3;
			h.net.setImpairment(newParams[paramNetLoss] / 100.0f, newParams[paramNetJitter]);
			h.net.configure(netSend, netRecv, (int)lround(newParams[paramNetChannel]));
			// a receiver follows the sender's codec type: hand back to the
			// thread loop, which reconfigures for it
			if (netMode == 2 && h.rxType != 0 && h.rxType != hdActiveType) return did;

			// Encoder config. Mode, math and constant changes are frame-
			// synchronous and keep the codec state (no reset click).
			const hdacelp::Config ce = hdConfigFor(hdActiveType, false);
			if (ce.mode != h.cfg.mode || ce.gainVq != h.cfg.gainVq) {
				hdLayoutFields(ce, h.fields);
				h.cst.haveFreeze = 0;
			}
			h.cfg = ce;
			h.enc.reconfigure(ce);

			// LP modification: STAGE 0 = decoder, 1 = encoder, 2 = split
			// (main set in the encoder, decoder set in the decoder).
			auto lpSet = [this](uint32_t w, uint32_t d, uint32_t o, uint32_t sm, uint32_t fr, uint32_t cr,
			                    uint32_t tp, uint32_t rs, uint32_t bd, uint32_t mi, uint32_t ji) {
				hdacelp::LpMod m;
				m.warp   = newParams[w] / 100.0f;
				m.depth  = newParams[d] / 100.0f;
				m.order  = newParams[o];
				m.smooth = newParams[sm] / 100.0f;
				m.freeze = newParams[fr] / 100.0f;
				m.crush  = newParams[cr] / 100.0f;
				m.taper     = newParams[tp] / 100.0f;
				m.resonance = newParams[rs] / 100.0f;
				m.band      = newParams[bd];
				m.mirror    = newParams[mi] / 100.0f;
				m.jitter    = newParams[ji] / 100.0f;
				return m;
			};
			const int stage = (int)lround(newParams[paramLpStage]);
			const hdacelp::LpMod mainSet = lpSet(paramLpWarp, paramLpDepth, paramLpOrder, paramLpSmooth, paramLpFreeze, paramLpCrush,
			                                     paramLpTaper, paramLpResonance, paramLpBand, paramLpMirror, paramLpJitter);
			hdacelp::LpMod encMod, decMod;
			if (stage == 1) encMod = mainSet;
			else if (stage == 2) {
				encMod = mainSet;
				decMod = lpSet(paramDecLpWarp, paramDecLpDepth, paramDecLpOrder, paramDecLpSmooth, paramDecLpFreeze, paramDecLpCrush,
				               paramDecLpTaper, paramDecLpResonance, paramDecLpBand, paramDecLpMirror, paramDecLpJitter);
			} else decMod = mainSet;
			// Momentary LP keys (see MIDI_CORRUPT_BASE_NOTE) act where the
			// sound is resynthesized: the decoder (or the encoder in ENC stage).
			updateKeyEnvelopes(ce.frameMs() / 1000.0f);
			hdacelp::LpMod &keyTarget = stage == 1 ? encMod : decMod;
			if (keyEnv[10].e > 0.0f) keyTarget.warp = fminf(1.0f, keyTarget.warp + keyEnv[10].e);
			{   // envelope-freeze key: full velocity depth while held, then fade
				const float fz = keyEnv[11].held ? keyEnv[11].peak : keyEnv[11].e;
				if (fz > keyTarget.freeze) keyTarget.freeze = fz;
			}
			h.enc.setLpMod(encMod);

			// Decoder-side excitation modification
			hdacelp::ExcMod exc;
			exc.pitchRatio = powf(2.0f, newParams[paramExcPitch] / 12.0f);
			exc.gpScale    = newParams[paramExcVoice] / 100.0f;
			exc.gcScale    = powf(10.0f, newParams[paramExcNoise] / 20.0f);

			float inFrame[hdacelp::HD_MAX_FRAME];
			memcpy(inFrame, h.inFifo, sizeof(float) * N);
			h.inFill -= N;
			memmove(h.inFifo, h.inFifo + N, sizeof(float) * h.inFill);

			CorruptCfg cfgs[MAX_CORRUPT_CFGS];
			const int ncfg = corruptCfgs(cfgs);
			bool paramCorr = false, bitCorr = false;
			for (int c = 0; c < ncfg; c++) {
				paramCorr |= cfgs[c].mode >= kCorruptLsp && cfgs[c].mode <= kCorruptFreeze;
				bitCorr   |= cfgs[c].mode >= kCorruptBitFlips;
			}
			// TARGET: bitstream modes hit only the selected codec fields, after
			// the channel (behind the FEC), instead of the whole bitstream.
			const int target = (int)lround(newParams[paramCorruptTarget]);
			const unsigned targetMask = corrupt_target_mask(target);
			// ---- stage 1: local encode (+ corruption, + TETRA+ channel)
			hdacelp::Config src = ce;       // config of the frame that gets decoded
			bool lost = false, silent = false;
			h.bfi = false;
			h.vizChannel = false;
			if (netMode != 2) {
				h.enc.encode(inFrame, idx);
				memset(h.wordsClean, 0, sizeof(h.wordsClean));
				hdacelp::pack_words(ce, idx, h.wordsClean);
				for (int c = 0; c < ncfg; c++)
					corrupt_apply_fields(idx, h.fields, ce.numFields(), cfgs[c], &h.cst);
				// zeroed buffer: a decoder with a larger layout reads zeros past the frame
				memset(words, 0, sizeof(words));
				const int nw = hdacelp::pack_words(ce, idx, words);
				if (tp) {
					// TETRA+: protected channel (CRC + convolutional code + interleaving)
					const int fec = (int)lround(newParams[paramFec]);
					if (fec != h.chanFec || ce.mode * 2 + ce.gainVq != h.chanMode) {
						h.chan.configure(ce, fec);
						h.chanFec = fec;
						h.chanMode = ce.mode * 2 + ce.gainVq;
					}
					h.nsym = h.chan.symbols();
					h.chan.encode(words, h.sym);
					memcpy(h.symClean, h.sym, sizeof(int16_t) * h.nsym);
					if (!targetMask)
						for (int c = 0; c < ncfg; c++)
							corrupt_apply_bitstream(h.sym, h.nsym, cfgs[c], true);
					h.bfi = !h.chan.decode(h.sym, words);
					h.vizChannel = true;
				} else if (!targetMask) {
					for (int c = 0; c < ncfg; c++)
						corrupt_apply_bitstream(words, nw, cfgs[c]);
				}
				// ---- stage 2a: send the frame as it would reach the decoder
				if (netSend) {
					static acenet::NetFrame nf;
					nf.stream = h.net.streamId();
					nf.seq = h.txSeq++;
					nf.type = (uint8_t)hdActiveType;
					nf.mode = (uint8_t)ce.mode;
					nf.math = (uint8_t)ce.math;
					nf.gainVq = ce.gainVq;
					nf.preEmph = ce.preEmph;
					nf.lsfPred = ce.lsfPred;
					nf.gcOffsetDb = ce.gcOffsetDb;
					nf.sharpenMax = ce.sharpenMax;
					nf.nbits = (uint16_t)ce.numBits();
					memcpy(nf.words, words, sizeof(nf.words));
					h.net.send(nf);
				}
			}
			// ---- stage 2b: receive (RECEIVE / LOOP decode the network stream)
			if (netRecv) {
				static acenet::NetFrame rf;
				const int got = netPull(rf);
				if (got == 1) {
					src = netFrameConfig(rf);
					memcpy(words, rf.words, sizeof(words));
					memcpy(h.wordsClean, rf.words, sizeof(h.wordsClean));
					if (netMode == 2 && !targetMask) {
						// a receive-only instance corrupts the stream it receives
						for (int c = 0; c < ncfg; c++)
							corrupt_apply_bitstream(words, (src.numBits() + 15) / 16, cfgs[c]);
					}
					h.rxCfg = src;
					h.haveRxCfg = true;
				} else if (got == 2 && h.haveRxCfg) {
					src = h.rxCfg;          // lost / late: conceal with the stream's layout
					lost = true;
				} else {
					silent = true;          // nothing to play yet / stream gone
				}
				h.vizChannel = false;
			}
			h.netConceal = lost;

			// ---- stage 3: decode
			const hdacelp::Config cd = decoderConfigFrom(src);
			h.decCfg = cd;
			h.dec.reconfigure(cd);
			memcpy(h.wordsRx, words, sizeof(h.wordsRx));
			if (silent) {
				memset(pcm, 0, sizeof(float) * N);
			} else {
				hdacelp::unpack_words(cd, words, idx);
				if (netMode == 2 && !lost) {
					CorruptField rxFields[hdacelp::HD_MAX_FIELDS];
					hdLayoutFields(cd, rxFields);
					for (int c = 0; c < ncfg; c++)
						corrupt_apply_fields(idx, rxFields, cd.numFields(), cfgs[c], &h.cst);
				}
				concealOrRemember(idx, cd, h.bfi || lost);
				h.nTargeted = 0;
				if (targetMask) {
					CorruptField decFields[hdacelp::HD_MAX_FIELDS];
					hdLayoutFields(cd, decFields);
					Word16 before[hdacelp::HD_MAX_FIELDS];
					memcpy(before, idx, sizeof(Word16) * cd.numFields());
					for (int c = 0; c < ncfg; c++)
						corrupt_apply_bitstream_fields(idx, decFields, cd.numFields(), cfgs[c], targetMask);
					h.nTargeted = fieldBitDiff(before, idx, cd.numFields());
					if (!h.vizChannel && cd.mode == src.mode) {
						// show the targeted errors in the bitstream view
						memset(h.wordsRx, 0, sizeof(h.wordsRx));
						hdacelp::pack_words(cd, idx, h.wordsRx);
					}
				}
				h.dec.decode(idx, pcm, &decMod, &h.trace, &exc);
			}

			// Level guard: mismatched encoder/decoder settings can make the
			// decoder run far hotter than the input; allow at most +6 dB over
			// the input level (fast attack, slow release). A network stream
			// has no matching local input, so it is capped at -6 dBFS RMS.
			double ein = 1e-8, eout = 1e-12;
			for (int i = 0; i < N; i++) { ein += (double)inFrame[i] * inFrame[i]; eout += (double)pcm[i] * pcm[i]; }
			const double refE = netRecv ? 0.25 * N : 4.0 * ein;
			const float limit = (float)std::min(1.0, sqrt(refE / eout));
			const float g0 = h.guardGain;
			const float g1 = limit < g0 ? limit : g0 + (limit - g0) * 0.15f;
			for (int i = 0; i < N; i++) pcm[i] *= g0 + (g1 - g0) * (float)(i + 1) / N;
			h.guardGain = g1;

			publishViz(h.trace, src, cd, inFrame, pcm, paramCorr, bitCorr,
			           encMod.active(ce.order), decMod.active(cd.order), exc.active(),
			           20.0f * log10f(g1 > 1e-6f ? g1 : 1e-6f));

			spx_uint32_t pLen = (spx_uint32_t)N;
			spx_uint32_t oLen = (spx_uint32_t)(sizeof(hostOut) / sizeof(float));
			speex_resampler_process_float(h.up, 0, pcm, &pLen, hostOut, &oLen);
			for (spx_uint32_t i = 0; i < oLen; i++)
				hostOut[i] = hostOut[i] > 1.0f ? 1.0f : (hostOut[i] < -1.0f ? -1.0f : hostOut[i]);
			cat_ringbuffer_write(pcm_output_buffer, (char *)hostOut, oLen * sizeof(float));
		}
	}
	return did;
}

#pragma endregion

void CheetahDSP::sampleRateChanged(double newSampleRate) {
  sampleRate = newSampleRate;
}

void CheetahDSP::mute() {
}

//   if (freq < 0) {
//     freq = 0;
//   } else if (freq > sampleRate / 2.0) {
//     freq = sampleRate / 2.0;
//   }
//   input_hpf_0.setHPF_BW(freq, sampleRate);
//   input_hpf_1.setHPF_BW(freq, sampleRate);
// }
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

#pragma region "mein kram"

Word16 encoder_last_ener_pit;
Word16 encoder_last_ener_cod;
Word16 sdec_last_ener_pit;
Word16 sdec_last_ener_cod;
Word16 encoder_old_speech[(240+40+10)];
Word16 *encoder_speech, *encoder_p_window;
Word16 *encoder_new_speech;                    /* Global variable */
#define L_frame 240
#define serial_size 138
#define ana_size 23
#define prm_size 24

Word16 encoder_FS_Flag = 0; /* Frame Stealing Flag :
				 0 = no stealing in the time-slot,
				!0 = stealing of the first speech frame
						in the time-slot
				This flag is set/reset by external world */
Word32 encoder_Loop_counter = 0;
short encoder_first_pass = true;
Word16 encoder_i;
Word16 encoder_Vocod_array[274]; /* Input Buffer : 2 vocoder frames */
Word16 encoder_Coded_array[432];
Word16 encoder_Interleaved_coded_array[432]; /* Output Buffer */

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

Word16 decoder_Reordered_array[286];		 /* 2 frames vocoder + 8 + 4 */
Word16 decoder_Interleaved_coded_array[432]; /*time-slot length at 7.2 kb/s*/
Word16 decoder_Coded_array[432];
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
        DEBUG_PRINTF("%04X%s", (uint16_t)data[i], (i < len - 1) ? "" : "\n");
        if ((i + 1) % 32 == 0) DEBUG_PRINTF("\n"); // optional: newline every 8 entries
    }
}
//


#define ACELP_DUAL_CHAN_FRAME_SIZE 432 // 60ms @ 8kHz
#define ACELP_DUAL_CHAN_AUDIO_SIZE 480 // 8kHz // L_frame*2

#define ACELP_FRAME_SIZE 286 // 60ms @ 8kHz
#define ACELP_VOCODER_SAMPLE_COUNT 286
#define UPSAMPLE_RATIO 6
#define DOWNSAMPLE_RATIO 6
#define UPSAMPLED_FRAME_SIZE (ACELP_FRAME_SIZE * UPSAMPLE_RATIO)
Word16 samples[ACELP_DUAL_CHAN_AUDIO_SIZE];

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
void encode_acelp(float *input_8khz, Word16 *out) {
	Word16 syn[L_frame];		/* Local synthesis.       */
	Word16 ana[ana_size];		/* Analysis parameters.   */
	// static Word16 serial[serial_size]; /* Serial stream.         */


	convert_buffer_float_to_int16(input_8khz, samples, (size_t)ACELP_DUAL_CHAN_AUDIO_SIZE);
	Word16 firstHalf[L_frame];
	Word16 secondHalf[L_frame];
	// static Word16 out2[ACELP_DUAL_CHAN_AUDIO_SIZE];
	Word16 vocoder_ChannelFrame[432];

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
	Word16 vocoderArray[274];
	Word16 vocoderSerial[138];

	{ // 1st 137 bits
		// print_word16_hex(firstHalf, 240);
		memcpy(encoder_new_speech, firstHalf, L_frame*sizeof(Word16));
		Pre_Process(firstHalf, (Word16)L_frame); /* Pre processing of input speech */
		// print_word16_hex(firstHalf, L_frame);
		encoder_Coder_Tetra(ana, syn);					  /* Find speech parameters         */
		// print_word16_hex(ana, 23);
		// print_word16_hex(syn, 240);
		Post_Process(syn, (Word16)L_frame);		  /* Post processing of synthesis   */
		Prm2bits_Tetra(ana, vocoderSerial);			  /* Parameters to serial bits      */
		// fwrite(syn,    sizeof(Word16), L_frame    ,  f_syn);
		//fwrite(serial, sizeof(Word16), serial_size, f_serial);
		// print_word16_hex(vocoderSerial, 138);
		// TODO: copy serial to Vocod_array
		for (encoder_i = 0; encoder_i < 137; encoder_i++)
			vocoderArray[encoder_i] = vocoderSerial[encoder_i+1];
	}
	{ // second 137 bits
		Pre_Process(secondHalf, (Word16)L_frame); /* Pre processing of input speech */
		// print_word16_hex(secondHalf, L_frame);
		memcpy(encoder_new_speech, secondHalf, L_frame*sizeof(Word16));
		encoder_Coder_Tetra(ana, syn);					  /* Find speech parameters         */
		// print_word16_hex(ana, 23);
		// print_word16_hex(syn, 240);
		Post_Process(syn, (Word16)L_frame);		  /* Post processing of synthesis   */
		Prm2bits_Tetra(ana, vocoderSerial);			  /* Parameters to serial bits      */
		// fwrite(syn,    sizeof(Word16), L_frame    ,  f_syn);
		//fwrite(serial, sizeof(Word16), serial_size, f_serial);
		// print_word16_hex(vocoderSerial, 138);
		// TODO: copy serial to Vocod_array
		for (encoder_i = 0; encoder_i < 137; encoder_i++)
			vocoderArray[encoder_i+137] = vocoderSerial[encoder_i+1];
	}
	// print_word16_hex(vocoderArray, 274);
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
			for (int i = 0; i < 137; i++) {
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
			for (int i = 137; i < 274; i++) {
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

CheetahDSP::CheetahDSP(double sampleRate): stopThread(false), threadRunning(false), callback(nullptr) {
  for (uint32_t param = 0; param < paramCount; param++) {
    newParams[param] = banks[DEFAULT_BANK].presets[DEFAULT_PRESET].params[param];
    oldParams[param] = -1.0;
  }
    finalized = false;
	encoded_frame_queue = NULL;
	ring2 = NULL;
	ring_48k_incoming = NULL;
	pcm_output_buffer = NULL;

    stopThread.store(false, std::memory_order_release);
    DEBUG_PRINTF("activate 3\n");
    if (!threadRunning.load(std::memory_order_acquire)) {
        DEBUG_PRINTF("starting background thread");
        backgroundThread = std::thread(&CheetahDSP::threadFunction, this);
    }

    sampleRateChanged(sampleRate);
	if (!finalized) {
		finalized = true;
		DEBUG_PRINTF("activate\n");
        DEBUG_PRINTF("initialized initialACELPSetup 1\n");
        encoded_frame_queue = cat_ringbuffer_create(ACELP_DUAL_CHAN_FRAME_SIZE * 4);
        ring_48k_incoming = cat_ringbuffer_create((48000 * sizeof(float))); // 1s 48khz
        ring2 = cat_ringbuffer_create(RINGBUFFER_SIZE);
        pcm_output_buffer = cat_ringbuffer_create(sizeof(float) * ACELP_DUAL_CHAN_AUDIO_SIZE * UPSAMPLE_RATIO);//(8*1024);
        /* Initialization of decoder  */
        sdec_Init_Decod_Tetra();
        /* Initialization of the coder */
        Init_Pre_Process();
        encoder_Init_Coder_Tetra();
        /* Loop for each "L_frame" speech data. */
        encoder_frame = 0;
        decoder_frame = 0;
        //
        resampler_down = speex_resampler_init(1, 48000, 8000, 5, &speex_err);
        resampler_up = speex_resampler_init(1, 8000, 48000, 5, &speex_err);

        DEBUG_PRINTF("initialized initialACELPSetup 2\n");
		
		DEBUG_PRINTF("activate 2\n");
		DEBUG_PRINTF("activated\n");
	}
}

CheetahDSP::~CheetahDSP() {
    // delete smooth_gain;
	stopThread.store(true, std::memory_order_release);
	if (backgroundThread.joinable())
		backgroundThread.join();
	if (finalized) {
		finalized = false;
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

void CheetahDSP::corrupt_audio(Word16 *data) {
	float pVolume = newParams[paramVolume];
	float pCorrMode = newParams[paramCorruptionMode];
	float pCorrInt = newParams[paramCorruptionIntensity];
	float pCorrMag = newParams[paramCorruptionMagnitude];
	float pCodecType = newParams[paramCodecType];
	{ // Intermediary Layer
		if (round(pCodecType == 1)) {
			if (round(pCorrMode) == 1) {
				corrupt_by_wrong_interleave(data, 432);
			}
			if (round(pCorrMode) == 2) {
				corrupt_by_overflow(data, 432);
			}
			if (round(pCorrMode) == 3) {
				bit_desync_shift_left_Word16(encoder_Interleaved_coded_array, 432);
				// int r = rand() % 100;
				// if (r < midiEnergyMap[70]) {
				// 	// allowFrameWrite = 0;
				// }
			} else {
				// allowFrameWrite = allowFrameWrite || 1;
			}
			if (round(pCorrMode) == 4) {
				random_bit_desync_Word16(data, 432, pCorrInt, pCorrMag);
			}
			if (round(pCorrMode) == 5) {
				corrupt_bit_flips_Word16(data, 432, pCorrInt);
				// random_bit_desync_Word16(encoder_Interleaved_coded_array, 432, midiEnergyMap[70], midiEnergyMap[65]);
			}
			if (round(pCorrMode) == 6) {
				encoder_first_pass = true;
				decoder_first_pass = true;
			}
		}
	}
}
void CheetahDSP::process_buff_ring1_audio(size_t nframes) {
	const int samples48kSize = ACELP_DUAL_CHAN_AUDIO_SIZE * 6;

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
		encode_acelp(down_out, channelFrameOutput);
		// print_word16_hex(channelFrameOutput, 432);
		corrupt_audio(channelFrameOutput);
		if (callback != nullptr) {
			callback->onVocoderFrame(channelFrameOutput);
		}
		
        // print_word16_hex(channelFrameOutput, ACELP_DUAL_CHAN_FRAME_SIZE);
		// Write downsampled to ring1
		size_t space_needed = sizeof(Word16)*ACELP_DUAL_CHAN_FRAME_SIZE;
		if (cat_ringbuffer_write_space(encoded_frame_queue) >= space_needed) {
			cat_ringbuffer_write(encoded_frame_queue, (char *)channelFrameOutput, space_needed);
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
	cat_ringbuffer_write(ring_48k_incoming, (char *)in, frames * sizeof(float));
	process_buff_ring1_audio(frames);
	// float monoOutput[frames];
    // Output: Read from ringbuffer, or fill with silence if underrun
    size_t to_read = sizeof(float) * frames;
    if (cat_ringbuffer_read_space(pcm_output_buffer) >= to_read) {
        cat_ringbuffer_read(pcm_output_buffer, (char *)out, to_read);
		// DEBUG_PRINTF("output ringbuffer serving %lu samples\n", frames);
    } else {
        memset(out, 0, to_read); // underrun fallback
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

void CheetahDSP::setCallback(Callback* newCallback) noexcept {
    callback = newCallback;
}
void CheetahDSP::threadFunction() {
	DEBUG_PRINTF("entering background thread\n");
	threadRunning.store(true, std::memory_order_release);

	Word16 playback_frame[ACELP_DUAL_CHAN_FRAME_SIZE];         // Temp buffer for each new frame
	// Word16 playback_frame[ACELP_DUAL_CHAN_AUDIO_SIZE];         // TEST NEW!!
    Word16 last_good_frame[ACELP_DUAL_CHAN_AUDIO_SIZE] = { 0 }; // Backup of last valid frame
	hex_to_word16_array(fuckFrame, last_good_frame, ACELP_DUAL_CHAN_FRAME_SIZE);
	//
    float decode_buf[ACELP_DUAL_CHAN_AUDIO_SIZE];
    float upsample_buf[ACELP_DUAL_CHAN_AUDIO_SIZE * UPSAMPLE_RATIO];
	//
	int has_valid_frame = 1;
	//
    while (!stopThread.load(std::memory_order_acquire)) {
        // Try to read a new encoded frame from the queue
        if (cat_ringbuffer_read_space(encoded_frame_queue) >= sizeof(playback_frame)) {
            // Read it
            //size_t read_bytes = 
			cat_ringbuffer_read(encoded_frame_queue, (char *)playback_frame, sizeof(playback_frame));
            // DEBUG_PRINTF("RINGBUFFER ENCODEDFRAME QUEUE readbytes=%lu, size %d\n", read_bytes, sizeof(encoded_frame));
			memcpy(&last_good_frame, &playback_frame, sizeof(playback_frame));
			// print_word16_hex(encoded_frame, sizeof(encoded_frame));
			// DEBUG_PRINTF("new nice frame came around");
            has_valid_frame = 1;
        } else if (has_valid_frame) {
            // No new frame: reuse the last good one
			// DEBUG_PRINTF("reusing old frame");
            memcpy(&playback_frame, &last_good_frame, sizeof(playback_frame));
        } else {
            // No data available yet: silence output or skip
            // usleep(1000);
        	std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }	

        // Decode and upsample
		// DEBUG_PRINTF("received frame:\n");
		// print_word16_hex(playback_frame, 432);
        decode_acelp(playback_frame, decode_buf);
        // upsample_6x(decode_buf, upsample_buf, ACELP_DUAL_CHAN_AUDIO_SIZE * UPSAMPLE_RATIO);
		// --- Upsample 6x ---
		spx_uint32_t in_len = ACELP_DUAL_CHAN_AUDIO_SIZE;
		spx_uint32_t out_len = ACELP_DUAL_CHAN_AUDIO_SIZE * UPSAMPLE_RATIO;
		// memcpy(down_in, in, sizeof(float) * in_len);
		speex_resampler_process_float(resampler_up, 0, decode_buf, &in_len, upsample_buf, &out_len);

        // Wait until there's enough space to write
        size_t bytes_needed = sizeof(float) * ACELP_DUAL_CHAN_AUDIO_SIZE * UPSAMPLE_RATIO;
        while (!stopThread.load(std::memory_order_acquire) && cat_ringbuffer_write_space(pcm_output_buffer) < bytes_needed) {
        	std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // Write the samples to output buffer
        // size_t wrote_bytes = 
		cat_ringbuffer_write(pcm_output_buffer, (char *)upsample_buf, bytes_needed);
		// DEBUG_PRINTF("decoding to %d 48khz samples=%d bytes, wrote %d bytes\n", ACELP_DUAL_CHAN_AUDIO_SIZE * UPSAMPLE_RATIO, bytes_needed, wrote_bytes);
	}
	//
	threadRunning.store(false, std::memory_order_release);
	DEBUG_PRINTF("bg: leaving thread\n");
	return;
}

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
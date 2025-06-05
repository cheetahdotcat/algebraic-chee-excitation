/************************************************************************
*
*	sdec_FILENAME		:	sdec_sdec_tet.sdec_c
*
*	sdec_DESCRIPTION		:	sdec_Main sdec_routines for sdec_speech sdec_source sdec_decoding
*
************************************************************************
*
*	sub-sdec_ROUTINES	:	- sdec_Init_Decod_Tetra()
*					- sdec_Decod_Tetra()
*
************************************************************************
*
*	sdec_INCLUDED sdec_FILES	:	sdec_source.sdec_h
*
************************************************************************/

#include "source.h"
#include "ener.h"

/*--------------------------------------------------------*
 *       sdec_Decoder sdec_constants sdec_parameters.                    *
 *                                                        *
 *   sdec_L_frame     : sdec_Frame sdec_size.                            *
 *   sdec_L_subfr     : sub-sdec_frame sdec_size.                        *
 *   sdec_p           : sdec_LPC sdec_order.                             *
 *   sdec_pp1         : sdec_LPC sdec_order+1                            *
 *   sdec_pit_min     : sdec_Minimum sdec_pitch sdec_lag.                     *
 *   sdec_pit_max     : sdec_Maximum sdec_pitch sdec_lag.                     *
 *   sdec_L_inter     : sdec_Length sdec_of sdec_filter for sdec_interpolation     *
 *   sdec_parm_size   : sdec_Lenght sdec_of sdec_vector sdec_parm[]                *
 *--------------------------------------------------------*/

#define  sdec_L_frame  (Word16)240
#define  sdec_L_subfr  (Word16)60
#define  sdec_p        (Word16)10
#define  sdec_pp1      (Word16)11
#define  sdec_pit_min  (Word16)20
#define  sdec_pit_max  (Word16)143
#define  sdec_L_inter  (Word16)15
#define  sdec_parm_size (Word16)23


/*--------------------------------------------------------*
 *   sdec_LPC sdec_bandwidth sdec_expansion sdec_factors for sdec_noise sdec_filter.    *
 *      sdec_In sdec_Q15 = 0.75, 0.85                               *
 *--------------------------------------------------------*/

#define sdec_gamma3  (Word16)24576
#define sdec_gamma4  (Word16)27853


/*--------------------------------------------------------*
 *         static sdec_memory sdec_allocation.                      *
 *--------------------------------------------------------*/

        /* sdec_Excitation sdec_vector */

static Word16 sdec_old_exc[sdec_L_frame+sdec_pit_max+sdec_L_inter];
static Word16 *sdec_exc;

        /* sdec_Spectral sdec_expansion sdec_factors */

static Word16 sdec_F_gamma3[sdec_p];
static Word16 sdec_F_gamma4[sdec_p];

        /* sdec_Lsp (sdec_Line sdec_spectral sdec_pairs sdec_in sdec_the sdec_cosine sdec_domain) */

static Word16 sdec_lspold[sdec_p]={
              30000, 26000, 21000, 15000, 8000, 0,
		  -8000,-15000,-21000,
			-26000};
static Word16 sdec_lspnew[sdec_p];

	  /* sdec_Initial sdec_lsp sdec_values sdec_used sdec_after sdec_each sdec_time */
        /* sdec_a sdec_reset sdec_is sdec_executed */

static Word16 sdec_lspold_init[sdec_p]={
              30000, 26000, 21000, 15000, 8000, 0,
		  -8000,-15000,-21000,-26000};

        /* sdec_Filter'sdec_s sdec_memory */

static Word16 sdec_mem_syn[sdec_p];

        /* sdec_Default sdec_parameters */

static Word16 sdec_old_parm[sdec_parm_size], sdec_old_T0;

       /* sdec_Global sdec_definition */


/**************************************************************************
*
*	sdec_ROUTINE				:	sdec_Init_Decod_Tetra
*
*	sdec_DESCRIPTION			:	sdec_Initialization sdec_of sdec_variables for sdec_the sdec_speech sdec_decoder
*
**************************************************************************
*
*	sdec_USAGE				:	sdec_Init_Decod_Tetra()
*
*	sdec_INPUT sdec_ARGUMENT(sdec_S)		:	sdec_None
*
*	sdec_OUTPUT sdec_ARGUMENT(sdec_S)		:	sdec_None
*
*	returnED sdec_VALUE		:	sdec_None
*
**************************************************************************/

void sdec_Init_Decod_Tetra(void)
{
  Word16 sdec_i;

  sdec_old_T0 = 60;
  for(sdec_i=0; sdec_i<23; sdec_i++)
     sdec_old_parm[sdec_i] = 0;

  /* sdec_Initialize static sdec_pointer */

  sdec_exc    = sdec_old_exc + sdec_pit_max + sdec_L_inter;

  /* sdec_Initialize sdec_global sdec_variables */

  sdec_last_ener_cod = 0;
  sdec_last_ener_pit = 0;
  
  /* static sdec_vectors sdec_to sdec_zero */

  for(sdec_i=0; sdec_i<sdec_pit_max + sdec_L_inter; sdec_i++)
    sdec_old_exc[sdec_i] = 0;

  for(sdec_i=0; sdec_i<sdec_p; sdec_i++)
    sdec_mem_syn[sdec_i] = 0;


  /* sdec_Initialisation sdec_of sdec_lsp sdec_values for sdec_first */
  /* sdec_frame sdec_lsp sdec_interpolation */

  for(sdec_i=0; sdec_i<sdec_p; sdec_i++)
    sdec_lspold[sdec_i] = sdec_lspold_init[sdec_i];


  /* sdec_Compute sdec_LPC sdec_spectral sdec_expansion sdec_factors */

  Fac_Pond(sdec_gamma3, sdec_F_gamma3);
  Fac_Pond(sdec_gamma4, sdec_F_gamma4);

 return;
}


/**************************************************************************
*
*	sdec_ROUTINE				:	sdec_Decod_Tetra
*
*	sdec_DESCRIPTION			:	sdec_Main sdec_speech sdec_decoder sdec_function
*
**************************************************************************
*
*	sdec_USAGE				:	sdec_Decod_Tetra(sdec_parm,sdec_synth)
*							(sdec_Routine_Name(sdec_input1,sdec_output1))
*
*	sdec_INPUT sdec_ARGUMENT(sdec_S)		:	
*
*		sdec_INPUT1			:	- sdec_Description : sdec_Synthesis sdec_parameters
*							- format : 24 * 16 sdec_bit-sdec_samples
*
*	sdec_OUTPUT sdec_ARGUMENT(sdec_S)		:	
*
*		sdec_OUTPUT1			:	- sdec_Description : sdec_Synthesis
*							- format : 240 * 16 sdec_bit-sdec_samples
*
*	returnED sdec_VALUE		:	sdec_None
*
**************************************************************************/

void sdec_Decod_Tetra(Word16 sdec_parm[], Word16 sdec_synth[])
{
  /* sdec_LPC sdec_coefficients */

  Word16 sdec_A_t[(sdec_pp1)*4];		/* sdec_A(sdec_z) sdec_unquantized for sdec_the 4 subframes */
  Word16 sdec_Ap3[sdec_pp1];		/* sdec_A(sdec_z) sdec_with sdec_spectral sdec_expansion         */
  Word16 sdec_Ap4[sdec_pp1];		/* sdec_A(sdec_z) sdec_with sdec_spectral sdec_expansion         */
  Word16 *sdec_A;			/* sdec_Pointer sdec_on sdec_A_t                       */

  /* sdec_Other sdec_vectors */

  Word16 sdec_zero_F[sdec_L_subfr+64],  *sdec_F;
  Word16 sdec_code[sdec_L_subfr+4];

  /* sdec_Scalars */

  Word16 sdec_i, sdec_i_subfr;
  Word16 sdec_T0, sdec_T0_min, sdec_T0_max, sdec_T0_frac;
  Word16 sdec_gain_pit, sdec_gain_code, sdec_index;
  Word16 sdec_sign_code, sdec_shift_code;
  Word16 sdec_bfi, sdec_temp;
  Word32 sdec_L_temp;

  /* sdec_Initialization sdec_of sdec_F */

  sdec_F  = &sdec_zero_F[64];
  for(sdec_i=0; sdec_i<64; sdec_i++)
   sdec_zero_F[sdec_i] = 0;

  /* sdec_Test sdec_bfi */

  sdec_bfi = *sdec_parm++;

  if(sdec_bfi == 0)
  {
    D_Lsp334(&sdec_parm[0], sdec_lspnew, sdec_lspold);	/* sdec_lsp sdec_decoding   */

    for(sdec_i=0; sdec_i< sdec_parm_size; sdec_i++)		/* sdec_keep sdec_parm[] sdec_as sdec_old_parm */
      sdec_old_parm[sdec_i] = sdec_parm[sdec_i];
  }
  else
  {
    for(sdec_i=1; sdec_i<sdec_p; sdec_i++)
      sdec_lspnew[sdec_i] = sdec_lspold[sdec_i];

    for(sdec_i=0; sdec_i< sdec_parm_size; sdec_i++)		/* sdec_use sdec_old sdec_parm[] */
      sdec_parm[sdec_i] = sdec_old_parm[sdec_i];
  }

  sdec_parm += 3;			/* sdec_Advance sdec_synthesis sdec_parameters sdec_pointer */

  /* sdec_Interpolation sdec_of sdec_LPC for sdec_the 4 subframes */

  Int_Lpc4(sdec_lspold,   sdec_lspnew,   sdec_A_t);

  /* sdec_update sdec_the sdec_LSPs for sdec_the sdec_next sdec_frame */

  for(sdec_i=0; sdec_i<sdec_p; sdec_i++)
    sdec_lspold[sdec_i]   = sdec_lspnew[sdec_i];

/*------------------------------------------------------------------------*
 *          sdec_Loop for sdec_every subframe sdec_in sdec_the sdec_analysis sdec_frame                 *
 *------------------------------------------------------------------------*
 * sdec_The subframe sdec_size sdec_is sdec_L_subfr sdec_and sdec_the sdec_loop sdec_is sdec_repeated sdec_L_frame/sdec_L_subfr  *
 *  sdec_times                                                                 *
 *     - sdec_decode sdec_the sdec_pitch sdec_delay                                           *
 *     - sdec_decode sdec_algebraic sdec_code                                            *
 *     - sdec_decode sdec_pitch sdec_and sdec_codebook sdec_gains                                  *
 *     - sdec_find sdec_the sdec_excitation sdec_and sdec_compute sdec_synthesis sdec_speech                 *
 *------------------------------------------------------------------------*/

  sdec_A = sdec_A_t;				/* sdec_pointer sdec_to sdec_interpolated sdec_LPC sdec_parameters */

  for (sdec_i_subfr = 0; sdec_i_subfr < sdec_L_frame; sdec_i_subfr += sdec_L_subfr)
  {

    sdec_index = *sdec_parm++;				/* sdec_pitch sdec_index */

    if (sdec_i_subfr == 0)				/* if sdec_first subframe */
    {
      if (sdec_bfi == 0)
      {						/* if sdec_bfi == 0 sdec_decode sdec_pitch */
         if (sdec_index < 197)
         {
           /* sdec_T0 = (sdec_index+2)/3 + 19; sdec_T0_frac = sdec_index - sdec_T0*3 + 58; */

           sdec_i = add(sdec_index, (Word16)2);
           sdec_i = mult(sdec_i, (Word16)10923);	/* 10923 = 1/3 sdec_in sdec_Q15 */
           sdec_T0 = add(sdec_i, (Word16)19);

           sdec_i = add(sdec_T0, add(sdec_T0, sdec_T0) );	/* sdec_T0*3 */
           sdec_i = sub((Word16)58, (Word16)sdec_i);
           sdec_T0_frac = add(sdec_index, (Word16)sdec_i);
         }
         else
         {
           sdec_T0 = sub(sdec_index, (Word16)112);
           sdec_T0_frac = 0;
         }
      }
      else   /* sdec_bfi == 1 */
      {
        sdec_T0 = sdec_old_T0;
        sdec_T0_frac = 0;
      }


      /* sdec_find sdec_T0_min sdec_and sdec_T0_max for sdec_other subframes */

      sdec_T0_min = sub(sdec_T0, (Word16)5);
      if (sdec_T0_min < sdec_pit_min) sdec_T0_min = sdec_pit_min;
      sdec_T0_max = add(sdec_T0_min, (Word16)9);
      if (sdec_T0_max > sdec_pit_max)
      {
        sdec_T0_max = sdec_pit_max;
        sdec_T0_min = sub(sdec_T0_max, (Word16)9);
      }
    }

    else  /* sdec_other subframes */

    {
      if (sdec_bfi == 0)				/* if sdec_bfi == 0 sdec_decode sdec_pitch */
      {
         /* sdec_T0 = (sdec_index+2)/3 - 1 + sdec_T0_min; */

         sdec_i = add(sdec_index, (Word16)2);
         sdec_i = mult(sdec_i, (Word16)10923);	/* 10923 = 1/3 sdec_in sdec_Q15 */
         sdec_i = sub(sdec_i, (Word16)1);
         sdec_T0 = add(sdec_T0_min, sdec_i);

         /* sdec_T0_frac = sdec_index - 2 - sdec_i*3; */

         sdec_i = add(sdec_i, add(sdec_i,sdec_i) );		/* sdec_i*3 */
         sdec_T0_frac = sub( sdec_index , add(sdec_i, (Word16)2) );
      }
    }

   /*-------------------------------------------------*
    * - sdec_Find sdec_the sdec_adaptive sdec_codebook sdec_vector.            *
    *-------------------------------------------------*/

    Pred_Lt(&sdec_exc[sdec_i_subfr], sdec_T0, sdec_T0_frac, sdec_L_subfr);

   /*-----------------------------------------------------*
    * - sdec_Compute sdec_noise sdec_filter sdec_F[].                         *
    * - sdec_Decode sdec_codebook sdec_sign sdec_and sdec_index.                   *
    * - sdec_Find sdec_the sdec_algebraic sdec_codeword.                      *
    *-----------------------------------------------------*/

    Pond_Ai(sdec_A, sdec_F_gamma3, sdec_Ap3);
    Pond_Ai(sdec_A, sdec_F_gamma4, sdec_Ap4);

    for (sdec_i = 0;   sdec_i <= sdec_p;      sdec_i++) sdec_F[sdec_i] = sdec_Ap3[sdec_i];
    for (sdec_i = sdec_pp1; sdec_i < sdec_L_subfr; sdec_i++) sdec_F[sdec_i] = 0;

    Syn_Filt(sdec_Ap4, sdec_F, sdec_F, sdec_L_subfr, &sdec_F[sdec_pp1], (Word16)0);

    /* sdec_Introduce sdec_pitch sdec_contribution sdec_with sdec_fixed sdec_gain sdec_of 0.8 sdec_to sdec_F[] */

    for (sdec_i = sdec_T0; sdec_i < sdec_L_subfr; sdec_i++)
    {
      sdec_temp = mult(sdec_F[sdec_i-sdec_T0], (Word16)26216);
      sdec_F[sdec_i] = add(sdec_F[sdec_i], sdec_temp);
    }

    sdec_index = *sdec_parm++;
    sdec_sign_code  = *sdec_parm++;
    sdec_shift_code = *sdec_parm++;

    D_D4i60(sdec_index, sdec_sign_code, sdec_shift_code, sdec_F, sdec_code);


   /*-------------------------------------------------*
    * - sdec_Decode sdec_pitch sdec_and sdec_codebook sdec_gains.              *
    *-------------------------------------------------*/

    sdec_index = *sdec_parm++;        /* sdec_index sdec_of sdec_energy sdec_VQ */

    Dec_Ener(sdec_index,sdec_bfi,sdec_A,&sdec_exc[sdec_i_subfr],sdec_code, sdec_L_subfr, &sdec_gain_pit, &sdec_gain_code);

   /*-------------------------------------------------------*
    * - sdec_Find sdec_the sdec_total sdec_excitation.                          *
    * - sdec_Find sdec_synthesis sdec_speech sdec_corresponding sdec_to sdec_exc[].       *
    *-------------------------------------------------------*/

    for (sdec_i = 0; sdec_i < sdec_L_subfr;  sdec_i++)
    {
      /* sdec_exc[sdec_i] = sdec_gain_pit*sdec_exc[sdec_i] + sdec_gain_code*sdec_code[sdec_i]; */
      /* sdec_exc[sdec_i]  sdec_in sdec_Q0   sdec_gain_pit sdec_in sdec_Q12               */
      /* sdec_code[sdec_i] sdec_in sdec_Q12  sdec_gain_cod sdec_in sdec_Q0                */

      sdec_L_temp = L_mult0(sdec_exc[sdec_i+sdec_i_subfr], sdec_gain_pit);
      sdec_L_temp = L_mac0(sdec_L_temp, sdec_code[sdec_i], sdec_gain_code);
      sdec_exc[sdec_i+sdec_i_subfr] = L_shr_r(sdec_L_temp, (Word16)12);
    }

    Syn_Filt(sdec_A, &sdec_exc[sdec_i_subfr], &sdec_synth[sdec_i_subfr], sdec_L_subfr, sdec_mem_syn, (Word16)1);

    sdec_A  += sdec_pp1;    /* sdec_interpolated sdec_LPC sdec_parameters for sdec_next subframe */
  }

 /*--------------------------------------------------*
  * sdec_Update sdec_signal for sdec_next sdec_frame.                    *
  * -> sdec_shift sdec_to sdec_the sdec_left sdec_by sdec_L_frame  sdec_exc[]           *
  *--------------------------------------------------*/

  for(sdec_i=0; sdec_i<sdec_pit_max+sdec_L_inter; sdec_i++)
    sdec_old_exc[sdec_i] = sdec_old_exc[sdec_i+sdec_L_frame];

  sdec_old_T0 = sdec_T0;

  return;
}


/************************************************************************
*
*	FILENAME		:	sdecoder.c
*
*	DESCRIPTION		:	Main program for speech source decoding
*
************************************************************************
*
*	USAGE			:	sdecoder  serial_file  synth_file
*					(executable_file input1 output1)
*
*	INPUT FILE(S)		:	
*
*		INPUT1	:	- Description : serial stream input file
*					- Format : binary file 16 bit-samples
*					  138 (= 1 + 137) samples per frame
*
*	OUTPUT FILE(S)	:	
*
*		OUTPUT1	:	- Description : synthesis output file 
*					- Format : binary file 16 bit-samples
*					  240 bits per frame
*
*	COMMENTS		:	First sample of each INPUT1 frame is the bad frame
*					indicator (BFI), BFI = 0 corresponds to a valid frame,
*					BFI = 1 to corrupted data
*
************************************************************************
*
*	INCLUDED FILES	:	source.h
*					stdio.h
*					stdlib.h
*
************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include "source.h"
#include "ener.h"
Word16 encoder_last_ener_pit;
Word16 encoder_last_ener_cod;
Word16 sdec_last_ener_pit;
Word16 sdec_last_ener_cod;
/*-----------------*
 * Constants       *
 *-----------------*/

#define L_frame     240
#define serial_size 138
#define prm_size    24

int main(int argc, char *argv[] )
{
  Word16 frame;
  Word16 synth[L_frame];		/* Synthesis              */
  Word16 parm[prm_size];		/* Synthesis parameters   */
  Word16 serial[serial_size];		/* Serial stream          */
  FILE   *f_syn, *f_serial;


  /* Passed arguments */

  if ( argc != 3 )
  {
     fprintf(stderr,"Usage : sdecoder  serial_file  synth_file\n");
     fprintf(stderr,"\n");
     fprintf(stderr,"Format for serial_file:\n");
     fprintf(stderr,"  Serial stream input is read from a binary file\n");
     fprintf(stderr,"  where each 16-bit word represents 1 encoded bit.\n");
     fprintf(stderr,"  BFI + 137 bits by frame\n");
     fprintf(stderr,"\n");
     fprintf(stderr,"Format for synth_file:\n");
     fprintf(stderr,"  Synthesis is written to a binary file of 16 bits data.\n");
     exit( 1 );
  }

  /* Open file for synthesis and serial stream */

  if( (f_serial = fopen(argv[1],"rb") ) == NULL )
  {
    fprintf(stderr,"Input file '%s' does not exist !!\n",argv[1]);
    exit(0);
  }

  if( (f_syn = fopen(argv[2], "wb") ) == NULL )
  {
    fprintf(stderr,"Cannot open file '%s' !!\n", argv[2]);
    exit(0);
  }


  /* Initialization of decoder  */

  sdec_Init_Decod_Tetra();

  /* Loop for each "L_frame" speech data. */

  frame =0;

  while( fread(serial, sizeof(Word16), serial_size, f_serial) == serial_size)
  {
    fprintf(stderr,"frame=%d\n", ++frame);

    Bits2prm_Tetra(serial, parm);	/* serial to parameters */

    sdec_Decod_Tetra(parm, synth);		/* decoder */

    Post_Process(synth, (Word16)L_frame);	/* Post processing of synthesis  */

    fwrite(synth, sizeof(Word16), L_frame,  f_syn);   /* Write output file */
  }
  return (EXIT_SUCCESS);
}


#ifndef _BITSTREAM_H_
#define _BITSTREAM_H_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdint.h>

#define BSWAP(x) x=((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) | (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))

#define EDGE_SIZE  32

typedef struct
{
	uint8_t * y;
	uint8_t * u;
	uint8_t * v;
} IMAGE;


/* Inter-coded macroblock, 1 motion vector */
#define MODE_INTER		0
/* Inter-coded macroblock + dquant */
#define MODE_INTER_Q	1
/* Inter-coded macroblock, 4 motion vectors */
#define MODE_INTER4V	2
/* Intra-coded macroblock */
#define	MODE_INTRA		3
/* Intra-coded macroblock + dquant */
#define MODE_INTRA_Q	4


typedef struct
{
	int32_t x;
	int32_t y;
} VECTOR;


#define MBPRED_SIZE  15


typedef struct
{
	VECTOR mvs[4];

    int16_t pred_values[6][MBPRED_SIZE];
    uint8_t acpred_directions[6];
    
	uint32_t mode;
	uint32_t quant;		// absolute quant

	// uint32_t cbp;
} MACROBLOCK;



typedef struct
{
	// bitstream

	uint32_t time_inc_bits;
	uint32_t quant_bits;
	uint32_t quant_type;

	// image

	uint32_t width;
	uint32_t height;
	uint32_t edged_width;
	uint32_t edged_height;
	
	IMAGE cur;
	IMAGE refn;
	IMAGE refh;
	IMAGE refv;
	IMAGE refhv;

	// macroblock

	uint32_t mb_width;
	uint32_t mb_height;
	MACROBLOCK * mbs;

	
} DECODER;

// vop coding types 
// intra, prediction, backward, sprite, not_coded
#define I_VOP	0
#define P_VOP	1
#define B_VOP	2
#define S_VOP	3
#define N_VOP	4

typedef struct
{
	uint32_t bufa;
	uint32_t bufb;
	uint32_t pos;
	uint32_t * head;
} BITSTREAM;



// header stuff
void bs_init_tc(BITSTREAM * bs, char *bitstream);
int bs_vol(BITSTREAM * bs, DECODER * dec);
int bs_vop(BITSTREAM * bs, DECODER * dec, uint32_t * rounding, uint32_t * quant, uint32_t * fcode);


static uint32_t __inline bs_show(BITSTREAM * const bs, const uint32_t bits)
{
	int nbit = (bits + bs->pos) - 32;
	if (nbit > 0) 
	{
		return ((bs->bufa & (0xffffffff >> bs->pos)) << nbit) |
				(bs->bufb >> (32 - nbit));
	}
	else 
	{
		return (bs->bufa & (0xffffffff >> bs->pos)) >> (32 - bs->pos - bits);
	}
}



static __inline void bs_skip(BITSTREAM * const bs, const uint32_t bits)
{
	bs->pos += bits;

	if (bs->pos >= 32) 
	{
		uint32_t tmp;

		bs->bufa = bs->bufb;
		tmp = *(uint32_t *)bs->head;
#ifndef BIG_ENDIAN
		BSWAP(tmp);
#endif
		bs->bufb = tmp;
		bs->head ++;
		bs->pos -= 32;
	}
}



static __inline void bs_bytealign(BITSTREAM * const bs)
{
	uint32_t remainder = bs->pos % 8;
	if (remainder)
	{
		bs_skip(bs, 8 - remainder);
	}
}



static uint32_t __inline bs_get(BITSTREAM * const bs, const uint32_t n)
{
	uint32_t ret = bs_show(bs, n);
	bs_skip(bs, n);
	return ret;
}


static uint32_t __inline bs_get1(BITSTREAM * const bs)
{
	return bs_get(bs, 1);
}


#endif /* _BITSTREAM_H_ */

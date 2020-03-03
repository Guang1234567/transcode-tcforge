#include "bitstream.h"


#define VO_START_CODE	0x8
#define VOL_START_CODE	0x12
#define VOP_START_CODE	0x1b6

#define SHAPE_RECTANGLE	0

#define MARKER()	bs_skip(bs, 1)

//#define DEBUGI(S,I) {fprintf(stderr, "%s %i\n", (S), (I));}

static int __inline log2bin(int value)
{
	int n = 0;
	while (value)
	{
		value >>= 1;
		n++;
	}
	return n;
}


void bs_init_tc(BITSTREAM * bs, char *bitstream)
{
	uint32_t tmp;

	tmp = *(uint32_t *)bitstream;
#ifndef BIG_ENDIAN
	BSWAP(tmp);
#endif
	bs->bufa = tmp;

	tmp = *((uint32_t *)bitstream + 1);
#ifndef BIG_ENDIAN
	BSWAP(tmp);
#endif
	bs->bufb = tmp;

	bs->pos = 0;
	bs->head = ((uint32_t *)bitstream + 2);
}


/*
video object layer
*/

int bs_vol(BITSTREAM *bs, DECODER *dec)
{
	uint32_t vol_ver_id;
	uint32_t shape;
	uint32_t time_inc_resolution;
	uint32_t width;
	uint32_t height;


	bs_bytealign(bs);

//	DEBUGI("***vo_startcode", bs_show(bs, 27));

	if (bs_show(bs,27) == VO_START_CODE) {
	    
	    bs_skip(bs, 27);			// vo_start_code
	    bs_skip(bs, 5);			// vo_id
	    
	    if (bs_show(bs, 28) == VOL_START_CODE)
	    {
		bs_skip(bs, 28);		// vol_start_code
			bs_skip(bs, 4);			// vol_id

			bs_skip(bs, 1);			// random_accessible_vol
			bs_skip(bs, 8);			// video_object_type_indication

			// ? fine granularity scalability

			if (bs_get1(bs))		// is_object_layer_identified
			{
				vol_ver_id = bs_get(bs,4);	// vol_ver_id
				bs_skip(bs, 3);				// vol_ver_priority
			}
			else
			{
				vol_ver_id = 1;
			}
			//			DEBUGI("vol_ver_id", vol_ver_id);

			bs_skip(bs, 4);			// aspect_ratio_info
			// todo: extended_PAR

			if (bs_get1(bs))		// vol_control_parameters
			{
				// todo
			}

			shape = bs_get(bs, 2);	// video_object_layer_shape
			//			DEBUGI("shape", shape);
			// todo: grayscale shape extension

			MARKER();

			time_inc_resolution = bs_get(bs, 16);	// time_increment_resolution
			dec->time_inc_bits = log2bin(time_inc_resolution);
			if (dec->time_inc_bits == 0) {
				dec->time_inc_bits = 1;
			}
			//			DEBUGI("tinc res", time_inc_resolution);

			MARKER();

			if (bs_get1(bs))						// fixed_vop_rate
			{
				bs_skip(bs, dec->time_inc_bits);	// fixed_time_increment
			}

			if (shape == SHAPE_RECTANGLE)
			{
				MARKER();
				width = bs_get(bs, 13);
				//				DEBUGI("width", width);
				MARKER();
				height = bs_get(bs, 13);
				//				DEBUGI("height", height);	
				MARKER();
				// not sure what to do with width/height
				// (verify against those supplied)
				// or should at every VOL realloc the buffers???
			}
			
			bs_skip(bs, 1);							// interlaced
			bs_skip(bs, 1);							// obmc_disable
			bs_skip(bs, (vol_ver_id == 1 ? 1 : 2));  // sprite_enable
			// todo: sprite enabled

			if (bs_get1(bs))						// not_8_bit
			{
				dec->quant_bits = bs_get(bs, 4);	// quant_precision
				bs_skip(bs, 4);						// bits_per_pixel
			}
			else
			{
				dec->quant_bits = 5;
			}

			dec->quant_type = bs_get1(bs);		// quant_type
			//			DEBUGI("quant_type", dec->quant_type);

			if (dec->quant_type)
			{
				bs_skip(bs, 1);				// load_intra_quant_mat
				bs_skip(bs, 1);				// load_inter_quant_mat
				//todo
			}

			// todo: grayscale stuff

			if (vol_ver_id != 1)
			{
				bs_skip(bs, 1);		// quarter_sample
				//todo
			}

			bs_skip(bs, 1);		// complexity_estimation_disable
			// todo: complexity est header

			bs_skip(bs, 1);		// resync_marker_disable
			bs_skip(bs, 1);		// data_partioned
			// todo: rvlc

			if (bs_get1(bs))	// scalability
			{
				// todo: 
			}
			return 0;
		}
	}
	return -1;
}



/*
video object plane
returns coding_type 
	-1 for error
*/

int bs_vop(BITSTREAM * bs, DECODER * dec, uint32_t * rounding, uint32_t * quant, uint32_t * fcode)
{
	uint32_t coding_type;

	bs_bytealign(bs);
	
//	bs_skip(bs, 4);
	//	DEBUGI("***vop_startcode", bs_show(bs, 32));

	if (bs_show(bs, 32) == VOP_START_CODE)
	{
		bs_skip(bs, 32);					// vop_start_code

		coding_type = bs_get(bs, 2);		// vop_coding_type
		// todo: bvops, svops
		//		DEBUGI("coding_type", coding_type);

		while (bs_get1(bs) == 1) ;			// time_base

		MARKER();

		bs_skip(bs, dec->time_inc_bits);	// vop_time_increment

		MARKER();

		if (bs_get1(bs))					// vop_coded
		{
			if (coding_type != I_VOP)
			{
				*rounding = bs_get1(bs);	// rounding_type
				//				DEBUGI("rounding", *rounding);
			}

			bs_skip(bs, 3);		// todo: intra_dc_vlc_threshold
						
			*quant = bs_get(bs, dec->quant_bits);		// vop_quant
			//			DEBUGI("quant", *quant);
						
			if (coding_type != I_VOP)
			{
				*fcode = bs_get(bs, 3);			// fcode_forward
				//				DEBUGI("fcode", *fcode);
			}
		}
		else
		{
			return N_VOP;
		}

		// todo: handle case where vop isnt coded

		return coding_type;
	}
	return -1;
}

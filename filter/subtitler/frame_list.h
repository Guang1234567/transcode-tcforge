#ifndef _FRAME_LIST_H_
#define _FRAME_LIST_H_

#define FRAME_HASH_SIZE	MAX_FRAMES

struct frame
	{
	char *name;

	int type;

	int end_frame;

	int xsize;
	int ysize;
	int zsize;

	char *data;

	font_desc_t *pfd;

	int id;

	int status;

	struct frame *nxtentr;
	struct frame *prventr;
	};
extern struct frame *frametab[];


struct subtitle_fontname
	{
	char *name; /* this is the fontname, font_symbols, fontsize, iso extension, outline_thickness, blur_radius */

	font_desc_t *pfd;

	struct subtitle_fontname *nxtentr;
	struct subtitle_fontname *prventr;
	};
extern struct subtitle_fontname *subtitle_fontnametab[];
		 /* first element points to first entry, second element to last entry */

#endif /* _FRAME_LIST_H */

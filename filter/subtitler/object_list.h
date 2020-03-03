#ifndef _OBJECT_LIST_H_
#define _OBJECT_LIST_H_

struct object
	{
	char *name;

	int start_frame;
	int end_frame;

	int type;

	double xpos;
	double ypos;
	double zpos;

	double dxpos;
	double dypos;
	double dzpos;

	double old_xpos;
	double old_ypos;
	double old_zpos;

	double xdest;
	double ydest;
	double zdest;
	double distance;

	double xsize;
	double ysize;
	double zsize;

	double dxsize;
	double dysize;
	double dzsize;

	double org_xsize;
	double org_ysize;
	double org_zsize;

	double xrotation;
	double yrotation;
	double zrotation;

	double dxrotation;
	double dyrotation;
	double dzrotation;

	double xshear;
	double yshear;
	double zshear;

	double dxshear;
	double dyshear;
	double dzshear;

	double heading;
	double dheading;

	double speed;
	double dspeed;
	double ddspeed;

	double saturation;
	double dsaturation;

	double hue;
	double dhue;

	double hue_line_drift;
	double dhue_line_drift;

	double u_shift;
	double du_shift;

	double v_shift;
	double dv_shift;

	double transparency;
	double dtransparency;

	double brightness;
	double dbrightness;

	double contrast;
	double dcontrast;

	double slice_level;
	double dslice_level;

	double mask_level;
	double dmask_level;

	double chroma_key_color;
	double dchroma_key_color;

	double chroma_key_saturation;
	double dchroma_key_saturation;

	double chroma_key_window;
	double dchroma_key_window;

	double extra_character_space;
	double dextra_character_space;

	int anti_alias_flag;

	int pattern;
	int background;
	int emphasis1;
	int emphasis2;

	int pattern_contrast;
	int background_contrast;
	int emphasis1_contrast;
	int emphasis2_contrast;

	char *font_dir;
	char *font_name;
	int font_symbols;
	int font_size;
	int font_iso_extension;
	double font_outline_thickness;
	double font_blur_radius;

	font_desc_t *pfd;

	int line_number; // line number in multiline formatted text
	int bg_y_start;
	int bg_y_end;
	int bg_x_start;
	int bg_x_end;

	double u;
	double du;

	double v;
	double dv;

	double color;
	double dcolor;

	double aspect;

	char *data;

	int id;

	/* commands working on main movie */
	double time_base_correct;
	double de_stripe;
	double show_output;

	/* add your variables here */

	int status;

	struct object *nxtentr;
	struct object *prventr;
	};
extern struct object *objecttab[];

#endif /* _OBJECT_LIST_H_ */



extern int rgb_to_yuv(int r, int g, int b, int *y, int *u, int *v);
extern font_desc_t *add_font(\
    char *name, int symbols, int size, int iso_extension, double outline_thickness, double blur_radius);
//extern void paste_bitmap(FT_Bitmap *bitmap, int x, int y);
extern int write_header(FILE *f);
extern int write_bitmap(void *buffer, char type);
extern int render(void);
//extern FT_ULong decode_char(char c);
extern int prepare_charset(void);
extern void outline(unsigned char *s, unsigned char *t, int width, int height, int *m, int r, int mwidth);
extern void outline1(unsigned char *s, unsigned char *t, int width, int height);
extern void blur(
unsigned char *buffer,\
unsigned char *tmp,\
int width,\
int height,\
int *m,\
int r,\
int mwidth,\
unsigned volume\
);
extern unsigned gmatrix(unsigned *m, int r, int w, double const A);
extern int alpha(double outline_thickness, double blur_radius);
extern font_desc_t *make_font(\
	char *font_name, int font_symbols, int font_size, int iso_extention, double outline_thickness, double blur_radius);
extern int chroma_key(int u, int v, double color,\
	double color_window, double saturation);
extern int set_main_movie_properties(struct object *pa);
extern void *movie_routine(char *temp);
extern void adjust_color(int *u, int *v, double degrees, double saturation);
extern int yuv_to_ppm(char *data, int xsize, int ysize, char *filename);
extern char *change_picture_geometry(\
	char *data, int xsize, int ysize,\
	double *new_xsize, double *new_ysize,\
	int keep_aspect,\
	double zrotation,\
	double xshear, double yshear);
extern int sort_objects_by_zaxis(void);
extern char *ppm_to_yuv_in_char(char *pathfilename, int *xsize, int *ysize);
extern int get_h_pixels(int c, font_desc_t *pfd);
extern char *p_reformat_text(char *text, int max_pixels, font_desc_t *pfd);
extern int p_center_text(char *text, font_desc_t *pfd);
extern int add_text(\
	int x, int y, char *text, struct object *pa, int u, int v,\
	double contrast, double transparency, font_desc_t *pfd,\
	int extra_char_space);
extern int draw_char(\
	int x, int y, int c, struct object *pa, int u, int v,\
	double contrast, double transparency, font_desc_t *pfd, int is_space);
extern void draw_alpha(\
	int x0 ,int y0,\
	struct object *pa,\
	int w, int h,\
	uint8_t *src, uint8_t *srca, int stride, int u, int v,\
	double contrast, double transparency, int is_space);
extern int print_options(void);
extern int hash(char *s);
extern char *strsave(char *s);
extern int readline(FILE *file, char *contents);
extern struct frame *lookup_frame(char *name);
extern struct frame *install_frame(char *name);
extern int delete_all_frames(void);
extern int add_frame(\
	char *name, char *data, int object_type,\
	int xsize, int ysize, int zsize, int id);
extern int set_end_frame_and_end_sample(int frame_nr, int end_frame);
extern char *get_path(char *filename);
extern raw_file* load_raw(char *name,int verbose);
extern font_desc_t* read_font_desc(char* fname,float factor,int verbose);
extern int readline_msdos(FILE *file, char *contents);
extern int load_ssa_file(char *pathfilename);
extern void read_in_ssa_file(FILE *finptr);
extern int load_ppml_file(char *pathfilename);
extern int read_in_ppml_file(FILE *finptr);
extern struct object *lookup_object(char *name);
extern struct object *install_object_at_end_of_list(char *name);
extern int delete_object(char *name);
extern int delete_all_objects(void);
extern int set_object_status(int start_frame_nr, int status);
extern int get_object_status(int start_frame_nr, int *status);
extern struct object *add_subtitle_object(\
	int start_frame_nr, int end_frame_nr, int type,\
	double xpos, double ypos, double zpos,\
	char *data);
extern int process_frame_number(int current_frame_nr);
extern void putimage(int xsize, int ysize);
extern int openwin(int argc, char *argv[], int xsize, int ysize);
extern unsigned char *getbuf(void);
extern void closewin(void);
extern int get_x11_bpp(void);
extern int resize_window(int xsize, int ysize);

extern int add_background(struct object *pa);
extern int add_objects(int);
extern int add_picture(struct object *pa);
extern int execute(char *);
extern int parse_frame_entry(struct frame *pa);
extern int readline_ppml(FILE *, char *);
extern int set_end_frame(int, int);
extern int swap_position(struct object *ptop, struct object *pbottom);

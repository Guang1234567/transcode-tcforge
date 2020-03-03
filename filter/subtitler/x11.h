#ifndef display_h
#define display_h

/* for the mouse, swap ye'r buttons here */
#define M_LEFT_DOWN     1
#define M_MIDDLE_DOWN   2
#define M_RIGHT_DOWN    3

typedef struct
	{
	int flags;
	int x, y;
	int buttons;
	int key;
	} MouseEvent;

#endif /* display_h */

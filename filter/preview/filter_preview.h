#ifndef _FILTER_PREVIEW_H
#define _FILTER_PREVIEW_H

#include <sys/types.h>
#include <sys/mman.h>

#include <libdv/dv_types.h>
#include <libdv/dv.h>

#include "display.h"

#define DV_PLAYER_OPT_VERSION         0
#define DV_PLAYER_OPT_DISABLE_AUDIO   1
#define DV_PLAYER_OPT_DISABLE_VIDEO   2
#define DV_PLAYER_OPT_NUM_FRAMES      3
#define DV_PLAYER_OPT_OSS_INCLUDE     4
#define DV_PLAYER_OPT_DISPLAY_INCLUDE 5
#define DV_PLAYER_OPT_DECODER_INCLUDE 6
#define DV_PLAYER_OPT_AUTOHELP        7
#define DV_PLAYER_OPT_DUMP_FRAMES     8
#define DV_PLAYER_NUM_OPTS            9

/* Book-keeping for mmap */
typedef struct dv_mmap_region_s {
  void   *map_start;  /* Start of mapped region (page aligned) */
  size_t  map_length; /* Size of mapped region */
  unsigned char *data_start; /* Data we asked for */
} dv_mmap_region_t;

typedef struct {
  dv_decoder_t    *decoder;
  dv_display_t    *display;
  dv_oss_t        *oss;
  dv_mmap_region_t mmap_region;
  struct stat      statbuf;
  struct timeval   tv[3];
  int             arg_disable_audio;
  int             arg_disable_video;
  int             arg_num_frames;
  int             arg_dump_frames;
} dv_player_t;

#endif

#ifndef __OGMSTREAMS_H
#define __OGMSTREAMS_H

/*
 * Taken from http://tobias.everwicked.com/packfmt.htm
 *
 * Changes: Prefixed types with ogm_ to avoid namespace polution
 *                             -- tibit
 *

 First packet (header)
 ---------------------

 pos    | content                 | description
 -------+-------------------------+----------------------------------
 0x0000 | 0x01                    | indicates 'header packet'
 -------+-------------------------+----------------------------------
 0x0001 | ogm_stream_header       | the size is indicated in the
        |                         | size member


 Second packet (comment)
 -----------------------

 pos    | content                 | description
 -------+-------------------------+----------------------------------
 0x0000 | 0x03                    | indicates 'comment packet'
 -------+-------------------------+----------------------------------
 0x0001 | data                    | see vorbis doc on www.xiph.org

 Data packets
 ------------

 pos      | content                 | description
 ---------+-------------------------+----------------------------------
 0x0000   | Bit0  0                 | indicates data packet
          | Bit1  Bit 2 of lenbytes |
          | Bit2  unused            |
          | Bit3  keyframe          |
          | Bit4  unused            |
          | Bit5  unused            |
          | Bit6  Bit 0 of lenbytes |
          | Bit7  Bit 1 of lenbytes |
 ---------+-------------------------+----------------------------------
 0x0001   | LowByte                 | Length of this packet in samples
          | ...                     | (frames for video, samples for
          | HighByte                | audio, 1ms units for text)
 ---------+-------------------------+----------------------------------
 0x0001+  | data                    | packet contents
 lenbytes |                         |

 *
 *
 */

//// oggDS headers
// Header for the new header format
typedef struct ogm_stream_header_video
{
  ogg_int32_t  width;
  ogg_int32_t  height;
} ogm_stream_header_video;

typedef struct ogm_stream_header_audio
{
  ogg_int16_t  channels;
  ogg_int16_t  blockalign;
  ogg_int32_t  avgbytespersec;
} ogm_stream_header_audio;

typedef struct ogm_stream_header
{
  char        streamtype[8];
  char        subtype[4];

  ogg_int32_t size;             // size of the structure

  ogg_int64_t time_unit;        // in reference time
  ogg_int64_t samples_per_unit;
  ogg_int32_t default_len;      // in media time

  ogg_int32_t buffersize;
  ogg_int16_t bits_per_sample;

  ogg_int16_t padding;

  union
  {
    // Video specific
    ogm_stream_header_video  video;
    // Audio specific
    ogm_stream_header_audio  audio;
  } sh;
} ogm_stream_header;

/// Some defines from OggDS
#define OGM_PACKET_TYPE_HEADER       0x01
#define OGM_PACKET_TYPE_COMMENT      0x03
#define OGM_PACKET_TYPE_BITS         0x07
#define OGM_PACKET_LEN_BITS01        0xc0
#define OGM_PACKET_LEN_BITS2         0x02
#define OGM_PACKET_IS_SYNCPOINT      0x08

#endif /* __OGMSTREAMS_H */

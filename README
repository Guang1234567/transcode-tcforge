
---------------------------------------------------
transcode - a video stream processing utility
---------------------------------------------------

transcode is a text-console utility for video stream processing,
running on a platform that supports shared libraries and threads.
Decoding and encoding is done by loading modules that are responsible
for feeding transcode with raw video/audio streams (import modules)
and encoding the frames (export modules). It supports elementary video
and audio frame transformations, including de-interlacing
or fast resizing of video frames and loading of external filters.

A number of modules are included to enable import of DVDs on-the-fly,
MPEG elementary (ES) or program streams (VOB), MPEG video, Digital Video (DV),
YUV4MPEG streams, NuppelVideo file format and raw or compressed
(pass-through) video frames and export modules for writing DivX;-),
DivX 4.02/5.xx, XviD, Digital Video, MPEG-1/2 or uncompressed
AVI files with MPEG, AC3 (pass-through) or PCM audio.
More file formats and codecs for audio/video import are supported by the
avifile library import module, the export with avifile is restricted to
video codecs only, with MPEG/PCM or AC3 (pass-through) audio provided by
transcode. Limited Quicktime export support and DVD subtitle rendering is
also avaliable.

It's modular concept is intended to provide flexibility and easy user
extensibility to include other video/audio codecs or file types.
A set of tools is available to extract, demultiplex and decode
the sources into raw video/audio streams for import, non AVI-file export
modules for writing single frames (PPM) or YUV4MPEG streams,
auto-probing and scanning your sources and to enable post-processing of
AVI files, including header fixing, merging multiple files or splitting
large AVI files to fit on a CD.

Written by Thomas Oestreich (ostreich@theorie.physik.uni-goettingen.de)
See the Authors file for contributions from the community.
See the file COPYING for license details.


USAGE:
------
./transcode -h | more

prints a list of available options. Check out
http://www.theorie.physik.uni-goettingen.de/~ostreich/transcode/
for more details.

use ^C for safely stopping the encoder.

now read the INSTALL file!!!

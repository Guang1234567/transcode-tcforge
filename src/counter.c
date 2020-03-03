/*
 * counter.c - transcode progress counter routines
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#include "transcode.h"
#include "counter.h"
#include "frame_threads.h"
#include <math.h>

/*************************************************************************/

static int counter_active = 0;    /* Is the counter active? */
static int frames_to_encode = 0;  /* Total number of frames to encode */
static int encoded_frames = 0;    /* Number of frames encoded so far */
static double encoded_time = 0;   /* Time spent encoding so far */
static int frames_to_skip = 0;    /* Total number of frames to skip */
static int skipped_frames = 0;    /* Number of frames skipped so far */
static double skipped_time = 0;   /* Time spent skipping so far */
static int highest_frame = 0;     /* Highest frame number to be seen */

static int printed = 0;           /* Have we printed a line? */

static void print_counter_line(int encoding, int frame, int first, int last,
                               double fps, double done, double timestamp,
                               int secleft, int decodebuf, int filterbuf,
                               int encodebuf);

/*************************************************************************/
/*************************************************************************/

/**
 * counter_on:  Activate the counter display.
 *
 * Parameters:
 *     None.
 * Return value:
 *     None.
 */

void counter_on(void)
{
    counter_active = 1;
}

/*************************************************************************/

/**
 * counter_off:  Deactivate the counter display.
 *
 * Parameters:
 *     None.
 * Return value:
 *     None.
 * Side effects:
 *     When in human-readable mode (tc_progress_meter == 1), if the counter
 *     has been displayed at least once, a newline is written to standard
 *     output.
 */

void counter_off(void)
{
    TCSession *session = tc_get_session();
    if (printed) {
        if (session->progress_meter == 1)
            fprintf(stderr, "\n");
        printed = 0;
    }
    counter_active = 0;
}

/*************************************************************************/

/**
 * counter_add_range:  Add the given range of frames to the total number of
 * frames to be encoded or skipped.
 *
 * Parameters:
 *      first: First frame of range.
 *       last: Last frame of range.
 *     encode: True (nonzero) if frames are to be encoded.
 *             False (zero) if frames are being skipped.
 * Return value:
 *     None.
 */

void counter_add_range(int first, int last, int encode)
{
    if (encode) {
        frames_to_encode += last+1 - first;
    } else {
        frames_to_skip += last+1 - first;
    }
    if (last > highest_frame)
        highest_frame = last;
}

/*************************************************************************/

/**
 * counter_reset_ranges:  Reset the counter's stored range data.
 *
 * Parameters:
 *     None.
 * Return value:
 *     None.
 */

void counter_reset_ranges(void)
{
    frames_to_encode = 0;
    encoded_frames = 0;
    encoded_time = 0;
    frames_to_skip = 0;
    skipped_frames = 0;
    skipped_time = 0;
    highest_frame = 0;
}

/*************************************************************************/

/**
 * counter_print:  Display the progress counter, if active.
 *
 * Parameters:
 *     encoding: True (nonzero) if frames are being encoded.
 *               False (zero) if frames are being skipped.
 *        frame: Current frame being encoded or skipped.
 *        first: First frame of current range.
 *         last: Last frame of current range, -1 if unknown.
 * Return value:
 *     None.
 */

void counter_print(int encoding, int frame, int first, int last)
{
    TCSession *session = tc_get_session();
    vob_t *vob = session->job;
    struct timeval tv;
    struct timezone dummy_tz = {0,0};
    double now, timediff, fps, time;
    int buf_im, buf_fl, buf_ex;
    /* Values of 'first' and `last' during last call (-1 = not called yet) */
    static int old_first = -1, old_last = -1;
    /* Time of first call for this range */
    static double start_time = 0;
    /* Time of last call */
    static double old_time = 0;

    if (!session->progress_meter
     || !session->progress_rate
     || !counter_active
     || frame % session->progress_rate != 0
    ) {
        return;
    }
    if (frame < 0 || first < 0) {
        static int warned = 0;
        if (!warned) {
            tc_log_warn(__FILE__, "invalid arguments to counter_print"
                        " (%d,%d,%d,%d)", encoding, frame, first, last);
            warned = 1;
        }
        return;
    }

#ifdef HAVE_GETTIMEOFDAY
    if (gettimeofday(&tv, &dummy_tz) != 0) {
        static int warned = 0;
        if (!warned) {
            tc_log_warn(__FILE__, "gettimeofday() failed!");
            warned = 1;
        }
        return;
    }
    now = tv.tv_sec + (double)tv.tv_usec/1000000.0;
#else
    now = time(NULL);
#endif

    timediff = now - old_time;
    old_time = now;
    if (old_first != first || old_last != last) {
        /* In human-readable mode, start a new counter line for each range
         * if we don't know the total number of frames to be encoded. */
        if (session->progress_meter == 1 && old_first != -1 && frames_to_encode == 0)
            fprintf(stderr, "\n");
        start_time = now;
        old_first = first;
        old_last = last;
        /* We decrement the frame counts here to compensate for this frame
         * which took an unknown amount of time to complete. */
        if (encoding && frames_to_encode > 0)
            frames_to_encode--;
        else if (!encoding && frames_to_skip > 0)
            frames_to_skip--;
        return;
    }

    /* Note that we don't add 1 to the numerator here, since start_time is
     * the time we were called for the first frame, so frame first+1 is one
     * one frame later than start_time, not two. */
    if (now > start_time) {
        fps = (frame - first) / (now - start_time);
    } else {
        /* No time has passed (maybe we don't have gettimeofday()) */
        fps = 0;
    }

    tc_framebuffer_get_counters(&buf_im, &buf_fl, &buf_ex);

    time = (double)frame / ((vob->ex_fps<1.0) ? 1.0 : vob->ex_fps);

    if (last == -1) {
        /* Can't calculate ETA, just display current timestamp */
        print_counter_line(encoding, frame, first, -1, fps, -1, time, -1,
                           buf_im, buf_fl, buf_ex);

    } else if (frames_to_encode == 0) {
        /* Total number of frames unknown, just display for current range */
        double done = (double)(frame - first + 1) / (double)(last+1 - first);
        int secleft = fps>0 ? ((last+1)-frame) / fps : -1;
        print_counter_line(encoding, frame, first, last, fps, done, time,
                           secleft, buf_im, buf_fl, buf_ex);

    } else {
        /* Estimate time remaining for entire run */
        double done;
        int secleft;
        if (encoding) {
            encoded_frames++;
            encoded_time += timediff;
        } else {
            skipped_frames++;
            skipped_time += timediff;
        }
        if (encoded_frames > frames_to_encode)
            frames_to_encode = encoded_frames;
        if (skipped_frames > frames_to_skip)
            frames_to_skip = skipped_frames;
        if (encoded_frames == 0) {
            /* We don't know how long it will take to encode frames; avoid
             * understating the ETA, and just say we don't know */
            secleft = -1;
        } else {
            double encode_fps, skip_fps, total_time;
            /* Find the processing speed for encoding and skipping */
            encode_fps = encoded_time ? encoded_frames / encoded_time : 0;
            if (skipped_frames > 0 && skipped_time > 0) {
                skip_fps = skipped_frames / skipped_time;
            } else {
                /* Just assume the same FPS for skipping as for encoding.
                 * Overstating the ETA isn't as bad as understating it, and
                 * certainly better than giving the user "unknown" for an
                 * entire encoding range. */
                skip_fps = encode_fps;
            }
            if (encode_fps > 0) {
                /* Estimate the total processing time required */
                total_time = (frames_to_encode / encode_fps)
                           + (frames_to_skip / skip_fps);
                /* Determine time left (round up) */
                secleft = ceil(total_time - (encoded_time + skipped_time));
            } else {
                total_time = -1;
                secleft = -1;
            }
            /* Use the proper overall FPS in the status line */
            fps = encoding ? encode_fps : skip_fps;
        }
        /* Just use the frame ratio for completion percentage */
        done = (double)(encoded_frames + skipped_frames)
             / (double)(frames_to_encode + frames_to_skip);
        print_counter_line(encoding, frame, 0, highest_frame, fps, done,
                           time, secleft, buf_im, buf_fl, buf_ex);
    }

    fflush(stdout);
}

/*************************************************************************/

/**
 * print_counter_line:  Helper function to format display arguments into a
 * progress counter line depending on settings.
 *
 * Parameters:
 *      encoding: True (nonzero) if frames are being encoded.
 *                False (zero) if frames are being skipped.
 *         frame: Current frame being encoded or skipped.
 *         first: First frame of current range.
 *          last: Last frame of current range, -1 if unknown.
 *           fps: Estimated frames processed per second.
 *          done: Completion ratio (0..1), -1 if unknown.
 *     timestamp: Timestamp of current frame, in seconds.
 *       secleft: Estimated time remaining to completion, in seconds (-1 if
 *                unknown).
 *     decodebuf: Number of buffered frames awaiting decoding.
 *     filterbuf: Number of buffered frames awaiting filtering.
 *     encodebuf: Number of buffered frames awaiting encoding.
 * Return value:
 *     None.
 */

static void print_counter_line(int encoding, int frame, int first, int last,
                               double fps, double done, double timestamp,
                               int secleft, int decodebuf, int filterbuf,
                               int encodebuf)
{
    TCSession *session = tc_get_session();
    if (session->progress_meter == 2) {
        /* Raw data format */
        printf("encoding=%d frame=%d first=%d last=%d fps=%.3f done=%.6f"
               " timestamp=%.3f timeleft=%d decodebuf=%d filterbuf=%d"
               " encodebuf=%d\n",
               encoding, frame, first, last, fps, done,
               timestamp, secleft, decodebuf, filterbuf, encodebuf);
    } else if (last < 0 || done < 0 || secleft < 0) {
        int timeint = floor(timestamp);
        fprintf(stderr, "%s frames [%d-%d], %6.2f fps, CFT: %d:%02d:%02d,"
                        "  (%2d|%2d|%2d) \r",
                encoding ? "encoding" : "skipping",
                first, frame,
                fps,
                timeint/3600, (timeint/60) % 60, timeint % 60,
                decodebuf, filterbuf, encodebuf
        );
    } else {
        char eta_buf[100];
        if (secleft < 0) {
            snprintf(eta_buf, sizeof(eta_buf), "--:--:--");
        } else {
            snprintf(eta_buf, sizeof(eta_buf), "%d:%02d:%02d",
                     secleft/3600, (secleft/60) % 60, secleft % 60);
        }
        fprintf(stderr, "%s frame [%d/%d], %6.2f fps, %5.1f%%, ETA: %s,"
                        " (%2d|%2d|%2d)  \r",
                encoding ? "encoding" : "skipping",
                frame, last+1,
                fps,
                floor(1000*done)/10,  // Round down to tenths of a percent
                eta_buf,
                decodebuf, filterbuf, encodebuf
        );
    }
    printed = 1;
}

/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */

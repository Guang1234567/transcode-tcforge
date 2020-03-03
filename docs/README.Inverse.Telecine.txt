NTSC, telecine, and how to cope (using telecide and decimate)
=============================================================
Based on the excellent work of Donald Graft (author of Decomb)

Filters Version: 0.2

Before we start, lets clear up the field a bit.

You DO NOT need any of the two filters (telecide and decimate)
if you are working with "pure interlaced data" as opposed to
"artificially interlaced data". In plain terms: You don't need
these two filters AND YOU SHOULD NOT USE THEM if your source
data are coming from:

- a decrypted PAL DVD (all European DVDs, for example)
- a captured .avi from a camera (be it PAL or NTSC)

For the above, you can use one of the other filters, like
"-J dilyuvmmx", "-I 1" or "-I 3" (or even smartdeinter).
Just remember to read the instructions.

Now that we've thrown away 90% of the readers, its time to
describe what is this artificial interlacing we (might)
meet in an NTSC DVD and why it requires special care.

23.976 -> 29.97
===============

NTSC television is broadcast at 29.97 frames per second.
Films however, (movies) are shot at 24 frames per second.
How are Americans watching movies on their TVs then?
Where do the extra 5.97 frames come from? :)
   As one might guess, American engineers had to interpolate.
They also had to cope with the fact that TV isn't actually
showing 29.97 frames per sec, but 2 x 29.97 fields per second.
What are fields, you ask? Well, check this ASCII art:

scanline 0 --------------------------------- Field 0, 1st line
scanline 1 +++++++++++++++++++++++++++++++++ Field 1, 1st line
scanline 2 --------------------------------- Field 0, 2nd line
scanline 3 +++++++++++++++++++++++++++++++++ Field 1, 2nd line
scanline 4 --------------------------------- Field 0, 3rd line
etc...

As you can see, each frame is composed from two fields. Each of
the fields has 240 lines, thus we finally get 480 scanlines.
So, to display film content, which is 24 frames per second,
this is what American engineers came up with:

- First, each film frame is split into two fields. We thus get
  48 fields per second (2 x 24)

- Then, a process called 3:2 pulldown (telecine) is performed.
  It's better to see it with an example:
  Assume we have 5 film frames, now split into 10 fields.
  The even scanlines are forming the 'top' fields,
  while the odd ones the 'bottom'.

  For example, the two first frames are split as follows:

    Frame 1 				Frame 2
    =======                         	=======
    Top Field of Frame 1    (T1) 	Top Field of Frame 2 	(T2)
    Bottom Field of Frame 1 (B1)	Bottom Field of Frame 2 (B2)

Now, if we had a 24fps Television set, we could see any film easily,
if we sent it the following sequence:

    T1 B1   T2 B2   T3 B3   T4 B4   T5 B5   T6 B6 ...
    ======  ======  ======  ======  ======  ======
    Frame1  Frame2  Frame3  Frame4  Frame5  Frame6

Unfortunately, we have a stupid 29.97 standard, so this is what we send:

    T1 B1 T2 B2 T2 B3 T3 B4 T4 B4 ...
    ===== ===== ===== ===== =====
      N     N     A     A     N

As you can see, we have used fields from 4 film frames, but we have
sent them in a way that produces 5 NTSC 'frames': 3 Normal (N) ones,
and two Artificial (A) ones. The displaying is also slowed down,
from 30 'frames' per sec to 29.97 fps.

You see it?
These Artificial frames are horribly interlaced!
And this interlacing has nothing to do with the normal interlacing
found in PAL and NTSC cameras... This is a different monster.

Note that if you use one of transcode's deinterlacing filters with
such a sequence, you'll probably get something like this:


    T1 B1 T2 B2 T3 B3 T4 B4 T4 B4 ...
    ===== ===== ===== ===== =====

... that is, two completely identical frames (IF the deinterlacing
algorithm is good enough). And yes, when you encode your sequence
to MPEG4 or whatever, you waste bandwidth for these extra frames.
Not to mention, that a skilled eye will notice that the film is not
natural - any motion will appear jerky, because of the duplicate
frames.
   Did I mention that this pattern shifts and jumps throughout the
video? In other words, this scheme is not constant; Video editing
is performed on fields, and if the DVD authoring studio deletes
a series of fields in the middle of the film, the eventual field
order is impossible to predict...

What can we do?
===============

Well, first of all, as good as transcode deinterlacing filters are,
they were not designed for this atrocity. Even "-I 3" falls back to
keeping odd (or even) scanlines and interpolates them to get the
ones missing. This is unacceptable.
The recovery process (called IVTC - inverse telecine) is done
in two steps, from two filters.
The first one, called 'ivtc', tries to recreate the original
film frames from the available fields. It doesn't remove the
duplicate frames; this is done from the next filter, the 'decimate'
one.

The algorithms used in these filters are the basic ones used in
the 'Decomb' package (made by Donald A. Graft and available -
unfortunately - only under Windows). Thanks for opening up the
source, Donald!

Example
=======

To summarize, let's see an example. The Dolby trailers are freely
available on the Web, and yes, they are purely telecined.
Let's transcode 'City' in a two pass perfect IVTC with Ogg sound:

First pass:
transcode -M 0 -f 23.976       -i dolby-city.vob -x vob -y xvid,null -w 740 -J ivtc,decimate -V -o /dev/null -R 1 -B 6,13,16

Second pass:
transcode -M 0 -f 23.976 -b 64 -i dolby-city.vob -x vob -y xvid,ogg  -w 740 -J ivtc,decimate -V -o test.avi  -R 2 -m test.ogg -B 6,13,16

Adding video and audio together in an Ogg stream (you'll need ogmtools):
ogmmerge -o dolby-city.ogm test.avi test.ogg

Some explaining:

 -w 740             Target video bitrate is 740000 bits per second

 -B 6,13,16         Resize from NTSC 720x480 to 512x384 (4:3 aspect)

 -M 0 -f 23.976     it means that the demux must not drop frames on
                    its own; the filters will do that, producing a
		    23.976 frames per sec result.

 -J ivtc,decimate   Always use them in this order, and with no other
                    filter before them - especially no deinterlacing
		    and no resizing one!

Problems
========

As good as these filters are, there are streams out there that are
simply impossible to IVTC. For example, amazing as it may sound,
some NTSC DVDs are switching from 29.97 to 23.976 IN THE MIDDLE
of playback! When there is no constant frame rate, the filters
will fail.
   Additionally, when the telecine pattern shifts (as a result
of, maybe, field editing by the DVD authors) a couple of
interlaced frames will pass through. These should be deinterlaced
on their own. You shouldn't use global deinterlacing after ivtc
- it would blur out the otherwise perfect progressive frames
that get reconstructed from 'ivtc'. However, transcode's '32detect'
filter comes to the rescue: It first checks whether the frame
is interlaced, and only then it forces a frame deinterlacing.
So, this is what I recommend for your sessions:

transcode -M 0 -f 23.976 -J ivtc,32detect=force_mode=3,decimate ...

To put it simply, 'ivtc' will try to fix the paranoia of telecine.
When the pattern shifts (or the algorithm fails) a couple of
interlaced frames will slip-by, which are detected and
deinterlaced by '32detect'. Finally, the 29.97 frames produced
for every second of input are fed into 'decimate', which removes
the extra frame.

Hope all this hasn't caused you a headache.
If your NTSC DVD contains a film, chances are these filters will
help (actually, they are the only 'correct' solution available
under Linux).

Since they work perfectly for my NTSC DVDs, I probably won't
mess them up anymore. Feel free to change them as you will,
as they are available under the GPL.

Thanassis Tsiodras
ttsiod@oreilly.softlab.ntua.gr

To e-mail, remove the well known publishing house from my address.
The joy of spam, you see...


Modification for Version 0.4.1 by Frederic Briere <fbriere at fbriere.net>
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Date: Tue, 6 Jan 2004 11:35:37 -0500
Subject: Tentative patch for ivtc

Hiya!  Here's something I whipped out in trying to improve ivtc a bit.
My motivation was that de-interlaced frames never look as good as "real"
ones, and if you're working with material that was telecined the "other"
way (repeating the top frame instead of the bottom frame, or vice versa,
I forget), then you end up with only two good frames out of four,
instead of three.

The easiest solution is to patch filter_decimate.c to reject the second
frame instead of the first, and that's what I've been doing for a few
months.  However, it seemed that the smartest way would be to adjust
filter_ivtc.c itself, which I finally got around to doing.


The field parameter specifies whether you want to work on the top field
(by default) or the bottom field.  Basically, when you enable
verboseness, if you see lots of "using 0" and "using 1", then you stand
to benefit from working on the opposite field instead.  (If ivtc is
using 1s and 2s, you've got it right.)

I also added a bit of "magic", to give ivtc a little nudge towards the
current field instead of the previous/next one.  When looking at the
verbose output, you'll see that ivtc often favors the "wrong" field, ie.
if you work on the top field, and have the following frames:

  Frame:          A  B  C  D  E
  Top field:      1  2  3  3  4
  Bottom field:   1  1  2  3  4

Then there is a good chance that frame D will end up with C's top field,
for reasons that are beyond me.

The values I chose are completely arbitrary, but they seemed to yield
good results for me.  Maybe they should be configurable, or maybe
there's a much better way to do this.  In any case, I made it an option,
so it's not in the way if you'd rather not use it.

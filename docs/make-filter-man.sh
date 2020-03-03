#!/bin/sh

# This scripts generates the FILTERS manpage section for transcode(1)
# It takes no arguments.
#
# If you want a new filter to be automatically documented, add it here to the
# list.
#
# After running the script, replace the FILTERS section in transcode.1 with the
# content of output.1
#
# (c) 2003 Tilmann Bitterberg
# Mon Sep  8 11:14:10 CEST 2003

#config

helpfile="filter-help.txt"
outfile="output.1"

# all filters to process
filter_list="
filter_29to23.so
filter_32detect.so
filter_32drop.so
filter_aclip.so
filter_astat.so
filter_clone.so
filter_compare.so
filter_control.so
filter_cpaudio.so
filter_cshift.so
filter_cut.so
filter_decimate.so
filter_denoise3d.so
filter_detectclipping.so
filter_detectsilence.so
filter_dilyuvmmx.so
filter_divxkey.so
filter_dnr.so
filter_doublefps.so
filter_extsub.so
filter_fieldanalysis.so
filter_fields.so
filter_fps.so
filter_hqdn3d.so
filter_invert.so
filter_ivtc.so
filter_levels.so
filter_logo.so
filter_logoaway.so
filter_lowpass.so
filter_mask.so
filter_modfps.so
filter_msharpen.so
filter_nored.so
filter_normalize.so
filter_null.so
filter_pp.so
filter_preview.so
filter_pv.so
filter_resample.so
filter_skip.so
filter_slowmo.so
filter_smartbob.so
filter_smartdeinter.so
filter_smartyuv.so
filter_smooth.so
filter_subtitler.so
filter_tc_audio.so
filter_tc_video.so
filter_test.so
filter_testframe.so
filter_text.so
filter_tomsmocomp.so
filter_unsharp.so
filter_videocore.so
filter_whitebalance.so
filter_xsharpen.so
filter_yuvdenoise.so
filter_yuvmedian.so
"

text=""

get_help_to_filter() {

  name=$1

  text=`sed -n "/^---------------------->\[ ${name}.help/,/^<----------------------|$/{
     /^---------------------->\[ ${name}.help/d
     /^<----------------------|$/d
     p;
  }" $helpfile`

}

>$outfile
for i in $filter_list;
do
    name=`echo $i | sed -e 's/[^_]*_//' -e 's/\.so//'`
    echo -n "Processing $name .."
    # rm -f $name.1 $name.txt
    tcmodinfo -i $name 2>&1| sed -n '/^START/,/^END/p' |
    sed '
/START/{
   N
   /\nEND/d
   s/START\n//
   bread
}

beer

:read
{
  h
  #   name                  comment       version      author       flags
  s|"filter_\([^.]*\)\.so", "\([^"]*\)", "\([^"]*\)", "\([^"]*\)", "\([^"]*\)",.*$|.TP 4\
\\fB\1\\fP - \\fB\2\\fP\
\\fB\1\\fP was written by \4. The version documented here is \3. |
  x

  # extract flags
  s/"filter_[^.]*\.so", "[^"]*", "[^"]*", "[^"]*", "\([^"]*\)",.*$/\1/
  s/V.*A/This is a video and audio filter. /
  s/A.*V/This is a video and audio filter. /
  s/V/This is a video filter. /
  s/A/This is a audio filter. /
  s/4.*R.*Y/It can handle rgb,yuv and yuv422 mode. /
  s/R.*4.*Y/It can handle rgb,yuv and yuv422 mode. /
  s/R.*Y.*4/It can handle rgb,yuv and yuv422 mode. /
  s/R.*Y/It can handle rgb and yuv mode. /
  s/Y.*R/It can handle rgb and yuv mode. /
  s/Y.*4/It can handle yuv and yuv422 mode. /
  s/4.*Y/It can handle yuv and yuv422 mode. /
  s/R/It can handle rgb mode only. /
  s/Y/It can handle yuv mode only. /
  s/M.*E.*O/It supports multiple instances and can run as a pre and\/or as a post filter. /
  s/E.*O.*M/It supports multiple instances and can run as a pre and\/or as a post filter. /
  s/O.*M.*E/It supports multiple instances and can run as a pre and\/or as a post filter. /
  s/M/It supports multiple instances. /
  s/E.*O/It can be used as a pre or as a post filter. /
  s/O.*E/It can be used as a pre or as a post filter. /
  s/E/It is a pre only filter. /
  s/O/It is a post only filter. /

  # fixup
  s/rgb/RGB/g
  s/yuv/YUV/g
  s/pre/pre-processing/g
  s/post/post-processing/g
  x;
  G
  s/\(.*\)\n\([^\n]*\)/\1\2/
  s/ *$/\
.IP\
.RS/
 p
 d
}

:eer
#now the fields

# "normal field"

s|"\([^"]*\)", "\([^"]*\)", "\([^"]*\)", "\([^"]*\)", .*|\\(bu \
.I \1 \
= \\fI\3\\fP  [default \\fI\4\\fP] \
.RS 3\
\2\
.RE|

# string

s|"\([^"]*\)", "\([^"]*\)", "\(%s\)", "\([^"]*\)"|\\(bu \
.I \1 \
= \\fI\3\\fP\
.RS 3\
\2\
.RE|

# bool

s|"\([^"]*\)", "\([^"]*\)", "\([^"]*\)", "\([^"]*\)"|\\(bu \
.I \1 \
(bool) \
.RS 3\
\2\
.RE|

/^END/d
    '>> $outfile

    get_help_to_filter $name

    if [ ! -z "$text" ]; then
      echo ".IP" >> $outfile
      echo "$text" >> $outfile 2>/dev/null
    fi
    echo -e ".RE" >> $outfile
    echo " done"

done

# empty lines removal
sed '
/\.IP/{
  N;
  N;
  /\.IP\n\.RS\n\.RE/d
}' $outfile > $outfile.new
mv $outfile.new $outfile

#!/bin/sed -f
# sed -f make-modules-man < export-mods.txt | tbl | groff -S -Wall -mtty-char -mandoc -Tascii | col | less

/^ *$/d

# 1i\
# .TH transcode 1 "1st November 2003" "transcode(1)"\
# .SH NAME\
# transcode \- LINUX video stream processing tool\
#
# start the work
/^M: /{
s/^M: [ei][xm]port_\([^.]*\)\.cp*/.TP 4\
\\fB\1\\fP/
N
s/\nD: / \\- /
s/$/\
.br/
}

/^C: /{
/none/s/.*/This module has no compile-time dependencies./
/This/!{
  s/C: //
  s/, / and /g
  s/^/At compile-time /
  s/$/ must be available./
  }
}

/^R: /{
/none/s/.*/This module has no run-time dependencies./
/This/!{
  s/R: //
  s/, / and /g
  s/^/At run-time /
  s/$/ must be present./
  }
}

/^S: /{
s/S: -/Support for this module is poor./
s/S: o/Support for this module is fair./
s/S: +/Support for this module is good./
}

# enhance this to allow multiple lines
/^I: /{
s/I: /.RS 8\
/
s/$/\
.br/
}

/^P: /{
s/^P: /Supported processing formats: /
s/$/\
.RE/
}


#!/bin/bash
#
# test-cfg-filelist.sh -- frontend for configuration file testsuite.
#                         autogenerates test data.
# (C) 2007-2010 - Francesco Romani <fromani -at- gmail -dot- com>
#
# This file is part of transcode, a video stream processing tool.
#
# transcode is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# transcode is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

REF="$( tempfile -p tclst -s .cfg )"
NEW="$( tempfile -p tclst -s .cfg )"
PROG="./test-cfg-filelist"
if [ -n "$1" ]; then
	PROG="$1"
fi


function makelist() {
	echo "[$1]"
	for F in "$1"/*; do
		echo "$F"
	done
}

function testit() {
	makelist "$1"                                     >> "$REF"
	"$PROG" "$REF" "$1" 2>&1 | sed 's/^\[test\]\ //g' >> "$NEW"
	if diff -q "$REF" "$NEW" &> /dev/null ; then
		printf "%-24s OK\n" "($1)"
	else
		printf "%-24s FAILED -> see ($REF|$NEW)\n" "($1)"
		return 1
	fi
	return 0
}

export "TRANSCODE_NO_LOG_COLOR=1"

DIRS="/usr /boot /usr/bin /usr/lib"
I=0
J=0
for DIR in $DIRS; do
	if testit $DIR; then
		let J=$J+1
	else
		break
	fi
	let I=$I+1
done
#rm -f "$REF" "$NEW"

echo "test succesfull/runned = $J/$I"

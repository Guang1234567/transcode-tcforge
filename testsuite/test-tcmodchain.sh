#!/bin/bash
#
# test-tcmodchain.sh -- modules compatibility testsuite.
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

# FIXME: default args,

# get program path, if given
TCMODCHAIN="tcmodchain"
if [ -n "$1" ]; then
	TCMODCHAIN="$1"
fi

if [ ! -x "$TCMODCHAIN" ]; then
	echo "missing tcmodchain program, test aborted" 1>&2
	exit 1
fi

# test helper

# $1, $2 -> modules
# $3, expected return code
function check_test() {
	if [ -z "$1" ] || [ -z "$2" ] || [ -z "$3" ]; then
		echo "bad test parameters (skipped)" 1>&2
		return 1
	fi
	$TCMODCHAIN -C $1 $2 -d 0 # silent operation
	local RET="$?"
	if [ "$RET" == "$3" ]; then
		printf "testing check (%16s) with (%16s) | OK\n" $1 $2
	else
		printf "testing check (%16s) with (%16s) | >> FAILED << [exp=%i|got=%i]\n" $1 $2 $3 $RET
	fi
	return $RET
}

# $1, $2 -> modules
# $3, expected result list
function list_test() {
	if [ -z "$1" ] || [ -z "$2" ] || [ -z "$3" ]; then
		echo "bad test parameters (skipped)" 1>&2
		return 1
	fi
	local GOT=$( $TCMODCHAIN -L $1 $2 | sort | tr '\n' ' ' | sed s/\ $//g )
	if [ "$GOT" == "$3" ]; then
		printf "testing list  (%16s) with (%16s) | OK\n" $1 $2
	else
		printf "testing list  (%16s) with (%16s) | >> FAILED <<\n" $1 $2
		printf "    expected=\"%s\"" $3
		printf "    received=\"%s\"" $GOT
	fi
	return $RET
}



## `check' (-C) tests first
#
check_test "encode:null" "multiplex:null" 0
check_test "encode:copy" "multiplex:null" 0
check_test "encode:xvid" "multiplex:null" 0
check_test "encode:x264" "multiplex:null" 0
check_test "encode:lame" "multiplex:null" 0
check_test "encode:faac" "multiplex:null" 0
check_test "encode:lzo"  "multiplex:null" 0
#
check_test "encode:null" "multiplex:raw" 0
check_test "encode:copy" "multiplex:raw" 0
check_test "encode:xvid" "multiplex:raw" 0
check_test "encode:x264" "multiplex:raw" 0
check_test "encode:lame" "multiplex:raw" 0
check_test "encode:faac" "multiplex:raw" 0
check_test "encode:lzo"  "multiplex:raw" 0
#
check_test "encode:null" "multiplex:avi" 0
check_test "encode:copy" "multiplex:avi" 0
check_test "encode:xvid" "multiplex:avi" 0
check_test "encode:x264" "multiplex:avi" 0
check_test "encode:lame" "multiplex:avi" 0
check_test "encode:faac" "multiplex:avi" 0
check_test "encode:lzo"  "multiplex:avi" 0
#
# see manpage for return code meaning
check_test "encode:null" "multiplex:y4m" 0
check_test "encode:copy" "multiplex:y4m" 0
check_test "encode:xvid" "multiplex:y4m" 3
check_test "encode:x264" "multiplex:y4m" 3
check_test "encode:lame" "multiplex:y4m" 3
check_test "encode:faac" "multiplex:y4m" 3
check_test "encode:lzo"  "multiplex:y4m" 3
#
## `check' (-L) tests then
#
list_test "encode:*" "multiplex:null" "copy faac lame lzo null x264 xvid"
list_test "encode:*" "multiplex:raw"  "copy faac lame lzo null x264 xvid"
list_test "encode:*" "multiplex:avi"  "copy faac lame lzo null x264 xvid"
list_test "encode:*" "multiplex:y4m"  "copy null"
#
list_test "encode:copy" "multiplex:*" "avi null raw y4m"
list_test "encode:faac" "multiplex:*" "avi null raw"
list_test "encode:lame" "multiplex:*" "avi null raw"
list_test "encode:lzo"  "multiplex:*" "avi null raw"
list_test "encode:null" "multiplex:*" "avi null raw y4m"
list_test "encode:x264" "multiplex:*" "avi null raw"
list_test "encode:xvid" "multiplex:*" "avi null raw"

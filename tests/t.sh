#!/bin/sh

# Copyright (c) 2019 Anton Lindqvist <anton@basename.se>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

set -eu

# assert_eq want got [message]
assert_eq() {
	[ "$1" = "$2" ] && return 0

	# $3 is intentionally unquoted since it's optional.
	printf 'WANT:\t%s\nGOT:\t%s\n' "$1" "$2" | fail - ${3:-}
}

# assert_file file0 file1
assert_file() {
	if ! cmp -s "$1" "$2"; then
                diff -u -L want -L got "$1" "$2" | fail - "unexpected output:"
	fi
	return 0
}

# fail [message]
fail() {
	NERR=$((NERR + 1))

	report -f -p FAIL "$@"
}

# testcase [-t tag] description
testcase() {
	local _tags=""

	# Report on behalf of the previous test case.
	[ "$NTEST" -gt 0 ] && report -p PASS

	NTEST=$((NTEST + 1))
	TCREPORT=0

	find "$WRKDIR" -mindepth 1 -delete

	while [ $# -gt 0 ]; do
		case "$1" in
		-t)	shift; _tags="${_tags} ${1}";;
		*)	break;;
		esac
		shift
	done
	TCDESC="$*"

	if [ -s "$INCLUDE" ]; then
		case "$FILTER" in
		f)	echo "$TCDESC";;
		t)	echo "$_tags";;
		esac | grep -q -f "$INCLUDE" && return 0
	elif [ -s "$EXCLUDE" ]; then
		case "$FILTER" in
		F)	echo "$TCDESC";;
		T)	echo "$_tags";;
		esac | grep -q -f "$EXCLUDE" || return 0
	else
		return 0
	fi
	report -p SKIP
	return 1
}

# Everything below is part of the private API, relying on it is a bad idea.

# fatal message
fatal() {
	echo "t.sh: ${*}" 1>&2
	exit 1
}

# report [-] [-f] -p prefix [message]
report() {
	local _force=0  _prefix="" _stdin=0 _tmp="${WRKDIR}/report"

	while [ $# -gt 0 ]; do
		case "$1" in
		-)	_stdin=1;;
		-f)	_force=1;;
		-p)	shift; _prefix="$1";;
		*)	break;;
		esac
		shift
	done

	if [ "$_force" -eq 0 ] && [ "$TCREPORT" -eq 1 ]; then
		return 0
	fi
	TCREPORT=1

	# Try hard to output everything to stderr in one go.
	{
		printf '%s: %s: %s' "$_prefix" "$NAME" "$TCDESC"
		[ $# -gt 0 ] && printf ': %s' "$*"
		echo
		[ $_stdin -eq 1 ] && cat
	} >"$_tmp"
	cat <"$_tmp" 1>&2
}

# atexit file ...
atexit() {
	local _err="$?"

	if [ "$NTEST" -gt 0 ]; then
		# Report on behalf of the previous test case.
		if [ "$_err" -eq 0 ]; then
			report -p PASS
		else
			report -p FAIL
		fi
	fi

	# Remove temporary files.
	rm -rf "$@"

	if [ "$NERR" -ne 0 ]; then
		exit 1
	fi
	exit "$_err"
}

usage() {
	echo "usage: sh t.sh [-f filter] [-t tag] file ..." 1>&2
	exit 1
}

# Keep the include and exclude files outside of the temporary directory since
# it's wiped between test cases.
INCLUDE="$(mktemp -t t.sh.XXXXXX)"
EXCLUDE="$(mktemp -t t.sh.XXXXXX)"
WRKDIR="$(mktemp -d -t t.sh.XXXXXX)"
trap 'atexit $INCLUDE $EXCLUDE $WRKDIR' EXIT

FILTER=""	# filter mode
NAME=""		# test file name
NERR=0		# total number of errors
NTEST=0		# total number of executed test cases
TCDESC=""	# current test case description
TCREPORT=0	# current test called report

while getopts "F:f:t:T:" opt; do
	case "$opt" in
	f|t)	FILTER="$opt"
		echo "$OPTARG" >>"$INCLUDE"
		;;
	F|T)	FILTER="$opt"
		echo "$OPTARG" >>"$EXCLUDE"
		;;
	*)	usage;;
	esac
done
shift $((OPTIND - 1))
[ $# -eq 0 ] && usage
if [ -s "$INCLUDE" ] && [ -s "$EXCLUDE" ]; then
	fatal "including and excluding tests is mutually exclusive"
fi

for a; do
	NAME="${a##*/}"
	. "$a"
done

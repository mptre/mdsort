set -e

usage() {
	echo "usage: sh run.sh [-e env] [-s skip] -b binary test-file ..." 1>&2
	exit 1
}

atexit() {
	local _err=$?

	rm -rf "$@"
	([ $_err -ne 0 ] || [ $NERR -gt 0 ]) && exit 1
	exit 0
}

fail() {
	printf 'FAIL: %s: %s\n' "$TCDESC" "$@" 1>&2
	TCFAIL=1
	NERR=$((NERR + 1))
}

pass() {
	[ $TCFAIL -eq 1 ] && return 0

	printf 'PASS: %s\n' "$TCDESC"
}

cppvar() {
	cpp - <<-EOF >$_TMP1 2>/dev/null
	#include <limits.h>
	#include <stdio.h>

	${1}
	EOF

	grep -v '^$' <$_TMP1 | tail -1
}

fcmp() {
	if ! cmp -s "$1" "$2"; then
		fail "unexpected output:"
		diff -u -L want -L got "$1" "$2" || return 1
	fi
	return 0
}

# mdsort [-] [mdsort-argument ...]
mdsort() {
	local _input=0 _exit=0

	if [ "$1" = "-" ]; then
		cat >$_TMP2
		_input=1
		shift
	fi

	env $ENV "$MDSORT" -f mdsort.conf "$@" >$_TMP1 2>&1 || _exit=1
	if [ $TCEXIT -ne $_exit ]; then
		fail "exits ${TCEXIT} != ${_exit}"
		cat "$_TMP1" 1>&2
	fi

	if [ $_input -eq 0 ]; then
		cat "$_TMP1"
		return 0
	else
		fcmp "$_TMP2" "$_TMP1" && pass
	fi
}

# mkmd path ...
mkmd() {
	for p; do
		for d in cur new tmp; do
			mkdir -p "${p}/${d}"
		done
	done
}

# mkmsg directory [suffix]
mkmsg() {
	local _name _path

	NMSG=$((NMSG + 1))
	_name=$(printf '%d.%d_%d.hostname%s' "$(date '+%s')" "$$" "$NMSG" "$2")
	_path="${1}/${_name}"

	if ! [ -e "$_path" ]; then
		mkdir -p "$1"
		cat >"$_path"
	else
		echo "${_path}: message already exists"
		return 1
	fi
}

testcase() {
	[ "$1" = "-e" ] && { TCEXIT=1; shift; } || TCEXIT=0
	TCDESC="${TCFILE}: $@"
	TCFAIL=0
	ls -d $MAILDIR/*/ 2>/dev/null | xargs rm -rf

	if echo "$TCDESC" | grep -q -f $SKIP; then
		echo "SKIP: ${TCDESC}"
		return 1
	else
		return 0
	fi
}

# randstr length predicate
randstr() {
	local _len=$1 _pred=$2

	>$_TMP1
	while [ $(wc -c "$_TMP1" | xargs | cut -d ' ' -f 1) -lt "$_len" ]; do
		dd if=/dev/urandom of=/dev/stdout "bs=${_len}" count=1 \
			2>/dev/null | tr -cd "[:${_pred}:]" >>$_TMP1
	done
	cut -b "-${_len}" "$_TMP1"
}

ENV=
NERR=0
NMSG=0
TCFILE=""
TCDESC=""
TCEXIT=0
TCFAIL=0

MAILDIR=$(mktemp -d -t mdsort.XXXXXX)
trap "atexit $MAILDIR" EXIT

CONF="${MAILDIR}/mdsort.conf"

# Temporary files used in tests.
TMP1="${MAILDIR}/tmp1"
TMP2="${MAILDIR}/tmp2"
TMP3="${MAILDIR}/tmp3"

# Temporary files used internally.
_TMP1="${MAILDIR}/_tmp1"
_TMP2="${MAILDIR}/_tmp2"
_TMP3="${MAILDIR}/_tmp3"

SKIP="${MAILDIR}/skip"
>$SKIP

while getopts "b:e:s:" opt; do
	case "$opt" in
	b)	MDSORT=$OPTARG;;
	e)	ENV="${ENV} ${OPTARG}";;
	s)	echo "$OPTARG" >>$SKIP;;
	*)	usage;;
	esac
done
shift $((OPTIND - 1))
([ $# -eq 0 ] || [ -z "$MDSORT" ]) && usage

ls "$MDSORT" >/dev/null || exit 1

# Platform specific values.
BUFSIZ=$(cppvar BUFSIZ || echo 0)
PATH_MAX=$(cppvar PATH_MAX || echo 0)

cd $MAILDIR

for f; do
	TCFILE="$(basename "$f")"
	. "$f"
done

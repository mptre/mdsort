set -e

usage() {
	echo "usage: sh run.sh mdsort test-file ..." 1>&2
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
	cpp - <<-EOF >$TMP1 2>/dev/null
	#include <limits.h>
	#include <stdio.h>

	${1}
	EOF

	grep -v '^$' <$TMP1 | tail -1
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
		_input=1
		shift
	fi

	env "$ENV" "$MDSORT" -f mdsort.conf "$@" >"$TMP1" 2>&1 || _exit=1
	if [ $TCEXIT -ne $_exit ]; then
		fail "exits ${TCEXIT} != ${_exit}"
		cat "$TMP1" 1>&2
	fi

	if [ $_input -eq 0 ]; then
		cat "$TMP1"
		return 0
	else
		fcmp - "$TMP1" && pass
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
	TCDESC="$@"
	TCFAIL=0
	ls -d $MAILDIR/*/ 2>/dev/null | xargs rm -rf
}

# randstr length predicate
randstr() {
	local _len=$1 _pred=$2

	>"$TMP1"
	while [ $(wc -c "$TMP1" | xargs | cut -d ' ' -f 1) -lt "$_len" ]; do
		dd if=/dev/urandom of=/dev/stdout "bs=${_len}" count=1 \
			2>/dev/null | tr -cd "[:${_pred}:]" >>"$TMP1"
	done
	cut -b "-${_len}" "$TMP1"
}

[ $# -lt 2 ] && usage

MDSORT=$1

ENV="MALLOC_OPTIONS=S"
NERR=0
NMSG=0
TCDESC=""
TCEXIT=0
TCFAIL=0

MAILDIR=$(mktemp -d -t mdsort.XXXXXX)
CONF="${MAILDIR}/mdsort.conf"
TMP1="${MAILDIR}/tmp1"
TMP2="${MAILDIR}/tmp2"
TMP3="${MAILDIR}/tmp3"
trap "atexit $MAILDIR" EXIT

# Platform specific values, skip on macOS.
if [ "$(uname)" != "Darwin" ]; then
	BUFSIZ=$(cppvar BUFSIZ || echo 0)
	PATH_MAX=$(cppvar PATH_MAX || echo 0)
else
	BUFSIZ=0
	PATH_MAX=0
fi

cd $MAILDIR

shift
for f; do
	. "$f"
done

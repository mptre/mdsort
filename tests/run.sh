set -e

usage() {
	echo "usage: sh run.sh [-e env] [-s skip] -b binary test-file ..." 1>&2
	exit 1
}

atexit() {
	local _err=$?

	assert_pass

	rm -rf "$@"
	([ $_err -ne 0 ] || [ $NERR -gt 0 ]) && exit 1
	exit 0
}

fail() {
	printf 'FAIL: %s\n\t%s\n' "$TCDESC" "$@" 1>&2
	TCFAIL=1
	NERR=$((NERR + 1))
}

pass() {
	[ $TCFAIL -eq 1 ] && return 0

	TCFAIL=0
	printf '%s: %s\n' "${1:-PASS}" "$TCDESC"
}

assert_empty() {
	ls "${MAILDIR}/${1}" 2>/dev/null | cmp -s - /dev/null && return 0
	fail "expected directory ${1} to be empty"
}

refute_empty() {
	ls "${MAILDIR}/${1}" 2>/dev/null | cmp -s - /dev/null || return 0
	fail "expected directory ${1} to not be empty"
}

assert_pass() {
	if [ $TCFAIL -lt 0 ]; then
		fail "pass never called"
	fi
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
		diff -u -L want -L got "$1" "$2" || return 0
	fi
	return 0
}

# mdsort [- | -D] [mdsort-argument ...]
mdsort() {
	local _args="-f mdsort.conf" _input=0 _exit=0

	while :; do
		case "$1" in
		-)	cat >$_TMP2
			_input=1
			;;
		-D)	_args=;;
		*)	break;;
		esac
		shift
	done

	env $ENV "$MDSORT" $_args "$@" >$_TMP1 2>&1 || _exit=1
	if [ $TCEXIT -ne $_exit ]; then
		fail "exits ${TCEXIT} != ${_exit}"
		cat "$_TMP1" 1>&2
	fi

	if [ $_input -eq 0 ]; then
		cat "$_TMP1"
		return 0
	else
		fcmp "$_TMP2" "$_TMP1"
	fi
}

# mkmsg [-H] [-b] [-s suffix] [-- headers ...]
mkmsg() {
	local _body=0 _dir _headers=1 _name _path _suffix

	while [ $# -gt 0 ]; do
		case "$1" in
		-b)	_body=1;;
		-H)	_headers=0;;
		-s)
			shift
			_suffix="$1"
			;;
		*)	break
		esac
		shift
	done

	_dir="${MAILDIR}/${1}"; shift
	mkdir -p "$_dir"

	while :; do
		_name=$(printf '%d.%d_%d.hostname%s' \
			"$(date '+%s')" "$$" "$RANDOM" "$_suffix")
		_path="${_dir}/${_name}"
		[ -e "$_path" ] || break
	done
	touch "$_path"

	if [ "$1" = "--" ]; then
		shift
		while [ $# -gt 0 ]; do
			echo "${1}: ${2}" >>$_path
			shift 2
		done
	fi
	[ $_headers -eq 1 ] && printf 'Content-Type: text/plain\n\n' >>$_path
	[ $_body -eq 1 ] && cat >>$_path || true
}

testcase() {
	assert_pass

	[ "$1" = "-e" ] && { TCEXIT=1; shift; } || TCEXIT=0
	TCDESC="${TCFILE}: $@"
	TCFAIL=-1
	ls -d $MAILDIR/*/ 2>/dev/null | xargs rm -rf

	if echo "$TCDESC" | grep -q -f $SKIP; then
		pass "SKIP"
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

# Temporary files used internally.
_TMP1="${MAILDIR}/_tmp1"
_TMP2="${MAILDIR}/_tmp2"

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

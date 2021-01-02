# assert_empty dir [message]
assert_empty() {
	if ! _assert_empty "$@"; then
		fail "${2:-expected ${1} to be empty}"
	fi
}

# refute_empty dir [message]
refute_empty() {
	if _assert_empty "$@"; then
		fail "${2:-expected ${1} to not be empty}"
	fi
}

# _assert_empty dir
_assert_empty() {
	local _dir="$1"

	# Append TSHDIR if relative.
	if [ "$_dir" = "${_dir#/}" ]; then
		_dir="${TSHDIR}/${_dir}"
	fi

	ls "${_dir}" 2>/dev/null | cmp -s - /dev/null
}

# assert_find dir pattern [message]
assert_find() {
	if ! _assert_find "$@"; then
		fail "${3:-expected ${1}/${2} to not be empty}"
	fi
}

# refute_find dir pattern [message]
refute_find() {
	if _assert_find "$@"; then
		fail "${3:-expected ${1}/${2} to be empty}"
	fi
}

# _assert_find dir pattern
_assert_find() {
	! find "${TSHDIR}/${1}" -type f -name "$2" | cmp -s - /dev/null
}

# assert_label label file
assert_label() {
	local _got

	if [ $# -ne 2 ]; then
		fail "assert_label: too many arguments: ${*}"
	fi

	_got="$(sed -n -e '/^X-Label/s/^[^:]*: //p' "$2")"
	assert_eq "$1" "$_got"
}

# b64 string ...
#
# Encode the given strings using base64.
b64() {
	if command -v b64encode >/dev/null 2>&1; then
		echo "$@" | b64encode - | sed -e '1d' -e '$d'
	elif command -v base64 >/dev/null 2>&1; then
		echo "$@" | base64
	else
		fail "base64 encode utility not found"
	fi
}

cppvar() {
	local _tmp="${TSHDIR}/cppvar"

	cpp - <<-EOF >"$_tmp" 2>/dev/null
	#include <limits.h>
	#include <stdio.h>

	${1}
	EOF

	grep -v -e '^#' -e '^$' <"$_tmp" | tail -1
}

# findmsg [-g pattern] dir
findmsg() {
	local _pattern="."

	while [ $# -gt 0 ]; do
		case "$1" in
		-g)	shift; _pattern="$1";;
		*)	break;;
		esac
		shift
	done

	find "${TSHDIR}/${1}" -type f |
	xargs grep -l "$_pattern" |
	sed "s,${TSHDIR}/,,"
}

# genstr length
genstr() {
	dd if=/dev/zero of=/dev/stdout "bs=${1}" count=1 2>/dev/null | tr '\0' 'x'
}

# mdsort [-D] [-e | -t] [-f fault] [-] [-- mdsort-argument ...]
mdsort() {
	local _args="-f mdsort.conf"
	local _core="${TSHDIR}/cdump"
	local _exit1=0
	local _exit2=0
	local _fault=""
	local _input=""
	local _output=1
	local _sig=""
	local _tmp="${TSHDIR}/mdsort"
	local _tmpdir

	while [ $# -gt 0 ]; do
		case "$1" in
		-)	_input="${TSHDIR}/input"
			cat >"$_input"
			;;
		-D)	_args=""
			;;
		-e)	_exit1=1
			;;
		-f)	shift; _fault="FAULT=${1}"
			;;
		-t)	_exit1=75
			;;
		--)	shift
			break
			;;
		*)	fail "mdsort: unknown test options: ${*}"
			return 0
			;;
		esac
		shift
	done

	_tmpdir="${TSHDIR}/_tmpdir"
	mkdir "$_tmpdir"

	(cd "$TSHDIR" && env "TMPDIR=${_tmpdir}" ${_fault} ${EXEC:-} \
		"$MDSORT" $_args "$@") >"$_tmp" 2>&1 || _exit2="$?"

	# Find coredump(s) and optionally preserve them.
	find "$TSHDIR" -name '*core*' >"$_core"
	if ! cmp -s "$_core" /dev/null; then
		if [ -n "$COREDUMP" ]; then
			cp "$(cat "$_core")" "${COREDUMP}/mdsort.core"
		fi
		fail - "found coredump" <"$_tmp"
	fi

	if [ "$_exit1" -ne "$_exit2" ]; then
		if [ "$_exit2" -gt 128 ]; then
			_sig=" (signal $((_exit2 - 128)))"
		fi
		fail - "want exit ${_exit1}, got ${_exit2}${_sig}" <"$_tmp"
		# Output already displayed, prevent from doing it twice.
		_output=0
	fi

	assert_empty "$_tmpdir" "temporary directory not empty"

	if [ -n "$_input" ]; then
		assert_file "$_input" "$_tmp"
	elif [ "$_output" -eq 1 ]; then
		cat "$_tmp"
	fi
}

# mkmd dir ...
mkmd() {
	local _a _b

	for _a; do
		for _b in cur new tmp; do
			mkdir -p "${TSHDIR}/${_a}/${_b}"
		done
	done
}

# mkmsg [-A] [-H] [-b] [-m modified-time] [-s suffix] dir [-- headers ...]
mkmsg() {
	local _dir _name _path
	local _attachments=0
	local _body=0
	local _headers=1
	local _suffix=""
	local _tim=""

	while [ $# -gt 0 ]; do
		case "$1" in
		-A)	_attachments=1;;
		-b)	_body=1;;
		-H)	_headers=0;;
		-m)	shift
			_tim="$1"
			;;
		-s)	shift
			_suffix="$1"
			;;
		*)	break
		esac
		shift
	done

	_dir="${TSHDIR}/${1}"; shift

	while :; do
		_name=$(printf '1553633333.%d_%d.hostname%s' \
			"$$" "$NMSG" "$_suffix")
		_path="${_dir}/${_name}"
		[ -e "$_path" ] || break

		NMSG=$((NMSG + 1))
	done

	{
		# Optional headers.
		if [ "${1:-}" = "--" ]; then
			shift
			while [ $# -gt 0 ]; do
				echo "${1}: ${2}"
				shift 2
			done
		fi

		# Default headers.
		if [ "$_attachments" -eq 1 ]; then
			printf 'Content-Type: multipart/alternative;boundary="deadbeef"\n'
		elif [ "$_headers" -eq 1 ]; then
			printf 'Content-Type: text/plain\n'
		fi

		# Start of body.
		printf '\n'

		# Optional body.
		if [ "$_attachments" -eq 1 ]; then
			cat <<-EOF
			--deadbeef
			Content-Type: text/plain

			First attachment.
			--deadbeef
			Content-Type: text/calendar

			Second attachment.
			--deadbeef--
			EOF
		elif [ $_body -eq 1 ]; then
			cat
		fi
	} >"$_path"

	if [ -n "$_tim" ]; then
		touch -m -t "$_tim" "$_path"
	fi
}

# now [-f format] [delta]
now() {
	local _tim
	local _fmt='%a, %d %b %Y %H:%M:%S %z'

	while [ $# -gt 0 ]; do
		case "$1" in
		-f)
			shift
			_fmt="$1"
			;;
		*)	break;;
		esac
		shift
	done

	_tim=$(date +%s)
	if [ $# -eq 1 ]; then
		_tim=$((_tim + $1))
	fi

	${DATE}${_tim} "+${_fmt}"
}

ls "${MDSORT:?}" >/dev/null || exit 1

# Temporary files used in tests.
CONF="${TSHDIR}/mdsort.conf"
TMP1="${TSHDIR}/tmp1"
TMP2="${TSHDIR}/tmp2"

# Platform specific values.
BUFSIZ=$(cppvar BUFSIZ || echo 0)
NAME_MAX=$(cppvar NAME_MAX || echo 0)
PATH_MAX=$(cppvar PATH_MAX || echo 0)

# Figure out date(1) seconds option.
if date -r 0 >/dev/null 2>&1; then
	DATE="date -r"
else
	DATE="date -d@"
fi

# Enable hardening malloc(3) options on OpenBSD.
case "$(uname -s)" in
OpenBSD)	export MALLOC_OPTIONS="RS";;
esac

# Number of messages created by mkmsg.
NMSG=0

export LC_ALL=C

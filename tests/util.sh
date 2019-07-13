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

	# Append WRKDIR if relative.
	if [ "$_dir" = "${_dir#/}" ]; then
		_dir="${WRKDIR}/${_dir}"
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
	! find "${WRKDIR}/${1}" -type f -name "$2" | cmp -s - /dev/null
}

cppvar() {
	local _tmp="${WRKDIR}/cppvar"

	cpp - <<-EOF >"$_tmp" 2>/dev/null
	#include <limits.h>
	#include <stdio.h>

	${1}
	EOF

	grep -v -e '^#' -e '^$' <"$_tmp" | tail -1
}

# genstr length
genstr() {
	dd if=/dev/zero of=/dev/stdout "bs=${1}" count=1 2>/dev/null | tr '\0' 'x'
}

# mdsort [-D] [-e | -t] [-] [-- mdsort-argument ...]
mdsort() {
	local _args="-f mdsort.conf" _exit1=0 _exit2=0 _input="" _tmp="${WRKDIR}/mdsort" _tmpdir

	while [ $# -gt 0 ]; do
		case "$1" in
		-)	_input="${WRKDIR}/input"
			cat >"$_input"
			;;
		-D)	_args=""
			;;
		-e)	_exit1=1
			;;
		-t)	_exit1=75
			;;
		--)	shift
			break
			;;
		*)	fail "mdsort: unknown test options: ${@}"
			return 0
			;;
		esac
		shift
	done

	_tmpdir="${WRKDIR}/_tmpdir"
	mkdir "$_tmpdir"

	(cd "$WRKDIR" && env "TMPDIR=${_tmpdir}" ${EXEC:-} "$MDSORT" $_args "$@") \
		>"$_tmp" 2>&1 || _exit2="$?"
	if [ "$_exit1" -ne "$_exit2" ]; then
		fail - "want exit ${_exit1}, got ${_exit2}" <"$_tmp"
	fi

	assert_empty "$_tmpdir" "temporary directory not empty"

	if [ -n "$_input" ]; then
		assert_file "$_input" "$_tmp"
	else
		cat "$_tmp"
	fi
}

# mkmd dir ...
mkmd() {
	local _a _b

	for _a; do
		for _b in cur new tmp; do
			mkdir -p "${WRKDIR}/${_a}/${_b}"
		done
	done
}

# mkmsg [-H] [-b] [-s suffix] dir [-- headers ...]
mkmsg() {
	local _body=0 _headers=1 _suffix="" _dir _i _name _path

	while [ $# -gt 0 ]; do
		case "$1" in
		-b)	_body=1;;
		-H)	_headers=0;;
		-s)	shift
			_suffix="$1"
			;;
		*)	break
		esac
		shift
	done

	_dir="${WRKDIR}/${1}"; shift

	_i=0
	while :; do
		_name=$(printf '1553633333.%d_%d.hostname%s' \
			"$$" "$NMSG" "$_suffix")
		_path="${_dir}/${_name}"
		[ -e "$_path" ] || break

		NMSG=$((NMSG + 1))
	done
	touch "$_path"

	if [ "${1:-}" = "--" ]; then
		shift
		while [ $# -gt 0 ]; do
			echo "${1}: ${2}" >>$_path
			shift 2
		done
	fi
	if [ $_headers -eq 1 ]; then
		printf 'Content-Type: text/plain\n' >>$_path
	fi
	echo >>$_path
	if [ $_body -eq 1 ]; then
		cat >>$_path
	fi
}

now() {
	local _fmt='%a, %d %b %Y %H:%M:%S %z' _tim=$(date +%s)

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

	if [ $# -eq 1 ]; then
		_tim=$((_tim + $1))
	fi

	(date -r "$_tim" "+${_fmt}" 2>/dev/null ||
		date -d "@${_tim}" "+${_fmt}")
}

ls "$MDSORT" >/dev/null || exit 1

# Temporary files used in tests.
CONF="${WRKDIR}/mdsort.conf"
TMP1="${WRKDIR}/tmp1"
TMP2="${WRKDIR}/tmp2"

# Platform specific values.
BUFSIZ=$(cppvar BUFSIZ || echo 0)
PATH_MAX=$(cppvar PATH_MAX || echo 0)

# Number of messages created by mkmsg.
NMSG=0

LC_ALL=C
export LC_ALL

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

# mkattach [-m multipart] dir -- type encoding body ...
#
# Create a message with one or many attachments.
mkattach() {
	local _mp="alternative"
	local _body _dir _enc _type

	while [ $# -gt 0 ]; do
		case "$1" in
		-m)	shift; _mp="$1";;
		*)	break;;
		esac
		shift
	done
	_dir="$1"; : "${_dir:?}"; shift

	while [ $# -gt 0 ]; do
		_type="$1"; : "${_type:?}"; shift
		_enc="$1"; : "${_enc:?}"; shift
		_body="$1"; : "${_body:?}"; shift

		printf -- '--boundary\nContent-Type: %s\n' "$_type"
		if [ "$_enc" != "identity" ]; then
			printf 'Content-Transfer-Encoding: %s\n' "$_enc"
		fi
		printf '\n%s\n' "$_body"

		[ $# -eq 0 ] && printf -- '--boundary--\n'
	done | mkmsg -H -b "$_dir" -- \
		"Content-Type" "multipart/${_mp}; boundary=\"boundary\""
}

if testcase "basic"; then
	mkmd "src" "dst"
	mkattach "src/new" "text/plain; charset=utf-8" "base64" "$(b64 hello)"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /hello/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "plain text is favored over html"; then
	mkmd "src" "dst"
	mkattach "src/new" \
		"text/html" "base64" "$(b64 html)" \
		"text/plain" "base64" "$(b64 plain)"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /plain/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

# The first plain text attachment is favored.
if testcase "multiple plain text"; then
	mkmd "src" "dst"
	mkattach "src/new" \
		"text/plain" "base64" "$(b64 first)" \
		"text/plain" "base64" "$(b64 second)"
	mkattach "src/new" \
		"text/plain" "base64" "$(b64 second)" \
		"text/plain" "base64" "$(b64 first)"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /first/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "multiple body rules"; then
	mkmd "src" "dst"
	mkattach "src/new" "text/plain" "base64" "$(b64 hello)"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /nein/ move "dst"
		match body /hello/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

# Since the type is not recognized, it must match the encoded representation.
if testcase "unknown type"; then
	mkmd "src" "dst"
	mkattach "src/new" "image/jpeg" "base64" "$(b64 encode)"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /$(b64 encode)/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

# Since the encoding is not recognized, it must match the encoded
# representation.
if testcase "unknown encoding"; then
	mkmd "src" "dst"
	mkattach "src/new" "text/plain" "base32" "$(b64 encode)"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /$(echo encode | b64)/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

# Since the multipart is not recgonized, any attachment must not be decoded.
if testcase "unknown multipart"; then
	mkmd "src"
	mkattach -m "unknown" "src/new" "text/plain" "base64" "$(b64 hello)"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /hello/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "invalid base64"; then
	mkmd "src"
	mkattach "src/new" "text/plain" "base64" "invalid"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /./ move "dst"
	}
	EOF
	mdsort -e >/dev/null
	refute_empty "src/new"
fi

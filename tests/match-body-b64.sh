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

if testcase "no attachments"; then
	mkmd "src" "dst"
	b64 hello | mkmsg -b "src/new" -- "Content-Transfer-Encoding" "base64"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /hello/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

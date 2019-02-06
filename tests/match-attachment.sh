if testcase "no pattern"; then
	mkmd "src" "dst"
	mkmsg -b -H "src/new" <<-EOF -- \
		"Content-Type" "multipart/alternative; boundary=\"deadbeef\""
	--deadbeef
	Content-Type: text/plain

	First attachment.

	--deadbeef
	Content-Type: text/plain

	Second attachment.

	--deadbeef--
	EOF
	cat <<-EOF >$CONF
	maildir "src" {
		match attachment move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "with pattern"; then
	mkmd "src" "dst"
	mkmsg -b -H "src/new" <<-EOF -- \
		"Content-Type" "multipart/alternative;boundary=\"deadbeef\""
	--deadbeef
	Content-Type: text/plain

	First attachment.
	--deadbeef
	Content-Type: text/calendar

	Second attachment.
	--deadbeef--
	EOF
	cat <<-EOF >$CONF
	maildir "src" {
		match attachment /text\/calendar/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "with pattern no match"; then
	mkmd "src" "dst"
	mkmsg -b -H "src/new" <<-EOF -- \
		"Content-Type" "multipart/alternative; boundary=\"deadbeef\""
	--deadbeef
	Content-Type: text/plain

	First attachment.
	--deadbeef
	Content-Type: text/calendar

	Second attachment.
	--deadbeef--
	EOF
	cat <<-EOF >$CONF
	maildir "src" {
		match attachment /text\/html/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_empty "dst/new"
fi

if testcase "negate"; then
	mkmd "src" "dst"
	mkmsg -b -H "src/new" <<-EOF -- \
		"Content-Type" "multipart/alternative; boundary=\"deadbeef\""
	--deadbeef
	Content-Type: text/plain

	First attachment.
	--deadbeef
	Content-Type: text/calendar

	Second attachment.
	--deadbeef--
	EOF
	cat <<-EOF >$CONF
	maildir "src" {
		match ! attachment /text\/html/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "multipart mixed"; then
	mkmd "src" "dst"
	mkmsg -b -H "src/new" <<-EOF -- \
		"Content-Type" "multipart/mixed; boundary=\"deadbeef\""
	--deadbeef
	Content-Type: text/plain

	First attachment.
	--deadbeef--
	EOF
	cat <<-EOF >$CONF
	maildir "src" {
		match attachment move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "content type unknown"; then
	mkmd "src"
	mkmsg -H "src/new" -- "Content-Type" "multipart/octet-stream"
	cat <<-EOF >$CONF
	maildir "src" {
		match attachment move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "content type missing"; then
	mkmd "src"
	mkmsg -H "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match attachment move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "content type boundary empty"; then
	mkmd "src"
	mkmsg -H "src/new" -- "Content-Type" "multipart/alternative; boundary=\"\""
	cat <<-EOF >$CONF
	maildir "src" {
		match attachment move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "content type boundary invalid"; then
	mkmd "src"
	mkmsg -H "src/new" -- "Content-Type" "multipart/alternative; boundary=\"xxx"
	cat <<-EOF >$CONF
	maildir "src" {
		match attachment move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "content type boundary missing"; then
	mkmd "src"
	mkmsg -H "src/new" -- "Content-Type" "multipart/alternative"
	cat <<-EOF >$CONF
	maildir "src" {
		match attachment move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "content type boundary without attachments"; then
	mkmd "src"
	mkmsg -b -H "src/new" <<-EOF -- \
		"Content-Type" "multipart/alternative; boundary=\"deadbeef\""
	No attachment.
	EOF
	cat <<-EOF >$CONF
	maildir "src" {
		match attachment move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "body boundary invalid"; then
	mkmd "src"
	mkmsg -b -H "src/new" <<-EOF -- \
		"Content-Type" "multipart/alternative; boundary=\"deadbeef\""
	--deadbeef
	Content-Type: text/plain

	First attachment.
	--deadbeef
	EOF
	cat <<-EOF >$CONF
	maildir "src" {
		match attachment move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "dry run without pattern"; then
	mkmd "src"
	mkmsg -b -H "src/new" <<-EOF -- \
		"Content-Type" "multipart/alternative; boundary=\"deadbeef\""
	--deadbeef
	Content-Type: text/plain

	First attachment.

	--deadbeef--
	EOF
	cat <<-EOF >$CONF
	maildir "src" {
		match attachment move "dst"
	}
	EOF
	cat <<EOF >$TMP1
mdsort.conf:2: Content-Type: text/plain
                             ^        $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	assert_file $TMP1 $TMP2
fi

if testcase "dry run with pattern"; then
	mkmd "src"
	mkmsg -b -H "src/new" <<-EOF -- \
		"Content-Type" "multipart/alternative; boundary=\"deadbeef\""
	--deadbeef
	Content-Type: text/plain

	First attachment.

	--deadbeef--
	EOF
	cat <<-EOF >$CONF
	maildir "src" {
		match attachment /text\/plain/ move "dst"
	}
	EOF
	cat <<EOF >$TMP1
mdsort.conf:2: Content-Type: text/plain
                             ^        $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	assert_file $TMP1 $TMP2
fi

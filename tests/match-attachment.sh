if testcase "presence"; then
	mkmd "src" "dst"
	mkmsg -b -H "src/new" <<-EOF -- \
		"Content-Type" "multipart/alternative;\n\tboundary=\"deadbeef\""
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

if testcase "header"; then
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
		match attachment header "Content-Type" /text\/calendar/ \
			move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "header no match"; then
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
		match attachment header "Content-Type" /text\/html/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_empty "dst/new"
fi

if testcase "header negate"; then
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
		match ! attachment header "Content-Type" /text\/html/ \
			move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "many headers"; then
	mkmd "src" "dst"
	mkmsg -b -H "src/new" <<-EOF -- \
		"Content-Type" "multipart/alternative;boundary=\"deadbeef\""
	--deadbeef
	From: user@localhost
	To: admin@example.com

	--deadbeef--
	EOF
	cat <<-EOF >$CONF
	maildir "src" {
		match attachment header { "From" "To" } /example/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "body"; then
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
		match attachment body /attachment/ move "dst"
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
	mdsort -e >/dev/null
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
	mdsort -e >/dev/null
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
	mdsort -e >/dev/null
	refute_empty "src/new"
fi

if testcase "terminating boundary invalid"; then
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
	mdsort -e >/dev/null
	refute_empty "src/new"
fi

if testcase "nested"; then
	mkmd "src" "dst"
	mkmsg -b -H "src/new" <<-EOF -- \
		"Content-Type" "multipart/alternative; boundary=\"one\""
	--one
	Content-Type: multipart/alternative; boundary="two"

	--two
	Content-Type: text/plain

	--two--
	--one--
	EOF
	cat <<-EOF >$CONF
	maildir "src" {
		match attachment header "Content-Type" /text\/plain/ \
			move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "nested too deep"; then
	mkmd "src"

	{
		i=0
		while :; do
			if [ $i = 10 ]; then
				printf 'Content-Type: text/plain\n\n'
				break
			elif [ $i -gt 0 ]; then
				printf 'Content-Type: multipart/alternative; boundary="%d"\n\n' $i
				printf -- '--%d\n' $i
			else
				printf -- '--%d\n' $i
			fi
			i=$((i + 1))
		done

		while :; do
			i=$((i - 1))
			printf -- '--%d--\n\n' $i
			[ $i = 0 ] && break
		done
	} | mkmsg -b -H "src/new" -- "Content-Type" "multipart/alternative; boundary=\"0\""

	cat <<-EOF >$CONF
	maildir "src" {
		match attachment move "dst"
	}
	EOF
	mdsort -e >/dev/null
	refute_empty "src/new"
fi

if testcase "dry run"; then
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
		match attachment header "Content-Type" /text\/plain/ \
			move "dst"
	}
	EOF
	cat <<EOF >$TMP1
mdsort.conf:2: Content-Type: text/plain
                             ^        $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	assert_file $TMP1 $TMP2
fi

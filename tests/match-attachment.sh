# xmkmsg dir
xmkmsg() {
	mkmsg -b -H "$1" <<-EOF -- \
		"Content-Type" "multipart/alternative;boundary=\"deadbeef\""
	--deadbeef
	Content-Type: text/plain

	First attachment.
	--deadbeef
	Content-Type: text/calendar

	Second attachment.
	--deadbeef--
	EOF
}

if testcase "match and"; then
	mkmd "src" "dst"
	xmkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment \
			header "Content-Type" |text/| and body /attachment/ \
			move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "match or"; then
	mkmd "src" "dst"
	xmkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment header "From" /admin/ or all move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "match negate"; then
	mkmd "src"
	xmkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment ! body /attachment/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "match attachment"; then
	mkmd "src"
	xmkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment attachment new move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "match body"; then
	mkmd "src" "dst"
	xmkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment body /attachment/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "match header"; then
	mkmd "src" "dst"
	xmkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment header "Content-Type" |text/calendar| move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "match new"; then
	mkmd "src" "dst"
	xmkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment new move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "match old"; then
	mkmd "src" "dst"
	xmkmsg "src/cur"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment old move "dst"
	}
	EOF
	mdsort
	assert_empty "src/cur"
	refute_empty "dst/cur"
fi

if testcase "no attachments"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment all move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
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
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment all move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "content type unknown"; then
	mkmd "src"
	mkmsg -H "src/new" -- "Content-Type" "multipart/octet-stream"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment all move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "content type missing"; then
	mkmd "src"
	mkmsg -H "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment all move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "content type boundary empty"; then
	mkmd "src"
	mkmsg -H "src/new" -- "Content-Type" "multipart/alternative; boundary=\"\""
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment all move "dst"
	}
	EOF
	mdsort -e - <<-EOF
	mdsort: $(findmsg "src/new"): invalid boundary
	EOF
	refute_empty "src/new"
fi

if testcase "content type boundary invalid"; then
	mkmd "src"
	mkmsg -H "src/new" -- "Content-Type" "multipart/alternative; boundary=\"xxx"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment all move "dst"
	}
	EOF
	mdsort -e - <<-EOF
	mdsort: $(findmsg "src/new"): invalid boundary
	EOF
	refute_empty "src/new"
fi

if testcase "content type boundary missing"; then
	mkmd "src"
	mkmsg -H "src/new" -- "Content-Type" "multipart/alternative"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment all move "dst"
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
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment all move "dst"
	}
	EOF
	mdsort -e
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
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment all move "dst"
	}
	EOF
	mdsort -e
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
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment header "Content-Type" |text/plain| \
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

	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment all move "dst"
	}
	EOF
	mdsort -e - -- <<-EOF
	mdsort: $(findmsg "src/new"): message contains too many nested attachments
	EOF
	refute_empty "src/new"
fi

if testcase -t regress "close file descriptor"; then
	mkmd "src"
	xmkmsg "src/new"
	xmkmsg "src/cur"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match body /nein/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	refute_empty "src/cur"
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
	cat <<-EOF >"$CONF"
	maildir "src" {
		match attachment header "Content-Type" |text/plain| \
			move "dst"
	}
	EOF
	mdsort - -- -d <<EOF
$(findmsg "src/new") -> dst/new
mdsort.conf:2: Content-Type: text/plain
                             ^        $
EOF
fi

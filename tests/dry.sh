if testcase "match header on first line"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /example.com/ move "${MAILDIR}/dst"
	}
	EOF
	cat <<EOF >$TMP2
To: user@example.com
         ^         $
EOF
	mdsort -d | tail -n +2 >$TMP3
	fcmp $TMP2 $TMP3 && pass
fi

if testcase "match header on middle line"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<EOF
To: admin@example.com,
	user@example.com,
	no-reply@example.com

EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user/ move "${MAILDIR}/dst"
	}
	EOF
	cat <<EOF >$TMP2
To: admin@example.com,
	user@example.com,
        ^  $
EOF
	mdsort -d | tail -n +2 >$TMP3
	fcmp $TMP2 $TMP3 && pass
fi

if testcase "match header on last line"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<EOF
To: admin@example.com,
  user@example.com,

EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user/ move "${MAILDIR}/dst"
	}
	EOF
	cat <<EOF >$TMP2
To: admin@example.com,
  user@example.com,
  ^  $
EOF
	mdsort -d | tail -n +2 >$TMP3
	fcmp $TMP2 $TMP3 && pass
fi

if testcase "match header with tab indent"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<EOF
Cc: admin@example.com,
	user@example.com

EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "Cc" /user/ move "${MAILDIR}/dst"
	}
	EOF
	cat <<EOF >$TMP2
Cc: admin@example.com,
	user@example.com
        ^  $
EOF
	mdsort -d | tail -n +2 >$TMP3
	fcmp $TMP2 $TMP3 && pass
fi

if testcase "match header negate"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: admin@example.com,

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match ! header "To" /user/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort -d >$TMP2
	grep -q '^.*src/new.* -> .*/dst/new$' $TMP2 || fail "expected move line"
	pass
fi

if testcase "match body on first line"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	Hello
	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match body /hello/i move "${MAILDIR}/dst"
	}
	EOF
	cat <<-EOF >$TMP2
	Hello
	^   $
	EOF
	mdsort -d | tail -n +2 >$TMP3
	fcmp $TMP2 $TMP3 && pass
fi

if testcase "match body on first line no newline"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	printf 'To: user@example.com\n\nHello' | mkmsg "${MAILDIR}/src/new"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match body /hello/i move "${MAILDIR}/dst"
	}
	EOF
	cat <<-EOF >$TMP2
	Hello
	^   $
	EOF
	mdsort -d | tail -n +2 >$TMP3
	fcmp $TMP2 $TMP3 && pass
fi

if testcase "match body on middle line"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	Hi,
	Hello hello
	Bye
	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match body /hello/ move "${MAILDIR}/dst"
	}
	EOF
	cat <<-EOF >$TMP2
	Hello hello
	      ^   $
	EOF
	mdsort -d | tail -n +2 >$TMP3
	fcmp $TMP2 $TMP3 && pass
fi

if testcase "match body spanning multiple lines"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	He
	llo
	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match body /h.*/i move "${MAILDIR}/dst"
	}
	EOF
	mdsort -d | tail -n +2 >$TMP3
	fcmp - $TMP3 </dev/null && pass
fi

if testcase "match many headers and body"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	Cc: admin@example.com
	To: user@example.com

	Hello!
EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "Cc" /admin/ and header "To" /user/ \
			and body /hello/i move "${MAILDIR}/dst"
	}
	EOF
	cat <<EOF >$TMP2
Cc: admin@example.com
    ^   $
To: user@example.com
    ^  $
Hello!
^   $
EOF
	mdsort -d | tail -n +2 >$TMP3
	fcmp $TMP2 $TMP3 && pass
fi

if testcase "matches from previous evaluations are discarded"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	Bye!
	EOF
	mkmsg "${MAILDIR}/src/new" <<-EOF
	Cc: admin@example.com

	Hello!
	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match (header "Cc" /admin/ or header "To" /user/) and \
			body /hello/i move "${MAILDIR}/dst"
	}
	EOF
	cat <<EOF >$TMP2
Cc: admin@example.com
    ^   $
Hello!
^   $
EOF
	mdsort -d | tail -n +2 >$TMP3
	fcmp $TMP2 $TMP3 && pass
fi

if testcase "matches from previous evaluations are discarded, inverted"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	Cc: admin@example.com

	Bye!
	EOF
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	Hello!
	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match (header "Cc" /admin/ or header "To" /user/) and \
			body /hello/i move "${MAILDIR}/dst"
	}
	EOF
	cat <<EOF >$TMP2
To: user@example.com
    ^  $
Hello!
^   $
EOF
	mdsort -d | tail -n +2 >$TMP3
	fcmp $TMP2 $TMP3 && pass
fi

testcase "match header on first line"
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

testcase "match header on middle line"
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

testcase "match header on last line"
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

testcase "match header with tab indent"
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

testcase "match header negate"
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

testcase "match body on first line"
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

testcase "match body on first line no newline"
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

testcase "match body on middle line"
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

testcase "match body spanning multiple lines"
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

testcase "match many headers and body"
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

testcase "matches from previous evaluations are discarded"
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

testcase "matches from previous evaluations are discarded, inverted"
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

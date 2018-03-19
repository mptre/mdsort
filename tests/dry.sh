testcase "match on first line"
	mkmd "${MAILDIR}/dst"
	mkmd "${MAILDIR}/src"
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

testcase "match on middle line"
	mkmd "${MAILDIR}/dst"
	mkmd "${MAILDIR}/src"
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

testcase "match on last line"
	mkmd "${MAILDIR}/dst"
	mkmd "${MAILDIR}/src"
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

testcase "match with tab indent"
	mkmd "${MAILDIR}/dst"
	mkmd "${MAILDIR}/src"
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

testcase "match negate"
	mkmd "${MAILDIR}/dst"
	mkmd "${MAILDIR}/src"
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

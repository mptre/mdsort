if testcase "match body"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	Hello Bob
	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match body /Bob/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	grep -q "Bob" ${MAILDIR}/dst/new/* || \
		fail "expected dst/cur directory to not be empty"
	pass
fi

if testcase "match body negate"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	Hello Alice
	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match ! body /Bob/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	grep -q "Alice" ${MAILDIR}/dst/new/* || \
		fail "expected dst/new directory to not be empty"
	pass
fi

if testcase "match body with empty body"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match body /Bob/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null && \
		fail "expected src/new directory to not be empty"
	ls "${MAILDIR}/dst/new" | cmp -s - /dev/null || \
		fail "expected dst/new directory to be empty"
	pass
fi

if testcase "match body with malformed body"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	printf 'To: user@example.com\nSubject: foo' | mkmsg "${MAILDIR}/src/new"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match body /Bob/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null && \
		fail "expected src/new directory to not be empty"
	ls "${MAILDIR}/dst/new" | cmp -s - /dev/null || \
		fail "expected dst/new directory to be empty"
	pass
fi

if testcase "match header"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	Subject:
	To: user@example.com

	EOF
	mkmsg "${MAILDIR}/src/cur" <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	grep -q "To: user@example.com" ${MAILDIR}/dst/new/* || \
		fail "expected dst/new directory to not be empty"
	ls "${MAILDIR}/src/cur" | cmp -s - /dev/null || \
		fail "expected src/cur directory to be empty"
	grep -q "To: user@example.com" ${MAILDIR}/dst/cur/* || \
		fail "expected dst/cur directory to not be empty"
	pass
fi

if testcase "match header negate"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	EOF
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: admin@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match ! header "To" /user/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null && \
		fail "expected src/new directory to not be empty"
	grep -q "To: user@example.com" ${MAILDIR}/src/new/* || \
		fail "expected src/new directory to not be empty"
	ls "${MAILDIR}/dst/new" | cmp -s - /dev/null && \
		fail "expected dst/new directory to not be empty"
	grep -q "To: admin@example.com" ${MAILDIR}/dst/new/* || \
		fail "expected dst/new directory to not be empty"
	pass
fi

if testcase "match header escape slash"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	Subject: foo/bar

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "Subject" /foo\/bar/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	ls "${MAILDIR}/dst/new" | cmp -s - /dev/null && \
		fail "expected dst/new directory to not be empty"
	grep -q "Subject: foo/bar" ${MAILDIR}/dst/new/* || \
		fail "expected dst/new directory to not be empty"
	pass
fi

if testcase "match many headers"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	EOF
	mkmsg "${MAILDIR}/src/cur" <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header { "Cc" "To" } /user/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	grep -q "To: user@example.com" ${MAILDIR}/dst/new/* || \
		fail "expected dst/new directory to not be empty"
	ls "${MAILDIR}/src/cur" | cmp -s - /dev/null || \
		fail "expected src/cur directory to be empty"
	grep -q "To: user@example.com" ${MAILDIR}/dst/cur/* || \
		fail "expected dst/cur directory to not be empty"
	pass
fi

if testcase "match with empty message"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" </dev/null
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match body /Bob/ or header "From" /Bob/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null && \
		fail "expected src/new directory to not be empty"
	ls "${MAILDIR}/dst/new" | cmp -s - /dev/null || \
		fail "expected dst/new directory to be empty"
	pass
fi

if testcase "match many and conditions"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@true.com
	Cc: user@true.com
	Bcc: user@true.com

	EOF
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@false.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user/ and header "Cc" /user/ and \
			header "Bcc" /user/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	grep -q "To: user@false.com" ${MAILDIR}/src/new/* || \
		fail "expected src/new directory to not be empty"
	grep -q "To: user@true.com" ${MAILDIR}/dst/new/* || \
		fail "expected dst/new directory to not be empty"
	pass
fi

if testcase "match many or conditions"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	EOF
	mkmsg "${MAILDIR}/src/new" <<-EOF
	Cc: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user/ or header "Cc" /user/ \
			move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	grep -q "To: user@example.com" ${MAILDIR}/dst/new/* || \
		fail "expected dst/new directory to not be empty"
	grep -q "Cc: user@example.com" ${MAILDIR}/dst/new/* || \
		fail "expected dst/new directory to not be empty"
	pass
fi

if testcase "match many and/or conditions"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user1@example.com

	Hello!
	EOF
	mkmsg "${MAILDIR}/src/new" <<-EOF
	Cc: user2@example.com

	Hello!
	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match body /hello/i and \
				(header "To" /user1/ or header "Cc" /user2/) \
			move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	grep -q "To: user1@example.com" ${MAILDIR}/dst/new/* || \
		fail "expected dst/new directory to not be empty"
	grep -q "Cc: user2@example.com" ${MAILDIR}/dst/new/* || \
		fail "expected dst/new directory to not be empty"
	pass
fi

if testcase "match new"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	EOF
	mkmsg "${MAILDIR}/src/cur" <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user/ and new move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	grep -q "To: user@example.com" ${MAILDIR}/dst/new/* || \
		fail "expected dst/new directory to not be empty"
	ls "${MAILDIR}/src/cur" | cmp -s - /dev/null && \
		fail "expected src/cur directory to not be empty"
	grep -q "To: user@example.com" ${MAILDIR}/src/cur/* || \
		fail "expected src/cur directory to not be empty"
	pass
fi

if testcase "match new negate"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: new@example.com

	EOF
	mkmsg "${MAILDIR}/src/cur" <<-EOF
	To: cur@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match ! new move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null && \
		fail "expected src/new directory to not be empty"
	grep -q "To: new@example.com" ${MAILDIR}/src/new/* || \
		fail "expected src/new directory to not be empty"
	ls "${MAILDIR}/dst/cur" | cmp -s - /dev/null && \
		fail "expected dst/cur directory to not be empty"
	grep -q "To: cur@example.com" ${MAILDIR}/dst/cur/* || \
		fail "expected dst/cur directory to not be empty"
	pass
fi

if testcase "match negate binds to the innermost condition"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: admin@example.com

	Hello!
	EOF
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	Bye!
	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match ! body /hello/i or header "To" /user/ \
			move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	grep -q "To: admin@example.com" ${MAILDIR}/src/new/* || \
		fail "expected dst/new directory to not be empty"
	grep -q "To: user@example.com" ${MAILDIR}/dst/new/* || \
		fail "expected dst/new directory to not be empty"
	pass
fi

if testcase "match negate nested condition"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: admin@example.com

	Hello!
	EOF
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	Bye!
	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match !(body /hello/i or header "To" /admin/) \
			move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	grep -q "To: admin@example.com" ${MAILDIR}/src/new/* || \
		fail "expected src/new directory to not be empty"
	grep -q "To: user@example.com" ${MAILDIR}/dst/new/* || \
		fail "expected dst/new directory to not be empty"
	pass
fi

if testcase "header key comparison is case insensitive"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	to: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	grep -q "to: user@example.com" ${MAILDIR}/dst/new/* || \
		fail "expected dst/new directory to not be empty"
	pass
fi

if testcase "match case insensitive"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: UsEr@ExAmPlE.CoM

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user/i move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	grep -q "To: UsEr@ExAmPlE.CoM" ${MAILDIR}/dst/new/* || \
		fail "expected dst/new directory to not be empty"
	pass
fi

if testcase "message without blank line after headers"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com
	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	grep -q "To: user@example.com" ${MAILDIR}/dst/new/* || \
		fail "expected dst/new directory to not be empty"
	pass
fi

if testcase "uniq suffix is preserved"; then
	mkmd "${MAILDIR}/dst" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2," <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	find "${MAILDIR}/dst/new" -type f -name '*:2,' | cmp -s - /dev/null && \
		fail "expected dst/new directory to not be empty"
	pass
fi

if testcase "destination interpolation from header"; then
	mkmd "${MAILDIR}/user-example" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2," <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /(user)@([^\.]+).com/ move "${MAILDIR}/\1-\2"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	grep -q "To: user@example.com" ${MAILDIR}/user-example/new/* || \
		fail "expected example/new directory to not be empty"
	pass
fi

if testcase "destination interpolation from body"; then
	mkmd "${MAILDIR}/example" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2," <<-EOF
	To: user@example.com

	Hello example
	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match body /Hello (example)/ move "${MAILDIR}/\1"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	grep -q "To: user@example.com" ${MAILDIR}/example/new/* || \
		fail "expected example/new directory to not be empty"
	pass
fi

if testcase "destination interpolation first match is favored"; then
	mkmd "${MAILDIR}/first" "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2," <<-EOF
	To: first@last.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /(first)/ and header "To" /(last)/ \
			move "${MAILDIR}/\1"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	grep -q "To: first@last.com" ${MAILDIR}/first/new/* || \
		fail "expected first/new directory to not be empty"
	pass
fi

if testcase "destination interpolation out of bounds"; then
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2," <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /./ move "${MAILDIR}/\1"
	}
	EOF
	mdsort >$TMP2
	grep -q '\\1: invalid back-reference in destination' $TMP2 || \
		fail "expected back-reference to be invalid"
	pass
fi

if testcase "destination interpolation out of range"; then
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2," <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /./ move "${MAILDIR}/\99999999999999999999"
	}
	EOF
	mdsort >$TMP2
	grep -q '9: invalid back-reference in destination' $TMP2 || \
		fail "expected back-reference to be invalid"
	pass
fi

if testcase "destination interpolation negative"; then
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2," <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /./ move "${MAILDIR}/\-1"
	}
	EOF
	mdsort >$TMP2
	grep -q '\\-1/new: No such file or directory' $TMP2 || \
		fail "expected back-reference to be ignored"
	pass
fi

if testcase "destination interpolation non-decimal"; then
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2," <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /(user)/ move "${MAILDIR}/\0x1"
	}
	EOF
	mdsort >$TMP2
	grep -q 'userx1/new: No such file or directory' $TMP2 || \
		fail "expected back-reference to be ignored"
	pass
fi

if testcase "destination interpolation with none body/header"; then
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2," <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new move "${MAILDIR}/\1"
	}
	EOF
	mdsort >$TMP2
	grep -q '\\1: invalid back-reference in destination' $TMP2 || \
		fail "expected back-reference to be invalid"
	pass
fi

if testcase "destination interpolation with negate"; then
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2," <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match ! header "To" /(user)/ or new move "${MAILDIR}/\1"
	}
	EOF
	mdsort >$TMP2
	grep -q '\\1: invalid back-reference in destination' $TMP2 || \
		fail "expected back-reference to be invalid"
	pass
fi

if testcase "destination interpolation too long"; then
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2," <<-EOF
	To: user@$(randstr $PATH_MAX alnum).com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /(.+)/ move "${MAILDIR}/\1"
	}
	EOF
	mdsort >$TMP2
	grep -q '\\1: destination too long' $TMP2 || \
		fail "expected destination to be too long"
	pass
fi

if testcase -e "unknown option"; then
	mdsort -1 >$TMP2
	grep -q 'usage' $TMP2 || fail "expected usage output"
	pass
fi

if testcase -e "extraneous option"; then
	mdsort extraneous >$TMP2
	grep -q 'usage' $TMP2 || fail "expected usage output"
	pass
fi

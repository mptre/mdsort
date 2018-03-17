testcase "match header"
	mkmd "${MAILDIR}/dst"
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
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
	grep -Rq "To: user@example.com" "${MAILDIR}/dst/new" || \
		fail "expected dst/new directory to not be empty"
	ls "${MAILDIR}/src/cur" | cmp -s - /dev/null || \
		fail "expected src/cur directory to be empty"
	grep -Rq "To: user@example.com" "${MAILDIR}/dst/cur" || \
		fail "expected dst/cur directory to not be empty"
	pass

testcase "match many headers"
	mkmd "${MAILDIR}/dst"
	mkmd "${MAILDIR}/src"
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
	grep -Rq "To: user@example.com" "${MAILDIR}/dst/new" || \
		fail "expected dst/new directory to not be empty"
	ls "${MAILDIR}/src/cur" | cmp -s - /dev/null || \
		fail "expected src/cur directory to be empty"
	grep -Rq "To: user@example.com" "${MAILDIR}/dst/cur" || \
		fail "expected dst/cur directory to not be empty"
	pass

testcase "match many and conditions"
	mkmd "${MAILDIR}/dst"
	mkmd "${MAILDIR}/src"
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
	grep -Rq "To: user@false.com" "${MAILDIR}/src/new" || \
		fail "expected src/new directory to not be empty"
	grep -Rq "To: user@true.com" "${MAILDIR}/dst/new" || \
		fail "expected dst/new directory to not be empty"
	pass

testcase "match many or conditions"
	mkmd "${MAILDIR}/dst"
	mkmd "${MAILDIR}/src"
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
	grep -Rq "To: user@example.com" "${MAILDIR}/dst/new" || \
		fail "expected dst/new directory to not be empty"
	grep -Rq "Cc: user@example.com" "${MAILDIR}/dst/new" || \
		fail "expected dst/new directory to not be empty"
	pass

testcase "header key comparison is case insensitive"
	mkmd "${MAILDIR}/dst"
	mkmd "${MAILDIR}/src"
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
	grep -Rq "to: user@example.com" "${MAILDIR}/dst/new" || \
		fail "expected dst/new directory to not be empty"
	pass

testcase "match case insensitive"
	mkmd "${MAILDIR}/dst"
	mkmd "${MAILDIR}/src"
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
	grep -Rq "To: UsEr@ExAmPlE.CoM" "${MAILDIR}/dst/new" || \
		fail "expected dst/new directory to not be empty"
	pass

testcase "uniq suffix is preserved"
	mkmd "${MAILDIR}/dst"
	mkmd "${MAILDIR}/src"
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

testcase "destination interpolation"
	mkmd "${MAILDIR}/example"
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2," <<-EOF
		To: user@example.com

	EOF
	cat <<-EOF >$CONF
		maildir "${MAILDIR}/src" {
			match header "To" /user@([^\.]+).com/ \
				move "${MAILDIR}/\1"
		}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	grep -Rq "To: user@example.com" "${MAILDIR}/example/new" || \
		fail "expected example/new directory to not be empty"
	pass

testcase "destination interpolation out of bounds"
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2," <<-EOF
		To: user@example.com

	EOF
	cat <<-EOF >$CONF
		maildir "${MAILDIR}/src" {
			match header "To" /./ move "${MAILDIR}/\1"
		}
	EOF
	mdsort | grep -q '\\1: invalid back-reference in destination' || \
		fail "expected back-reference to be invalid"
	pass

testcase "destination interpolation out of range"
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2," <<-EOF
		To: user@example.com

	EOF
	cat <<-EOF >$CONF
		maildir "${MAILDIR}/src" {
			match header "To" /./ \
				move "${MAILDIR}/\99999999999999999999"
		}
	EOF
	mdsort | grep -q '9: invalid back-reference in destination' || \
		fail "expected back-reference to be invalid"
	pass

testcase "destination interpolation negative"
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2," <<-EOF
		To: user@example.com

	EOF
	cat <<-EOF >$CONF
		maildir "${MAILDIR}/src" {
			match header "To" /./ move "${MAILDIR}/\-1"
		}
	EOF
	mdsort | grep -q '\\-1/new: No such file or directory' || \
		fail "expected back-reference to be ignored"
	pass

testcase "destination interpolation non-decimal"
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2," <<-EOF
		To: user@example.com

	EOF
	cat <<-EOF >$CONF
		maildir "${MAILDIR}/src" {
			match header "To" /(user)/ move "${MAILDIR}/\0x1"
		}
	EOF
	mdsort | grep -q 'userx1/new: No such file or directory' || \
		fail "expected back-reference to be ignored"
	pass

testcase -e "destination interpolation too long"
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2," <<-EOF
		To: user@$(randstr 1024 alnum).com

	EOF
	cat <<-EOF >$CONF
		maildir "${MAILDIR}/src" {
			match header "To" /(.+)/ move "${MAILDIR}/\1"
		}
	EOF
	mdsort | grep -q '\\1: destination too long' || \
		fail "expected destination to be too long"
	pass

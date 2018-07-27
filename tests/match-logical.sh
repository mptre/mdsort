if testcase "many and conditions"; then
	mkmsg "src/new" -- \
		"To" "user@true.com" "Cc" "user@true.com" "Bcc" "user@true.com"
	mkmsg "src/new" -- "To" "user@false.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user/ and header "Cc" /user/ and \
			header "Bcc" /user/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	refute_empty "dst/new"
	pass
fi

if testcase "many or conditions"; then
	mkmsg "src/new" -- "To" "user@example.com"
	mkmsg "src/new" -- "Cc" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user/ or header "Cc" /user/ \
			move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	pass
fi

if testcase "many and/or conditions"; then
	echo "Hello!" | mkmsg "src/new" - -- "To" "user1@example.com"
	echo "Hello!" | mkmsg "src/new" - -- "Cc" "user2@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match body /hello/i and \
				(header "To" /user1/ or header "Cc" /user2/) \
			move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	pass
fi

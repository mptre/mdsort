if testcase "many and conditions"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- \
		"To" "user@true.com" "Cc" "user@true.com" "Bcc" "user@true.com"
	mkmsg "src/new" -- "To" "user@false.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /user/ and header "Cc" /user/ and \
			header "Bcc" /user/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "many or conditions"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "user@example.com"
	mkmsg "src/new" -- "Cc" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /user/ or header "Cc" /user/ \
			move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "many and/or conditions"; then
	mkmd "src" "dst"
	echo "Hello!" | mkmsg -b "src/new" -- "To" "user1@example.com"
	echo "Hello!" | mkmsg -b "src/new" -- "Cc" "user2@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /hello/i and \
				(header "To" /user1/ or header "Cc" /user2/) \
			move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

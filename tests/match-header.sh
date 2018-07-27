if testcase "basic"; then
	mkmsg "src/new" -- "To" "user@example.com"
	mkmsg "src/cur" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user@example.com/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	assert_empty "src/cur"
	refute_empty "dst/new"
	refute_empty "dst/cur"
	pass
fi

if testcase "negate"; then
	mkmsg "src/new" -- "To" "admin@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match ! header "To" /user/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	pass
fi

if testcase "line continuation"; then
	mkmsg "src/new" - <<EOF
Subject: foo
	bar

EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "Subject" /foobar/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	pass
fi

if testcase "many headers"; then
	mkmsg "src/new" -- "To" "user@example.com"
	mkmsg "src/cur" -- "Cc" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header { "Cc" "To" } /user/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	assert_empty "src/cur"
	refute_empty "dst/new"
	refute_empty "dst/cur"
	pass
fi

if testcase "no blank line after headers"; then
	echo "To: user@example.com" | mkmsg "src/new" -
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	pass
fi

if testcase "key comparison is case insensitive"; then
	mkmsg "src/new" -- "to" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	pass
fi

if testcase "destination interpolation"; then
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /(user)@([^\.]+).com/ move "${MAILDIR}/\1-\2"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "user-example/new"
	pass
fi

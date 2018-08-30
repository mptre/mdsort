if testcase "basic"; then
	mkmsg "src/new" -- "To" "user@example.com"
	mkmsg "src/cur" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /user@example.com/ move "dst"
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
	maildir "src" {
		match ! header "To" /user/ move "dst"
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
	maildir "src" {
		match header "Subject" /foobar/ move "dst"
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
	maildir "src" {
		match header { "Cc" "To" } /user/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	assert_empty "src/cur"
	refute_empty "dst/new"
	refute_empty "dst/cur"
	pass
fi

if testcase "duplicate headers"; then
	mkmsg "src/new" -- "To" "user@example.com" "To" "foo@example.com" \
		"To" "bar@example.com"
	mkmsg "src/new" -- "To" "foo@example.com" "To" "admin@example.com" \
		"To" "bar@example.com"
	mkmsg "src/new" -- "To" "foo@example.com" "To" "bar@example.com" \
		"To" "root@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /user/ move "user"
		match header "To" /admin/ move "admin"
		match header "To" /root/ move "root"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "user/new"
	refute_empty "admin/new"
	refute_empty "root/new"
	pass
fi

if testcase "no blank line after headers"; then
	echo "To: user@example.com" | mkmsg "src/new" -
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /user/ move "dst"
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
	maildir "src" {
		match header "To" /user/ move "dst"
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
	maildir "src" {
		match header "To" /(user)@([^\.]+).com/ move "\1-\2"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "user-example/new"
	pass
fi

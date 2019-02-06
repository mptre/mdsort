if testcase "basic"; then
	mkmd "src" "dst"
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
fi

if testcase "negate"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "admin@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match ! header "To" /user/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "line continuation"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "Subject" "$(printf 'foo\n\tbar')"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "Subject" /foobar/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "many headers"; then
	mkmd "src" "dst"
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
fi

if testcase "duplicate headers"; then
	mkmd "src" "user" "admin" "root"
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
fi

if testcase "key comparison is case insensitive"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "to" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /user/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "destination interpolation"; then
	mkmd "src" "user-example"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /(user)@([^\.]+).com/ move "\1-\2"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "user-example/new"
fi

if testcase "dry run first line"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /example.com/ move "dst"
	}
	EOF
	cat <<EOF >$TMP1
mdsort.conf:2: To: user@example.com
                        ^         $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	assert_file $TMP1 $TMP2
fi

if testcase "dry run middle line"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" \
		"$(printf 'admin@a.com,\n\tuser@a.com,\n\tno-reply@a.com')"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /user/ move "dst"
	}
	EOF
	cat <<EOF >$TMP1
mdsort.conf:2: To: admin@a.com,user@a.com,no-reply@a.com
                               ^  $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	assert_file $TMP1 $TMP2
fi

if testcase "dry run last line"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" \
		"$(printf 'admin@example.com,\n\tuser@example.com')"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /user/ move "dst"
	}
	EOF
	cat <<EOF >$TMP1
mdsort.conf:2: To: admin@example.com,user@example.com
                                     ^  $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	assert_file $TMP1 $TMP2
fi

if testcase "dry run negate"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "admin@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match ! header "To" /user/ move "dst"
	}
	EOF
	mdsort -- -d >$TMP1
	grep -q '^src/new.* -> dst/new$' $TMP1 ||
		fail - "expected move line" <$TMP1
fi

if testcase "dry run many subexpressions"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /(example).(com)/ move "dst"
	}
	EOF
	cat <<EOF >$TMP1
mdsort.conf:2: To: user@example.com
                        ^         $
                   user@example.com
                        ^     $
                   user@example.com
                                ^ $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	assert_file $TMP1 $TMP2
fi

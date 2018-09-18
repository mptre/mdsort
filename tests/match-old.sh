if testcase "old"; then
	mkmsg "src/cur"
	cat <<-EOF >$CONF
	maildir "src" {
		match old move "dst"
	}
	EOF
	mdsort
	assert_empty "src/cur"
	refute_empty "dst/cur"
	pass
fi

if testcase "negate"; then
	mkmsg "src/cur"
	cat <<-EOF >$CONF
	maildir "src" {
		match ! old move "dst"
	}
	EOF
	mdsort
	refute_empty "src/cur"
	assert_empty "dst/cur"
	pass
fi

if testcase "seen flag present"; then
	mkmsg -s ":2,S" "src/cur"
	cat <<-EOF >$CONF
	maildir "src" {
		match old move "dst"
	}
	EOF
	mdsort
	refute_empty "src/cur"
	assert_empty "dst/cur"
	pass
fi

if testcase "invalid flags"; then
	mkmsg -s ":1," "src/cur"
	cat <<-EOF >$CONF
	maildir "src" {
		match old move "dst"
	}
	EOF
	mdsort
	refute_empty "src/cur"
	assert_find "src/cur" "*:1,"
	assert_empty "dst/cur"
	pass
fi

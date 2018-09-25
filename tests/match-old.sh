if testcase "old"; then
	mkmd "src" "dst"
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
	mkmd "src" "dst"
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
	mkmd "src" "dst"
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
	mkmd "src" "dst"
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

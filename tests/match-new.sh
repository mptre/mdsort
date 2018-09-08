if testcase "basic"; then
	mkmsg "src/new"
	mkmsg "src/cur"
	cat <<-EOF >$CONF
	maildir "src" {
		match new move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	refute_empty "dst/new"
	assert_empty "dst/cur"
	pass
fi

if testcase "negate"; then
	mkmsg "src/new"
	mkmsg "src/cur"
	cat <<-EOF >$CONF
	maildir "src" {
		match ! new move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_empty "src/cur"
	assert_empty "dst/new"
	refute_empty "dst/cur"
	pass
fi

if testcase "flag as new"; then
	mkmsg "src/cur"
	cat <<-EOF >$CONF
	maildir "src" {
		match !new flag new
	}
	EOF
	mdsort
	assert_empty "src/cur"
	refute_empty "src/new"
	pass
fi

if testcase "flag as not new"; then
	mkmsg "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match new flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	pass
fi

if testcase "flag and move"; then
	mkmsg "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match new move "dst" flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/cur"
	pass
fi

if testcase "move and flag"; then
	mkmsg "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match new flag !new move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/cur"
	pass
fi

if testcase "flag as not new when path flags are missing"; then
	mkmsg "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match new flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	find "src/cur" -type f -name '*:2,S' | cmp -s - /dev/null && \
		fail "expected flags to be present"
	pass
fi

if testcase "flag as not new when path flags are invalid"; then
	mkmsg  -s ":1,S" "src/new"
	mkmsg  -s ":2,s" "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match new flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	find "src/cur" -type f -name '*:1,S' | cmp -s - /dev/null && \
		fail "expected flags to be present"
	find "src/cur" -type f -name '*:2,s' | cmp -s - /dev/null && \
		fail "expected flags to be present"
	pass
fi

if testcase "flag as not new when path flags are already present"; then
	mkmsg -s ":2,S" "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match new flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	find "src/cur" -type f -name '*:2,S' | cmp -s - /dev/null && \
		fail "expected flags to be present"
	pass
fi

if testcase "flag as not new when path flags are valid"; then
	mkmsg -s ":2,R" "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match new flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	find "src/cur" -type f -name '*:2,RS' | cmp -s - /dev/null && \
		fail "expected flags to be present"
	pass
fi

if testcase "flag as new when seen flag is already present"; then
	mkmsg -s ":2,S" "src/cur"
	cat <<-EOF >$CONF
	maildir "src" {
		match all flag new
	}
	EOF
	mdsort
	assert_empty "src/cur"
	refute_empty "src/new"
	find "src/new" -type f -name '*:2,S' | cmp -s - /dev/null || \
		fail "expected flags to not be present"
	pass
fi

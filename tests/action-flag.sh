if testcase "flag as new"; then
	mkmd "src"
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
	mkmd "src"
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
	mkmd "src" "dst"
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
	mkmd "src" "dst"
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
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match new flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	assert_find "src/cur" "*:2,S"
	pass
fi

if testcase "flag as not new when path flags are invalid"; then
	mkmd "src"
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
	assert_find "src/cur" "*:1,S"
	assert_find "src/cur" "*:2,s"
	pass
fi

if testcase "flag as not new when path flags are already present"; then
	mkmd "src"
	mkmsg -s ":2,S" "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match new flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	assert_find "src/cur" "*:2,S"
	pass
fi

if testcase "flag as not new when path flags are valid"; then
	mkmd "src"
	mkmsg -s ":2,R" "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match new flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	assert_find "src/cur" "*:2,RS"
	pass
fi

if testcase "flag as new when seen flag is already present"; then
	mkmd "src"
	mkmsg -s ":2,S" "src/cur"
	cat <<-EOF >$CONF
	maildir "src" {
		match all flag new
	}
	EOF
	mdsort
	assert_empty "src/cur"
	refute_empty "src/new"
	refute_find "src/new" "*2,S"
	pass
fi

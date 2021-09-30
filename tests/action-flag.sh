if testcase "flag as new"; then
	mkmd "src"
	mkmsg "src/cur"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match !new flag new
	}
	EOF
	mdsort
	assert_empty "src/cur"
	refute_empty "src/new"
fi

if testcase "flag as not new"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match new flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
fi

if testcase "flag and move"; then
	mkmd "src" "dst"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match new move "dst" flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/cur"
fi

if testcase "move and flag"; then
	mkmd "src" "dst"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match new flag !new move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/cur"
fi

if testcase "flag as not new when path flags are missing"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match new flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	assert_find "src/cur" "*:2,S"
fi

if testcase "flag as not new when path flags are lowercase"; then
	mkmd "src"
	mkmsg -s ":2,s" "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match new flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	assert_find "src/cur" "*:2,Ss"
fi

if testcase "flag as not new when path flags are invalid"; then
	mkmd "src"
	mkmsg  -s ":1,S" "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match new flag !new
	}
	EOF
	mdsort -e >/dev/null
	assert_empty "src/cur"
	refute_empty "src/new"
	assert_find "src/new" "*:1,S"
fi

if testcase "flag as not new when path flags are already present"; then
	mkmd "src"
	mkmsg -s ":2,S" "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match new flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	assert_find "src/cur" "*:2,S"
fi

if testcase "flag as not new when path flags are valid"; then
	mkmd "src"
	mkmsg -s ":2,R" "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match new flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	assert_find "src/cur" "*:2,RS"
fi

if testcase "flag as new when seen flag is already present"; then
	mkmd "src"
	mkmsg -s ":2,S" "src/cur"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all flag new
	}
	EOF
	mdsort
	assert_empty "src/cur"
	refute_empty "src/new"
	refute_find "src/new" "*2,S"
fi

# Try to make new messages appear in the maildir currently being traversed.
if testcase "flag as new when already new"; then
	mkmd "src"
	mkmsg "src/new"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all flag new
	}
	EOF
	mdsort
	assert_empty "src/cur"
	refute_empty "src/new"
fi

if testcase "redundant flag actions"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all flag new flag !new flag new
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

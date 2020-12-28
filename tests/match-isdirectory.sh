if testcase "basic"; then
	mkmd "src" "dst"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match isdirectory "src" move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "not a directory"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match isdirectory "$(findmsg "src/new")" move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "directory not found"; then
	_dir="/$(genstr 16)"
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match isdirectory "$_dir" move "dst"
	}
	EOF
	mdsort -e - -- <<-EOF
	mdsort: stat: ${_dir}: No such file or directory
	EOF
	refute_empty "src/new"
fi

if testcase "interpolation"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "dst"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "To" /.*/ and isdirectory "\\0" move "\\0"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "interpolation out of bounds"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "dst"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match isdirectory "\\0" move "\\0"
	}
	EOF
	mdsort -e - <<-EOF
	mdsort: \\0: invalid back-reference
	EOF
	refute_empty "src/new"
fi

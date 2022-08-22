if testcase "flags"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all flags "ABC"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_find "src/new" "*:2,ABC"
fi

if testcase "invalid flags"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all flags "0A1"
	}
	EOF
	mdsort -e - <<-EOF
	mdsort: 0: unknown flag
	mdsort: 1: unknown flag
	EOF
fi

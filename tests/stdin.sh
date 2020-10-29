if testcase "stdin may only be defined once"; then
	cat <<-EOF >$CONF
	stdin {
		match all move "dst"
	}

	stdin {
		match all move "dst"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:5: stdin already defined
	EOF
fi

if testcase "maildir rules are skipped"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match all move "dst"
	}
	EOF
	mdsort -- - </dev/null
	refute_empty "src/new"
fi

if testcase "stdin rule is skipped"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >$CONF
	stdin {
		match all move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "move"; then
	mkmd "dst"
	cat <<-EOF >$CONF
	stdin {
		match all move "dst"
	}
	EOF
	mdsort -- - </dev/null
	refute_empty "dst/new"
fi

if testcase "discard"; then
	cat <<-EOF >$CONF
	stdin {
		match all discard
	}
	EOF
	mdsort -- - </dev/null
fi

if testcase "match date modified"; then
	cat <<-EOF >$CONF
	stdin {
		match date modified > 30 seconds move "dst"
	}
	EOF
	mdsort -- - </dev/null
fi

if testcase "temporary failure"; then
	cat <<-EOF >$CONF
	stdin {
		match all move "dst"
	}
	EOF
	mdsort -t -- - </dev/null >/dev/null
fi

if testcase -t memleak "temporary config failure"; then
	cat <<-EOF >$CONF
	invalid
	EOF
	mdsort -t -- - </dev/null >/dev/null
fi

if testcase "dry run"; then
	cat <<-EOF >$CONF
	stdin {
		match all move "dst"
	}
	EOF
	mdsort -- -d - </dev/null >"$TMP1"
	assert_file - $TMP1 <<-EOF
	<stdin> -> dst/new
	EOF
fi

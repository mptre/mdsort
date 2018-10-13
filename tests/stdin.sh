if testcase -e "stdin may only be defined once"; then
	cat <<-EOF >$CONF
	stdin {
		match all move "dst"
	}

	stdin {
		match all move "dst"
	}
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:5: stdin already defined
	EOF
	pass
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
	refute_empty "src"
	pass
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
	refute_empty "src"
	pass
fi

if testcase "move"; then
	mkmd "dst"
	cat <<-EOF >$CONF
	stdin {
		match all move "dst"
	}
	EOF
	mdsort -- - </dev/null
	refute_empty "dst"
	pass
fi

if testcase "discard"; then
	cat <<-EOF >$CONF
	stdin {
		match all discard
	}
	EOF
	mdsort -- - </dev/null
	pass
fi

if testcase "dry run"; then
	cat <<-EOF >$CONF
	stdin {
		match all move "dst"
	}
	EOF
	cat <<-EOF >$TMP1
	<stdin> -> dst/new
	EOF
	mdsort -- -d - </dev/null >$TMP2
	fcmp $TMP1 $TMP2
	pass
fi

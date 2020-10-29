if testcase "reject is mutually exclusive with all other actions"; then
	cat <<-EOF >$CONF
	stdin {
		match all move "foo" reject
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: reject cannot be combined with another action
	EOF
fi

if testcase "reject cannot be used outside stdin"; then
	cat <<-EOF >$CONF
	maildir "src" {
		match all reject
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:3: reject cannot be used outside stdin
	EOF
fi

if testcase "reject"; then
	cat <<-EOF >$CONF
	stdin {
		match all reject
	}
	EOF
	mdsort -e -- - </dev/null
fi

if testcase "dry run"; then
	cat <<-EOF >$CONF
	stdin {
		match all reject
	}
	EOF
	mdsort - -- -d - <<EOF
<stdin> -> <reject>
EOF
fi

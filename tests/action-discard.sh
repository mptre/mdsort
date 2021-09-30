if testcase "discard is mutually exclusive with all other actions"; then
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all move "dst" discard
		match all flag new discard
		match all discard discard
		match all break discard
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: discard cannot be combined with another action
	mdsort.conf:3: discard cannot be combined with another action
	mdsort.conf:4: discard cannot be combined with another action
	mdsort.conf:5: break cannot be combined with another action
	mdsort.conf:5: discard cannot be combined with another action
	EOF
fi

if testcase "discard"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all discard
	}
	EOF
	mdsort
	assert_empty "src/new"
fi

if testcase "dry run"; then
	mkmd "src"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "To" /user/ discard
	}
	EOF
	mdsort - -- -d <<EOF
$(findmsg "src/new") -> <discard>
mdsort.conf:2: To: user@example.com
                   ^  $
EOF
fi

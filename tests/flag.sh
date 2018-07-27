if testcase "flag as new"; then
	mkmsg "src/cur" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match !new flag new
	}
	EOF
	mdsort
	assert_empty "src/cur"
	refute_empty "src/new"
	pass
fi

if testcase "flag as not new"; then
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	pass
fi

if testcase "flag and move"; then
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new move "${MAILDIR}/dst" flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/cur"
	pass
fi

if testcase "move and flag"; then
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new flag !new move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/cur"
	pass
fi

if testcase "flag as not new when path flags are missing"; then
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	find "${MAILDIR}/src/cur" -type f -name '*:2,S' | cmp -s - /dev/null && \
		fail "expected flags to be present"
	pass
fi

if testcase "flag as not new when path flags are invalid"; then
	mkmsg "src/new" -s ":1,S" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	find "${MAILDIR}/src/cur" -type f -name '*:1,S' | cmp -s - /dev/null && \
		fail "expected flags to be present"
	pass
fi

if testcase "flag as not new when path flags are already present"; then
	mkmsg "src/new" -s ":2,S" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	find "${MAILDIR}/src/cur" -type f -name '*:2,S' | cmp -s - /dev/null && \
		fail "expected flags to be present"
	pass
fi

if testcase "flag as not new when path flags are valid"; then
	mkmsg "src/new" -s ":2,R" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	find "${MAILDIR}/src/cur" -type f -name '*:2,RS' | cmp -s - /dev/null && \
		fail "expected flags to be present"
	pass
fi

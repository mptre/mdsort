if testcase "flag as new"; then
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/cur" <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match !new flag new
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/cur" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null && \
		fail "expected src/new directory to not be empty"
	pass
fi

if testcase "flag as not new"; then
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new flag !new
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	ls "${MAILDIR}/src/cur" | cmp -s - /dev/null && \
		fail "expected src/cur directory to not be empty"
	pass
fi

if testcase "flag and move"; then
	mkmd "${MAILDIR}/src" "${MAILDIR}/dst"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new move "${MAILDIR}/dst" flag !new
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	ls "${MAILDIR}/dst/cur" | cmp -s - /dev/null && \
		fail "expected dst/cur directory to not be empty"
	pass
fi

if testcase "move and flag"; then
	mkmd "${MAILDIR}/src" "${MAILDIR}/dst"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new flag !new move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	ls "${MAILDIR}/dst/cur" | cmp -s - /dev/null && \
		fail "expected dst/cur directory to not be empty"
	pass
fi

if testcase "flag as not new when path flags missing"; then
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new flag !new
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	find "${MAILDIR}/src/cur" -type f -name '*:2,S' | cmp -s - /dev/null && \
		fail "expected src/cur directory to not be empty"
	pass
fi

if testcase "flag as not new when path flags invalid"; then
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":1,S" <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new flag !new
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	find "${MAILDIR}/src/cur" -type f -name '*:1,S' | cmp -s - /dev/null && \
		fail "expected src/cur directory to not be empty"
	pass
fi

if testcase "flag as not new when path flags already present"; then
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2,S" <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new flag !new
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	find "${MAILDIR}/src/cur" -type f -name '*:2,S' | cmp -s - /dev/null && \
		fail "expected src/cur directory to not be empty"
	pass
fi

if testcase "flag as not new when path flags valid"; then
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" ":2,R" <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new flag !new
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	find "${MAILDIR}/src/cur" -type f -name '*:2,RS' | cmp -s - /dev/null && \
		fail "expected src/cur directory to not be empty"
	pass
fi

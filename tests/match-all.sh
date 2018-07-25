if testcase "all matches any message"; then
	mkmd "${MAILDIR}/src"
	mkmsg "${MAILDIR}/src/new" <<-EOF
	To: user@example.com

	EOF
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new {
			match header "To" /admin/ move "${MAILDIR}/admin"

			match all move "${MAILDIR}/dst"
		}
	}
	EOF
	mdsort
	ls "${MAILDIR}/src/new" | cmp -s - /dev/null || \
		fail "expected src/new directory to be empty"
	ls "${MAILDIR}/dst" | cmp -s - /dev/null && \
		fail "expected dst/new directory to not be empty"
	pass
fi

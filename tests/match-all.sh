if testcase "all matches any message"; then
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new {
			match header "To" /admin/ move "${MAILDIR}/admin"

			match all move "${MAILDIR}/dst"
		}
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	pass
fi

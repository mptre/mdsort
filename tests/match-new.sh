if testcase "basic"; then
	mkmsg "src/new" -- "To" "user@example.com"
	mkmsg "src/cur" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	refute_empty "dst/new"
	assert_empty "dst/cur"
	pass
fi

if testcase "negate"; then
	mkmsg "src/new" -- "To" "new@example.com"
	mkmsg "src/cur" -- "To" "cur@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match ! new move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_empty "src/cur"
	assert_empty "dst/new"
	refute_empty "dst/cur"
	pass
fi

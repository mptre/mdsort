if testcase "all matches any message"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match new {
			match header "To" /admin/ move "admin"

			match all move "dst"
		}
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "pass before no match negation"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >"${CONF}"
	maildir "src" {
		match all move "dst" pass
		match ! header "To" /user@example.com/ move "nein"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

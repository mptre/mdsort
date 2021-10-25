if testcase "basic"; then
	mkmd "src" "dst"
	mkmsg "src/cur"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all flag new pass
		match all move "dst"
	}
	EOF
	mdsort
	assert_empty "src/cur"
	refute_empty "dst/new"
fi

if testcase "last match"; then
	mkmd "src"
	mkmsg "src/cur"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all flag new pass
		match header "Subject" /nein/ move "junk"
	}
	EOF
	mdsort
	assert_empty "src/cur"
	refute_empty "src/new"
fi

if testcase "nested block"; then
	mkmd "src" "dst"
	mkmsg "src/cur"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all {
			match all flag new pass
			match all move "dst"
		}
		match all move "fallback"
	}
	EOF
	mdsort
	assert_empty "src/cur"
	refute_empty "dst/new"
fi

if testcase "dangling"; then
	mkmd "src"
	mkmsg "src/cur" -- "To" "user@example.com"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "To" /user/ pass
		match all {
			match header "To" /admin/ move "dst"
		}
	}
	EOF
	mdsort
	refute_empty "src/cur"
fi

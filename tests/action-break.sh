if testcase "root level"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "user@example.com"
	mkmsg "src/new" -- "To" "admin@example.com"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "To" /user@example.com/ break

		match ! new move "fail"

		match all move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "nested level"; then
	mkmd "src" "d0" "d3"
	mkmsg "src/new" -- "To" "user@example.com"
	mkmsg "src/new" -- "To" "admin@example.com"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match new {
			match new {
				match new {
					match header "To" /user/ break
					match all move "d3"
				}
				match header "To" /user/ break
				match all move "d2"
			}
			match header "To" /user/ break
			match all move "d1"
		}
		match all move "d0"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "d0/new"
	refute_empty "d3/new"
fi

if testcase "move, break and move"; then
	mkmd "src" "dst1" "dst2"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all {
			match all move "dst1" break
		}
		match all move "dst2"
	}
	EOF
	mdsort
	assert_empty "src/new"
	assert_empty "dst1/new"
	refute_empty "dst2/new"
fi

if testcase "label, pass and break"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all label "pass" pass
		match all {
			match all break
		}
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_label pass "$(findmsg "src/new")"
fi

if testcase "label, pass, label, break and move"; then
	mkmd "src" "dst"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all label "one" pass
		match all {
			match all label "two" break
		}
		match all move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	assert_label "one two" "$(findmsg "dst/new")"
fi

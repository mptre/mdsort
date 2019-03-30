if testcase "break is mutually exclusive with all other actions"; then
	cat <<-EOF >$CONF
	maildir "src" {
		match all move "dst" break
		match all flag new break
		match all discard break
		match all break break
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: break cannot be combined with another action
	mdsort.conf:3: break cannot be combined with another action
	mdsort.conf:4: break cannot be combined with another action
	mdsort.conf:4: discard cannot be combined with another action
	mdsort.conf:5: break cannot be combined with another action
	EOF
fi

if testcase "root level"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "user@example.com"
	mkmsg "src/new" -- "To" "admin@example.com"
	cat <<-EOF >$CONF
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
	cat <<-EOF >$CONF
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

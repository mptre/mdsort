if testcase -e "pass is mutually exclusive with all other actions"; then
	cat <<-EOF >$CONF
	maildir "src" {
		match all move "dst" pass
		match all flag new pass
		match all discard pass
		match all pass pass
	}
	EOF
	mdsort - -n <<-EOF
	mdsort.conf:2: pass cannot be combined with another action
	mdsort.conf:3: pass cannot be combined with another action
	mdsort.conf:4: discard cannot be combined with another action
	mdsort.conf:4: pass cannot be combined with another action
	mdsort.conf:5: pass cannot be combined with another action
	EOF
	pass
fi

if testcase "root level"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "user@example.com"
	mkmsg "src/new" -- "To" "admin@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /user@example.com/ pass

		match ! new move "fail"

		match all move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	refute_empty "dst/new"
	pass
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
					match header "To" /user/ pass
					match all move "d3"
				}
				match header "To" /user/ pass
				match all move "d2"
			}
			match header "To" /user/ pass
			match all move "d1"
		}
		match all move "d0"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "d0/new"
	refute_empty "d3/new"
	pass
fi

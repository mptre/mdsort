if testcase "tilde expansion"; then
	echo "Hello Bob" | mkmsg "src/new" - -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "~/src" {
		match body /Bob/ move "${MAILDIR}/dst"
	}
	EOF
	HOME=$MAILDIR mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	pass
fi

if testcase "escape slash in pattern"; then
	mkmsg "src/new" -- "Subject" "foo/bar"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "Subject" /foo\/bar/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	pass
fi

if testcase "match negate binds to the innermost condition"; then
	echo "Hello!" | mkmsg "src/new" - -- "To" "admin@example.com"
	echo "Bye!" | mkmsg "src/new" - -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match ! body /hello/i or header "To" /user/ \
			move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	refute_empty "dst/new"
	pass
fi

if testcase "match negate nested condition"; then
	echo "Hello!" | mkmsg "src/new" - -- "To" "admin@example.com"
	echo "Bye!" | mkmsg "src/new" - -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match !(body /hello/i or header "To" /admin/) \
			move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	refute_empty "dst/new"
	pass
fi

if testcase "match nested"; then
	echo "Hello!" | mkmsg "src/new" - -- "To" "user@example.com"
	echo "Bye!" | mkmsg "src/new" - -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user/ {
			match new {
				match body /Hello/ move "${MAILDIR}/dst"
			}
		}
	}
	EOF
	mdsort
	refute_empty "src/new"
	refute_empty "dst/new"
	pass
fi

if testcase "match nested without interpolation matches"; then
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new {
			match new move "${MAILDIR}/dst"
		}
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	pass
fi

if testcase "match nested with negate"; then
	mkmsg "src/new" -- "To" "user@example.com"
	mkmsg "src/new" -- "To" "admin@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new {
			match ! header "To" /admin/ move "${MAILDIR}/dst"
		}

		match ! header "To" /user/ {
			match new move "${MAILDIR}/dst"
		}
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	pass
fi

if testcase "match case insensitive"; then
	mkmsg "src/new" -- "To" "UsEr@ExAmPlE.CoM"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user/i move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	pass
fi

if testcase "unique suffix is preserved when valid"; then
	mkmsg "src/new" -s ":2,S" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	find "${MAILDIR}/dst/new" -type f -name '*:2,S' | cmp -s - /dev/null && \
		fail "expected suffix to be preserved"
	pass
fi

if testcase "unique suffix is preserved when invalid"; then
	mkmsg "src/new" -s ":1,S" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /user/ move "${MAILDIR}/dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	find "${MAILDIR}/dst/new" -type f -name '*:1,S' | cmp -s - /dev/null && \
		fail "expected suffix to be preserved"
	pass
fi

if testcase "destination interpolation first match is favored"; then
	mkmsg "src/new" -- "To" "first@last.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /(first)/ and header "To" /(last)/ \
			move "${MAILDIR}/\1"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "first/new"
	pass
fi

if testcase "destination interpolation out of bounds"; then
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /./ move "${MAILDIR}/\1"
	}
	EOF
	mdsort >$TMP1
	grep -q '\\1/new: invalid back-reference in destination' $TMP1 || \
		fail "expected back-reference to be invalid"
	pass
fi

if testcase "destination interpolation out of range"; then
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /./ move "${MAILDIR}/\99999999999999999999"
	}
	EOF
	mdsort >$TMP1
	grep -q '9/new: invalid back-reference in destination' $TMP1 || \
		fail "expected back-reference to be invalid"
	pass
fi

if testcase "destination interpolation negative"; then
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /./ move "${MAILDIR}/\-1"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "\-1/new"
	pass
fi

if testcase "destination interpolation non-decimal"; then
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /(user)/ move "${MAILDIR}/\0x1"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "userx1/new"
	pass
fi

if testcase "destination interpolation with none body/header"; then
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new move "${MAILDIR}/\1"
	}
	EOF
	mdsort >$TMP1
	grep -q '\\1/new: invalid back-reference in destination' $TMP1 || \
		fail "expected back-reference to be invalid"
	pass
fi

if testcase "destination interpolation with negate"; then
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match ! header "To" /(user)/ or new move "${MAILDIR}/\1"
	}
	EOF
	mdsort >$TMP1
	grep -q '\\1/new: invalid back-reference in destination' $TMP1 || \
		fail "expected back-reference to be invalid"
	pass
fi

if testcase "destination interpolation too long"; then
	mkmsg "src/new" -- "To" "user@$(randstr $PATH_MAX alnum).com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /(.+)/ move "${MAILDIR}/\1"
	}
	EOF
	mdsort >$TMP1
	grep -q '\\1/new: destination too long' $TMP1 || \
		fail "expected destination to be too long"
	pass
fi

if testcase -e "unknown option"; then
	mdsort -1 >$TMP1
	grep -q 'usage' $TMP1 || fail "expected usage output"
	pass
fi

if testcase -e "extraneous option"; then
	mdsort extraneous >$TMP1
	grep -q 'usage' $TMP1 || fail "expected usage output"
	pass
fi

if testcase "create maildir"; then
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new move "${MAILDIR}/dst"
	}
	EOF
	[ -d "${MAILDIR}/src/cur" ] && fail "expected cur to be missing"
	[ -d "${MAILDIR}/src/new" ] && fail "expected new to be missing"
	[ -d "${MAILDIR}/src/tmp" ] && fail "expected tmp to be missing"
	mdsort
	[ -d "${MAILDIR}/src/cur" ] || fail "expected cur to be created"
	[ -d "${MAILDIR}/src/new" ] || fail "expected new to be created"
	[ -d "${MAILDIR}/src/tmp" ] || fail "expected tmp to be created"
	pass
fi

if testcase "create maildir subdirectory"; then
	mkdir -p "${MAILDIR}/src/cur"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new move "${MAILDIR}/dst"
	}
	EOF
	[ -d "${MAILDIR}/src/cur" ] || fail "expected cur to be present"
	[ -d "${MAILDIR}/src/new" ] && fail "expected new to be missing"
	[ -d "${MAILDIR}/src/tmp" ] && fail "expected tmp to be missing"
	mdsort
	[ -d "${MAILDIR}/src/new" ] || fail "expected new to be created"
	[ -d "${MAILDIR}/src/tmp" ] || fail "expected tmp to be created"
	pass
fi

if testcase "create maildir with destination interpolation"; then
	mkmsg "src/new" -- "To" "missing@example.com"
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match header "To" /([^@]+)/ move "${MAILDIR}/\1"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "missing/new"
	for d in cur new tmp; do
		[ -d "${MAILDIR}/missing/${d}" ] || \
			fail "expected missing/${d} to be created"
	done
	pass
fi

if testcase "do not create maildir"; then
	cat <<-EOF >$CONF
	maildir "${MAILDIR}/src" {
		match new move "${MAILDIR}/dst"
	}
	EOF
	mdsort -C >$TMP1
	grep -q "opendir: ${MAILDIR}/src/new" $TMP1 || \
		fail "expected opendir error output"
	pass
fi

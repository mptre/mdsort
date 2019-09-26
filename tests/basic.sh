if testcase "tilde expansion"; then
	mkmd "src" "dst"
	echo "Hello Bob" | mkmsg -b "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "~/src" {
		match body /Bob/ move "~/dst"
	}
	EOF
	HOME=$TSHDIR mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "escape slash in pattern"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "Subject" "foo/bar"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "Subject" /foo\/bar/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "match negate binds to the innermost condition"; then
	mkmd "src" "dst"
	echo "Hello!" | mkmsg -b "src/new" -- "To" "admin@example.com"
	echo "Bye!" | mkmsg -b "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match ! body /hello/i or header "To" /user/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "match negate nested condition"; then
	mkmd "src" "dst"
	echo "Hello!" | mkmsg -b "src/new" -- "To" "admin@example.com"
	echo "Bye!" | mkmsg -b "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match !(body /hello/i or header "To" /admin/) move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "match nested"; then
	mkmd "src" "dst"
	echo "Hello!" | mkmsg -b "src/new" -- "To" "user@example.com"
	echo "Bye!" | mkmsg -b "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /user/ {
			match new {
				match body /Hello/ move "dst"
			}
		}
	}
	EOF
	mdsort
	refute_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "match nested without interpolation matches"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match new {
			match new move "dst"
		}
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "match nested with negate"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "user@example.com"
	mkmsg "src/new" -- "To" "admin@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match new {
			match ! header "To" /admin/ move "dst"
		}

		match ! header "To" /user/ {
			match new move "dst"
		}
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "match case insensitive"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "UsEr@ExAmPlE.CoM"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /user/i move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "match force"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "user@dst.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /(user)/ and header "To" /(dst)/f move "\1"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "match lowercase"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "user@DST.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /dst/il move "\0"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "match uppercase"; then
	mkmd "src" "DST"
	mkmsg "src/new" -- "To" "user@dst.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /dst/u move "\0"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "DST/new"
fi

if testcase "unique suffix is preserved when valid"; then
	mkmd "src" "dst"
	mkmsg -s ":2,S" "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /user/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	assert_find "dst/new" "*:2,S"
fi

if testcase "destination interpolation first match is favored"; then
	mkmd "src" "first"
	mkmsg "src/new" -- "To" "first@last.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /(first)/ and header "To" /(last)/ move "\1"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "first/new"
fi

if testcase "destination interpolation out of bounds"; then
	mkmd "src"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /./ move "\1"
	}
	EOF
	mdsort -e >$TMP1
	grep -q '\\1/new: invalid back-reference' $TMP1 ||
		fail - "expected back-reference to be invalid" <$TMP1
fi

if testcase "destination interpolation out of range"; then
	mkmd "src"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /./ move "\99999999999999999999"
	}
	EOF
	mdsort -e >$TMP1
	grep -q '9/new: invalid back-reference' $TMP1 ||
		fail - "expected back-reference to be invalid" <$TMP1
fi

if testcase "destination interpolation negative"; then
	mkmd "src" "\-1"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /./ move "\-1"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "\-1/new"
fi

if testcase "destination interpolation non-decimal"; then
	mkmd "src" "userx1"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /(user)/ move "\0x1"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "userx1/new"
fi

if testcase "destination interpolation with none body/header"; then
	mkmd "src" ""
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match new move "\1"
	}
	EOF
	mdsort -e >$TMP1
	grep -q '\\1/new: invalid back-reference' $TMP1 ||
		fail - "expected back-reference to be invalid" <$TMP1
fi

if testcase "destination interpolation with negate"; then
	mkmd "src"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match ! header "To" /(user)/ or new move "\1"
	}
	EOF
	mdsort -e >$TMP1
	grep -q '\\1/new: invalid back-reference' $TMP1 ||
		fail - "expected back-reference to be invalid" <$TMP1
fi

if testcase "destination interpolation too long"; then
	mkmd "src"
	mkmsg "src/new" -- "To" "user@$(genstr $PATH_MAX).com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /(.+)/ move "\1"
	}
	EOF
	mdsort -e >$TMP1
	grep -q '\\1/new: interpolated string too long' $TMP1 ||
		fail - "expected destination to be too long" <$TMP1
fi

if testcase "unknown option"; then
	mdsort -e -- -1 >$TMP1
	grep -q 'usage' $TMP1 || fail - "expected usage output" <$TMP1
fi

if testcase "extraneous option"; then
	mdsort -e -- extraneous >$TMP1
	grep -q 'usage' $TMP1 || fail - "expected usage output" <$TMP1
fi

if testcase "long filename"; then
	mkmd "src" "dst"
	touch "${TSHDIR}/src/new/$(genstr "$NAME_MAX")"
	cat <<-EOF >$CONF
	maildir "src" {
		match all move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "pattern delimiter"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "From" "/dst@example.com/"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "From" @/(dst)\@example.com/@ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

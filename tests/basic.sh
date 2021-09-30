if testcase "tilde expansion"; then
	mkmd "src" "dst"
	echo "Hello Bob" | mkmsg -b "src/new" -- "To" "user@example.com"
	cat <<-EOF >"$CONF"
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
	cat <<-EOF >"$CONF"
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
	cat <<-EOF >"$CONF"
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
	cat <<-EOF >"$CONF"
	maildir "src" {
		match !(body /hello/i or header "To" /admin/) move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "match nested with interpolation"; then
	mkmd "src" "dst"
	echo "foo" | mkmsg -b "src/new" -- "To" "user@example.com"
	echo "dst" | mkmsg -b "src/new" -- "To" "user@example.com"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "To" /user/ {
			match new {
				match body /dst/ move "\0"
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
	cat <<-EOF >"$CONF"
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
	cat <<-EOF >"$CONF"
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
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "To" /user/i move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "match lowercase"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "user@DST.com"
	cat <<-EOF >"$CONF"
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
	cat <<-EOF >"$CONF"
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
	cat <<-EOF >"$CONF"
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
	cat <<-EOF >"$CONF"
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
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "To" /./ move "\\1"
	}
	EOF
	mdsort -e - <<-EOF
	mdsort: \\1/new: invalid back-reference
	EOF
fi

if testcase "destination interpolation out of range"; then
	mkmd "src"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "To" /./ move "\\99999999999999999999"
	}
	EOF
	mdsort -e - <<-EOF
	mdsort: \\99999999999999999999/new: invalid back-reference
	EOF
fi

if testcase "destination interpolation negative"; then
	mkmd "src" "\-1"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >"$CONF"
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
	cat <<-EOF >"$CONF"
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
	cat <<-EOF >"$CONF"
	maildir "src" {
		match new move "\\1"
	}
	EOF
	mdsort -e - <<-EOF
	mdsort: \\1/new: invalid back-reference
	EOF
fi

if testcase "destination interpolation with negate"; then
	mkmd "src"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match ! header "To" /(user)/ or new move "\\1"
	}
	EOF
	mdsort -e - <<-EOF
	mdsort: \\1/new: invalid back-reference
	EOF
fi

if testcase "unknown option"; then
	mdsort -e -- -1 >"$TMP1"
	grep -q 'usage' "$TMP1" || fail - "expected usage output" <"$TMP1"
fi

if testcase "extraneous option"; then
	mdsort -e -- extraneous >"$TMP1"
	grep -q 'usage' "$TMP1" || fail - "expected usage output" <"$TMP1"
fi

if testcase "long filename"; then
	mkmd "src" "dst"
	touch "${TSHDIR}/src/new/$(genstr "$NAME_MAX")"
	cat <<-EOF >"$CONF"
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
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "From" @/(dst)\@example.com/@ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

# Exercise header reallocation logic.
if testcase "many headers"; then
	mkmd "src" "dst"
	mkmsg "src/new"
	cp "$(findmsg -p "src/new")" "$TMP1"
	{
		_i=0
		while [ "$_i" -lt 32 ]; do
			echo "$(genstr "$((RANDOM % 128))"): $(genstr "$((RANDOM % 128))")"
			_i=$((_i + 1))
		done

		cat "$TMP1"
	} >"$(findmsg -p "src/new")"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase -t fault "readdir failure"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all move "dst"
	}
	EOF
	mdsort -e -f "name=maildir_read,errno=EINVAL" - <<-EOF
	mdsort: fault: maildir_read
	EOF
fi

if testcase -t fault "readdir unknown file type"; then
	mkmd "src" "dst"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all move "dst"
	}
	EOF
	mdsort -f "name=readdir_type" - <<-EOF
	mdsort: fault: readdir_type
	EOF
	assert_empty "src/new"
	refute_empty "dst/new"
fi

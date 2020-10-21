if testcase "many move actions"; then
	mkmd "src" "dst1" "dst2" "dst3"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all move "dst1" move "dst2" move "dst3"
	}
	EOF
	mdsort
	assert_empty "src/new"
	assert_empty "dst1/new"
	assert_empty "dst2/new"
	refute_empty "dst3/new"
fi

if testcase "interpolation too long"; then
	_to="user@$(genstr $PATH_MAX).com"
	mkmd "src"
	mkmsg "src/new" -- "To" "$_to"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /(.+)/ move "\1"
	}
	EOF
	mdsort -e >"$TMP1"
	if ! grep -q 'matches_interpolate:' "$TMP1"; then
		fail - "expected too long error" <"$TMP1"
	fi
fi

# Ensure error path is free from memory leaks.
if testcase "interpolation out of bounds"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match all move "/\0"
	}
	EOF
	mdsort -e >/dev/null
fi

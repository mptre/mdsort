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

if testcase "destination missing"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all move "dst"
	}
	EOF
	mdsort -e - <<-EOF
mdsort: opendir: dst/new: No such file or directory
	EOF
fi

if testcase "interpolation too long"; then
	_to="user@$(genstr "$PATH_MAX").com"
	mkmd "src"
	mkmsg "src/new" -- "To" "$_to"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "To" /(.+)/ move "\\1"
	}
	EOF
	mdsort -e - <<-EOF
	mdsort: match_interpolate: $(errno ENAMETOOLONG)
	EOF
fi

# Ensure error path is free from memory leaks.
if testcase "interpolation out of bounds"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all move "/\\0"
	}
	EOF
	mdsort -e >/dev/null
fi

# Exercise fallback logic of writing a new message while trying to move across
# file systems.
if testcase -t fault "exdev"; then
	mkmd "src" "dst"
	mkmsg -b "src/new" -- "B" "1" "A" "2" <<-EOF
	The headers above are intentionally not sorted in order to ensure
	that the ordering is preserved.
	EOF
	cat "$(findmsg -p "src/new")" >"$TMP1"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all move "dst"
	}
	EOF
	mdsort -f "name=maildir_rename,errno=EXDEV" - <<-EOF
	mdsort: fault: maildir_rename
	EOF
	assert_empty "src/new"
	refute_empty "dst/new"
	cat "$(findmsg -p "dst/new")" | assert_file "$TMP1" -
fi

# Ensure that a failure in the message write fallback logic does not leave any
# broken message behind.
if testcase -t fault "exdev write failure"; then
	mkmd "src" "dst"
	mkmsg "src/new"
	cat >"$CONF" <<-EOF
	maildir "src" {
		match all move "dst"
	}
	EOF
	mdsort -e -f "name=maildir_rename,errno=EXDEV:name=message_write" - <<-EOF
	mdsort: fault: maildir_rename
	mdsort: fault: message_write
	EOF
	refute_empty "src/new"
	assert_empty "dst/new"
fi

# Ensure that a failure in the message write fallback logic does not leave any
# broken message behind.
if testcase -t fault "exdev unlink failure"; then
	mkmd "src" "dst"
	mkmsg "src/new"
	cat >"$CONF" <<-EOF
	maildir "src" {
		match all move "dst"
	}
	EOF
	mdsort -e -f "name=maildir_rename,errno=EXDEV:name=maildir_unlink" - <<-EOF
	mdsort: fault: maildir_rename
	mdsort: fault: maildir_unlink
	EOF
	refute_empty "src/new"
	assert_empty "dst/new"
fi

if testcase -t fault "message path too long"; then
	mkmd "src" "dst"
	mkmsg "src/new"
	cat >"$CONF" <<-EOF
	maildir "src" {
		match all move "dst"
	}
	EOF
	mdsort -e -f "name=message_set_file,errno=ENAMETOOLONG" - <<-EOF
	mdsort: fault: message_set_file
	EOF
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase -t fault "rename failure"; then
	mkmd "src" "dst"
	mkmsg "src/new"
	cat >"$CONF" <<-EOF
	maildir "src" {
		match all move "dst"
	}
	EOF
	mdsort -e -f "name=maildir_rename,errno=ENOSPC" - <<-EOF
	mdsort: fault: maildir_rename
	EOF
	refute_empty "src/new"
	assert_empty "dst/new"
fi

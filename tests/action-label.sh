# assert_label label file
assert_label() {
	local _got

	if [ $# -ne 2 ]; then
		fail "assert_label: too many arguments: ${*}"
	fi

	_got="$(sed -n -e '/^X-Label/s/^[^:]*: //p' "$2")"
	assert_eq "$1" "$_got"
}

if testcase "no x-label header"; then
	mkmd "src"
	mkmsg -H "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match all label "label"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_label label ${TSHDIR}/src/new/*
fi

if testcase "x-label header present but empty"; then
	mkmd "src"
	mkmsg -H "src/new" -- "X-Label" ""
	cat <<-EOF >$CONF
	maildir "src" {
		match all label "label"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_label label ${TSHDIR}/src/new/*
fi

if testcase "multiple x-label headers"; then
	mkmd "src"
	mkmsg -H "src/new" -- "X-Label" "one" "X-Label" "two" "X-Subject" "hello"
	cat <<-EOF >$CONF
	maildir "src" {
		match all label "label"
	}
	EOF
	cat <<-EOF >$TMP1
	X-Label: one two label
	X-Subject: hello

	EOF
	mdsort
	refute_empty "src/new"
	assert_label "one two label" ${TSHDIR}/src/new/*
	assert_file ${TSHDIR}/src/new/* $TMP1
fi

if testcase "multiple labels"; then
	mkmd "src"
	mkmsg -H "src/new" -- "X-Label" "one"
	cat <<-EOF >$CONF
	maildir "src" {
		match all label { "two" "three" }
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_label "one two three" ${TSHDIR}/src/new/*
fi

if testcase "multiple labels already present"; then
	mkmd "src"
	mkmsg -H "src/new" -- "X-Label" "one two three"
	cat <<-EOF >$CONF
	maildir "src" {
		match all label { "two" "three" }
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_label "one two three two three" ${TSHDIR}/src/new/*
fi

if testcase "many label actions"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match new label "1" label "2" label { "3" "4" }
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_label "1 2 3 4" ${TSHDIR}/src/new/*
fi

if testcase "label and move"; then
	mkmd "src" "dst"
	mkmsg -H "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match all label "label" move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	assert_label label ${TSHDIR}/dst/new/*
fi

if testcase "move and label"; then
	mkmd "src" "dst"
	mkmsg -H "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match all move "dst" label "label"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	assert_label label ${TSHDIR}/dst/new/*
fi

if testcase "label and flag"; then
	mkmd "src"
	mkmsg -H "src/cur"
	cat <<-EOF >$CONF
	maildir "src" {
		match all label "label" flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	assert_label label ${TSHDIR}/src/cur/*
fi

if testcase "flag and label"; then
	mkmd "src"
	mkmsg -H "src/cur"
	cat <<-EOF >$CONF
	maildir "src" {
		match all flag !new label "label"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	assert_label label ${TSHDIR}/src/cur/*
fi

if testcase "label and pass"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /user/ label "user" pass
		match header "To" /example/ label "example" pass
		match all move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	assert_label "user example" ${TSHDIR}/dst/new/*
fi

if testcase "interpolation with no x-label header"; then
	mkmd "src"
	mkmsg -H "src/new" -- "To" "user+label@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /user\+(.+)@example.com/ label "\1"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_label label ${TSHDIR}/src/new/*
fi

if testcase "interpolation with x-label header"; then
	mkmd "src"
	mkmsg -H "src/new" -- "X-Label" "label"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "X-Label" /.+/ label "\0"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_label "label label" ${TSHDIR}/src/new/*
fi

# Ensure error path is free from memory leaks.
if testcase "interpolation out of bounds"; then
	mkmd "src"
	mkmsg -H "src/new" -- "X-Label" "label"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "X-Label" /.+/ label "label \1"
	}
	EOF
	mdsort -e >/dev/null
fi

# The label action constructs a destination path, however the one from the move
# action must take higher precedence.
if testcase "dry run label and move"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match all label "label" move "dst"
	}
	EOF
	mdsort - -- -d <<EOF
$(findmsg "src/new") -> dst/new
EOF
fi

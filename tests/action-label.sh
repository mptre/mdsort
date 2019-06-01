# assert_label label file
assert_label() {
	local _got

	if [ $# -ne 2 ]; then
		fatal "assert_label: too many arguments: ${*}"
	fi

	_got="$(sed -n -e '/^X-Label/s/^[^:]*: //p' "$2")"
	assert_eq "$1" "$_got"
}

if testcase "duplicate label actions"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/INBOX" {
		match new label "one" label "two"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: label action already defined
	EOF
fi

if testcase "word boundary begin"; then
	mkmd "src"
	mkmsg -H "src/new" -- "X-Label" "label"
	cat <<-EOF >$CONF
	maildir "src" {
		match all label "label"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_label label ${WRKDIR}/src/new/*
fi

if testcase "word boundary middle"; then
	mkmd "src"
	mkmsg -H "src/new" -- "X-Label" "first label last"
	cat <<-EOF >$CONF
	maildir "src" {
		match all label "label"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_label "first label last" ${WRKDIR}/src/new/*
fi

if testcase "word boundary end"; then
	mkmd "src"
	mkmsg -H "src/new" -- "X-Label" "first label"
	cat <<-EOF >$CONF
	maildir "src" {
		match all label "label"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_label "first label" ${WRKDIR}/src/new/*
fi

if testcase "word boundary substring"; then
	mkmd "src"
	mkmsg -H "src/new" -- "X-Label" "llabell"
	cat <<-EOF >$CONF
	maildir "src" {
		match all label "label"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_label "llabell label" ${WRKDIR}/src/new/*
fi

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
	assert_label label ${WRKDIR}/src/new/*
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
	assert_label label ${WRKDIR}/src/new/*
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
	assert_label "one two label" ${WRKDIR}/src/new/*
	assert_file ${WRKDIR}/src/new/* $TMP1
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
	assert_label "one two three" ${WRKDIR}/src/new/*
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
	assert_label "one two three" ${WRKDIR}/src/new/*
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
	assert_label label ${WRKDIR}/dst/new/*
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
	assert_label label ${WRKDIR}/dst/new/*
fi

if testcase "label and flag"; then
	mkmd "src"
	mkmsg -H "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match all label "label" flag !new
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	assert_label label ${WRKDIR}/src/cur/*
fi

if testcase "flag and label"; then
	mkmd "src"
	mkmsg -H "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match all flag !new label "label"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "src/cur"
	assert_label label ${WRKDIR}/src/cur/*
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
	assert_label "user example" ${WRKDIR}/dst/new/*
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
	assert_label label ${WRKDIR}/src/new/*
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
	assert_label "label label" ${WRKDIR}/src/new/*
fi

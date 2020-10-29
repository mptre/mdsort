if testcase "scalar abbreviation"; then
	cat <<-EOF >$CONF
	maildir "src" {
		match date > 1 second move "dst"
		match date > 1 minute move "dst"
		match date > 1 hour move "dst"
		match date > 1 day move "dst"
		match date > 1 month move "dst"
	}
	EOF
	mdsort -- -n
fi

if testcase "scalar abbreviation ambiguous"; then
	cat <<-EOF >$CONF
	maildir "src" {
		match date > 1m move "dst"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: ambiguous keyword: m
	EOF
fi

if testcase "age too large"; then
	cat <<-EOF >$CONF
	maildir "src" {
		match date > 4294967296 seconds move "dst"
		match date > 9999999999 seconds move "dst"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: integer too large
	mdsort.conf:3: integer too large
	EOF
fi

if testcase "age and scalar too large"; then
	cat <<-EOF >$CONF
	maildir "src" {
		match date > 4294967295 years move "dst"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: integer too large
	EOF
fi

if testcase "greater than"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "Date" "$(now -1)"
	mkmsg "src/new" -- "Date" "$(now -60)"
	cat <<-EOF >$CONF
	maildir "src" {
		match date > 30 seconds move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "less than"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "Date" "$(now -1)"
	mkmsg "src/new" -- "Date" "$(now -60)"
	cat <<-EOF >$CONF
	maildir "src" {
		match date < 30 seconds move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "date format variations"; then
	mkmd "src" "dst"
	# Missing seconds.
	mkmsg "src/new" -- "Date" "$(now -f '%a, %d %b %Y %H:%M %z' -60)"
	# Missing weekday.
	mkmsg "src/new" -- "Date" "$(now -f '%d %b %Y %H:%M:%S %z' -60)"
	# Timezone abbreviation without offset.
	mkmsg "src/new" -- "Date" "$(now -f '%a, %d %b %Y %H:%M:%S GMT' -86400)"
	cat <<-EOF >$CONF
	maildir "src" {
		match date > 30 seconds move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "invalid date"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "Date" "$(now -f '%d' -60)"
	cat <<-EOF >$CONF
	maildir "src" {
		match date > 30 seconds move "dst"
	}
	EOF
	mdsort -e >/dev/null
	refute_empty "src/new"
	assert_empty "dst/new"
fi

if testcase "interpolation regression"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "Date" "$(now -60)" "From" "dst"
	cat <<-EOF >$CONF
	maildir "src" {
		match date > 30 seconds and header "From" /dst/ move "\0"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "dry run"; then
	_d="$(now -120)"
	mkmd "src" "dst"
	mkmsg "src/new" -- "Date" "$_d"
	cat <<-EOF >$CONF
	maildir "src" {
		match date > 1 minute move "dst"
	}
	EOF
	mdsort - -- -d <<EOF
$(findmsg "src/new") -> dst/new
mdsort.conf:2: Date: ${_d}
                     ^                             $
EOF
fi


# Constructing the expected output is quite tedious, just exercise the code
# path.
if testcase "dry run modified"; then
	mkmd "src" "dst"
	mkmsg -m "$(now -f "%Y%m%d%H%M.%S" -120)" "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match date modified > 1 minute move "dst"
	}
	EOF
	mdsort -- -d >/dev/null
fi

if testcase "header"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "Date" "$(now -60)"
	cat <<-EOF >$CONF
	maildir "src" {
		match date header > 30 seconds move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

# Just ensure the config is accepted.
if testcase "access"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match date access > 30 seconds move "dst"
	}
	EOF
	mdsort
fi

# Just ensure the config is accepted.
if testcase "created"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match date created > 30 seconds move "dst"
	}
	EOF
	mdsort
fi

if testcase "modified"; then
	mkmd "src" "dst"
	mkmsg -m "$(now -f "%Y%m%d%H%M.%S" -60)" "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match date modified > 30 seconds move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

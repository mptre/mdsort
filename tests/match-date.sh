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

if testcase -t leaky "scalar abbreviation ambiguous"; then
	cat <<-EOF >$CONF
	maildir "src" {
		match date > 1m move "dst"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: ambiguous keyword: m
	mdsort.conf:2: syntax error
	EOF
fi

if testcase -t leaky "age too large"; then
	cat <<-EOF >$CONF
	maildir "src" {
		match date > 4294967296 seconds  move "dst"
		match date > 9999999999 seconds  move "dst"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: integer too large
	mdsort.conf:3: integer too large
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
	mdsort >/dev/null
	refute_empty "src/new"
	assert_empty "dst/new"
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
	cat <<EOF >$TMP1
mdsort.conf:2: Date: ${_d}
                     ^                             $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	assert_file $TMP1 $TMP2
fi

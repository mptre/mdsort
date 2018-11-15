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
	pass
fi

if testcase -e "scalar abbreviation ambiguous"; then
	cat <<-EOF >$CONF
	maildir "src" {
		match date > 1m move "dst"
	}
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:2: ambiguous keyword: m
	mdsort.conf:2: syntax error
	EOF
	pass
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
	pass
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
	pass
fi

if testcase "missing weekday"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "Date" "$(now -f '%d %b %Y %H:%M:%S %z' -60)"
	cat <<-EOF >$CONF
	maildir "src" {
		match date > 30 seconds move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	pass
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
Date: ${_d}
      ^                             $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	fcmp $TMP1 $TMP2
	pass
fi

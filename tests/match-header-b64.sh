if testcase "basic"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "Subject" "=?UTF-8?B?aGFjay1sw7ZyZGFnYXI=?="
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "Subject" /hack-lördagar/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "invalid"; then
	mkmd "src"
	mkmsg "src/new" -- "Subject" "=?UTF-8?B?aGFjay1sw7ZyZGFnYXI==?="
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "Subject" /hack-lördagar/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "dry run"; then
	mkmd "src"
	mkmsg "src/new" -- "Subject" "=?UTF-8?B?aGFjay1sw7ZyZGFnYXI=?="
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "Subject" /hack-lördagar/ move "dst"
	}
	EOF
	mdsort - -- -d <<EOF
$(findmsg "src/new") -> dst/new
mdsort.conf:2: Subject: hack-lördagar
                        ^           $
EOF
fi

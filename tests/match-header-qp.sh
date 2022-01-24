if testcase "basic"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "Subject" "=?UTF-8?Q?ny_fr=C3=A5ga_r=C3=B6rande?="
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "Subject" /ny fråga/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "multiple lines"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- \
		"Subject" "$(printf '=?UTF-8?Q?ny?=\n =?UTF-8?Q?fr=C3=A5ga_r=C3=B6rande?=')"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "Subject" /ny fråga/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "invalid encoding"; then
	mkmd "src"
	mkmsg "src/new" -- "Subject" "=?UTF-8?A?ny_fr=C3=A5ga_r=C3=B6rande?="
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "Subject" /ny fråga/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "invalid charset delimiter"; then
	mkmd "src"
	mkmsg "src/new" -- "Subject" "=?UTF-8"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "Subject" /ny fråga/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "invalid encoding delimiter"; then
	mkmd "src"
	mkmsg "src/new" -- "Subject" "=?UTF-8?Q"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "Subject" /ny fråga/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "invalid missing trailing delimiter"; then
	mkmd "src"
	mkmsg "src/new" -- "Subject" "=?UTF-8?Q?ny_fr=C3=A5ga_r=C3=B6rande"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "Subject" /ny fråga/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "invalid multiple lines"; then
	mkmd "src"
	mkmsg "src/new" -- \
		"Subject" "=?UTF-8?Q?ny?=\n fraga"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "Subject" /ny fraga/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase "dry run"; then

	mkmd "src"
	mkmsg "src/new" -- "Subject" "=?UTF-8?Q?ny_fr=C3=A5ga_r=C3=B6rande?="
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "Subject" /ny fråga/ move "dst"
	}
	EOF
	mdsort - -- -d <<EOF
$(findmsg "src/new") -> dst/new
mdsort.conf:2: Subject: ny fråga rörande
                        ^      $
EOF
fi

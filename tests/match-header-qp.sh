# subject
#
# Fairly long base64 encoded subject that spans over multiple lines.
subject() {
	printf '=?utf-8?B?zojOus60zr/Pg863IGUtzrvOv86zzrHPgc65zrHPg868zr/P?=\n '
	printf '=?utf-8?B?jSDPg8+EzrHOuM61z4HOrs+CIDEzODI0OTI0IM6ZzrHOvc6/z4XOsc+B?=\n '
	printf '=?utf-8?B?zq/Ov8+F?='
}

if testcase "basic"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "Subject" "=?UTF-8?Q?ny_fr=C3=A5ga_r=C3=B6rande?="
	mkmsg "src/new" -- "Subject" "=?UTF-8?q?ny_fr=C3=A5ga_r=C3=B6rande?="
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
	mkmsg "src/new" -- "Subject" "$(subject)"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "Subject" /Έκδοση e-λογαριασμού/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "followed by ascii"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "Subject" "$(subject) ascii"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "Subject" / ascii/ move "dst"
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

if testcase "dry run multiple lines"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "Subject" "$(subject)"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "Subject" /e-λογαριασμού/ move "dst"
	}
	EOF
	mdsort - -- -d <<EOF
$(findmsg "src/new") -> dst/new
mdsort.conf:2: Subject: Έκδοση e-λογαριασμού σταθερής 13824924 Ιανουαρίου
                               ^           $
EOF
fi

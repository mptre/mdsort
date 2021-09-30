if testcase "basic"; then
	mkmd "src" "dst"
	mkmsg -b "src/new" -- "Content-Transfer-Encoding" "quoted-printable" <<-EOF
	f=
	oo
	EOF
	cat <<-EOF >"$CONF"
	maildir "src" {
		match body /foo/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "attachment"; then
	mkmd "src"
	mkattach "src/new" "text/plain" "quoted-printable" "$(printf 'f=\noo\n')"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all attachment {
			match body /.*/ exec stdin body "cat"
		}
	}
	EOF
	mdsort - <<-EOF
	foo
EOF
fi

if testcase "invalid content transfer encoding"; then
	mkmd "src" "dst"
	printf '=30' | mkmsg -b "src/new" -- "Content-Transfer-Encoding" "Xquoted-printable"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match body /=30$/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "non hexadecimal"; then
	mkmd "src" "dst"
	printf '=3X' | mkmsg -b "src/new" -- "Content-Transfer-Encoding" "quoted-printable"
	printf '=X3' | mkmsg -b "src/new" -- "Content-Transfer-Encoding" "quoted-printable"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match body /=3X$/ or body /=X3$/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "too few characters"; then
	mkmd "src" "dst"
	printf '=3' | mkmsg -b "src/new" -- "Content-Transfer-Encoding" "quoted-printable"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match body /=3$/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "trailing equal"; then
	mkmd "src" "dst"
	printf '=' | mkmsg -b "src/new" -- "Content-Transfer-Encoding" "quoted-printable"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match body /=$/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "dry run"; then
	mkmd "src"
	mkmsg -b "src/new" -- "Content-Transfer-Encoding" "quoted-printable" <<-EOF
	F=
	OO p=C3=A9dagogues
	EOF
	cat <<-EOF >"$CONF"
	maildir "src" {
		match body /.*/ move "dst"
	}
	EOF
	mdsort - -- -d <<EOF
$(findmsg "src/new") -> dst/new
mdsort.conf:2: Body: FOO pédagogues
                     ^            $
EOF
fi

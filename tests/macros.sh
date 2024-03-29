if testcase "basic"; then
	mkmd "src1" "src2" "src3" "dst1" "dst2"
	mkmsg "src1/new"
	mkmsg "src2/new"
	mkmsg "src3/new"
	cat <<-'EOF' >"$CONF"
	one = "dst1"
	maildir "src1" {
		match all move "${one}"
	}

	two = "dst2"
	maildir "src2" {
		match all move "${two}"
	}

	three = "dst3"
	maildir "src3" {
		match all exec { "echo" "${three}" }
	}
	EOF
	mdsort - <<-EOF
	dst3
	EOF
	assert_empty "src1/new"
	assert_empty "src2/new"
	refute_empty "dst1/new"
	refute_empty "dst2/new"
fi

if testcase "nested macro"; then
	mkmd "src" "dst"
	mkmsg "src/new"
	cat <<-'EOF' >"$CONF"
	head = "d"
	tail = "t"
	dst = "${head}s${tail}"

	maildir "src" {
		match all move "${dst}"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "empty macro"; then
	cat <<-EOF >"$CONF"
	empty = ""
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:1: empty string
	mdsort.conf:1: unused macro: empty
	EOF
fi

if testcase "collision with keyword"; then
	cat <<-EOF >"$CONF"
	maildir = "maildir"
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:1: syntax error
	EOF
fi

if testcase "collision with date scalar"; then
	cat <<-'EOF' >"$CONF"
	s = "src"
	seconds = "dst"

	maildir "${s}" {
		match all move "${seconds}"
	}
	EOF
	mdsort -- -n
fi

if testcase -t memleak "macro not on root level"; then
	cat <<-EOF >"$CONF"
	maildir "src" {
		dst = "dst"
	}
	EOF
	mdsort -e -- -n >"$TMP1"
	# Remove unstable output, some yacc implementations invokes yylex() more
	# than once in case of an error.
	sed -i -e '/unknown keyword: dst/d' "$TMP1"
	assert_file - "$TMP1" <<-EOF
	mdsort.conf:2: syntax error
	EOF
fi

if testcase "unknown macro"; then
	cat <<-'EOF' >"$CONF"
	maildir "src" {
		match all move "${dst}"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: unknown macro used in string: dst
	EOF
fi

if testcase "unused macro"; then
	cat <<-'EOF' >"$CONF"
	dst = "dst"
	trash = "trash"

	maildir "src" {
		match all move "${dst}"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: unused macro: trash
	EOF
fi

if testcase "unterminated macro"; then
	cat <<-'EOF' >"$CONF"
	maildir "src" {
		match all move "${dst"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: unterminated macro
	EOF
fi

if testcase "pre defined macros cannot be redefined"; then
	cat <<-EOF >"$CONF"
	path = "path"
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:1: macro already defined: path
	EOF
fi

if testcase "macro used in wrong context"; then
	cat <<-'EOF' >"$CONF"
	maildir "src" {
		match header "${path}" /./ move "dst"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: macro used in wrong context: path
	EOF
fi

if testcase "sticky basic"; then
	mkmd "src" "dst"
	mkmsg "src/new"
	cat <<-'EOF' >"$CONF"
	maildir "${m}" {
		match all move "dst"
	}
	EOF
	mdsort -- -D m=src
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "sticky precedence"; then
	mkmd "src" "dst"
	mkmsg "src/new"
	cat <<-'EOF' >"$CONF"
	m = "nein"
	maildir "${m}" {
		match all move "dst"
	}
	EOF
	mdsort -- -D m=src
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "sticky wrong context"; then
	mdsort -e - -- -D path=src <<-EOF
	mdsort: invalid macro: path
	EOF
fi

if testcase "sticky redefined"; then
	mdsort -e - -- -D one=1 -D one=2 <<-EOF
	mdsort: invalid macro: one
	EOF
fi

if testcase "sticky redefined config" ; then
	cat <<-'EOF' >"$CONF"
	m = "one"
	m = "two"
	maildir "${m}" {
		match all move "dst"
	}
	EOF
	mdsort -e - -- -D m=nein <<-EOF
	mdsort.conf:2: macro already defined: m
	EOF
fi

if testcase "sticky unused"; then
	cat <<-'EOF' >"$CONF"
	maildir "src" {
		match all move "dst"
	}
	EOF
	mdsort -e - -- -D unused=1 <<-EOF
	mdsort.conf:0: unused macro: unused
	EOF
fi

if testcase "sticky invalid"; then
	cat <<-'EOF' >"$CONF"
	maildir "src" {
		match all move "dst"
	}
	EOF
	mdsort -e - -- -D invalid <<-EOF
	mdsort: missing macro separator: invalid
	EOF
fi

if testcase "action label with pre defined macros"; then
	mkmd "src"
	mkmsg "src/new"
	_label="$(findmsg "src/new")"
	cat <<-'EOF' >"$CONF"
	maildir "src" {
		match all label "${path}"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_label "$_label" "$(findmsg "src/new")"
fi

if testcase "action exec with pre defined macros"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-'EOF' >"$CONF"
	maildir "src" {
		match all exec { "sh" "-c" "echo ${path}" }
	}
	EOF
	mdsort - <<-EOF
	$(findmsg "src/new")
	EOF
fi

if testcase "action move with pre defined macros"; then
	:
fi

if testcase "action attachment with pre defined macros"; then
	mkmd "src"
	mkmsg -A "src/new"
	cat <<-'EOF' >"$CONF"
	maildir "src" {
		match all attachment {
			match all exec { "echo" "${path}" }
		}
	}
	EOF
	mdsort - <<-EOF
	$(findmsg "src/new")
	$(findmsg "src/new")
	EOF
fi

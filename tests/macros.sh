if testcase "basic"; then
	mkmd "src1" "src2" "src3" "dst1" "dst2"
	mkmsg "src1/new"
	mkmsg "src2/new"
	mkmsg "src3/new"
	cat <<-EOF >$CONF
	one = "dst1"
	maildir "src1" {
		match all move "\${one}"
	}

	two = "dst2"
	maildir "src2" {
		match all move "\${two}"
	}

	three = "dst3"
	maildir "src3" {
		match all exec { "echo" "\${three}" }
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
	cat <<-EOF >$CONF
	head = "d"
	tail = "t"
	dst = "\${head}s\${tail}"

	maildir "src" {
		match all move "\${dst}"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "empty macro"; then
	cat <<-EOF >$CONF
	empty = ""
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:1: empty string
	mdsort.conf:1: unused macro: empty
	EOF
fi

if testcase "collision with keyword"; then
	cat <<-EOF >$CONF
	maildir = "maildir"
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:1: syntax error
	EOF
fi

if testcase -t leaky "macro not on root level"; then
	cat <<-EOF >$CONF
	maildir "src" {
		dst = "dst"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: syntax error
	EOF
fi

if testcase "unknown macro"; then
	cat <<-EOF >$CONF
	maildir "src" {
		match all move "\${dst}"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: unknown macro used in string: dst
	EOF
fi

if testcase "unused macro"; then
	cat <<-EOF >$CONF
	dst = "dst"
	trash = "trash"

	maildir "src" {
		match all move "\${dst}"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: unused macro: trash
	EOF
fi

if testcase "unterminated macro"; then
	cat <<-EOF >$CONF
	maildir "src" {
		match all move "\${dst"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: unterminated macro
	EOF
fi

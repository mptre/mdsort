if testcase "basic"; then
	mkmd "src"
	mkmsg "src/new" -- "To" "one"
	mkmsg "src/new" -- "To" "two"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "To" /.*/ exec { "echo" "\0" }
	}
	EOF
	# The order in which entries are returned from readdir(3) is not
	# deterministic.
	mdsort | sort >"$TMP1"
	assert_file - "$TMP1" <<-EOF
	one
	two
	EOF
fi

if testcase "stdin defaults to /dev/null"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all exec "cat"
	}
	EOF
	echo nein | mdsort >"$TMP1"
	assert_file - "$TMP1" </dev/null
fi

if testcase "stdin"; then
	mkmd "src"
	echo body | mkmsg -b "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all exec stdin "cat" exec stdin "cat"
	}
	EOF
	mdsort - <<EOF
Content-Type: text/plain

body
Content-Type: text/plain

body
EOF
fi

if testcase "stdin body"; then
	mkmd "src"
	mkmsg -b "src/new" <<-EOF
	This is the body.
	EOF
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all exec stdin body "cat"
	}
	EOF
	mdsort - -- <<-EOF
	This is the body.
	EOF
fi

if testcase "stdin body base64"; then
	mkmd "src"
	b64 "This is the body." | mkmsg -b "src/new" -- \
		"Content-Transfer-Encoding" "base64"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all exec stdin body "cat"
	}
	EOF
	mdsort - -- <<-EOF
	This is the body.
	EOF
fi

if testcase -t memleak "body without stdin"; then
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all exec body "cat"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: invalid exec options
	EOF
fi

if testcase "duplicate options"; then
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all exec stdin stdin "cat"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: exec options cannot be repeated
	EOF
fi

if testcase "exit non-zero"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all exec { "sh" "-c" "exit 1" }
	}
	EOF
	mdsort -e - <<-EOF
	mdsort: sh: exited 1
	EOF
fi

if testcase "exit signal"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-'EOF' >"$CONF"
	maildir "src" {
		match all exec { "sh" "-c" "kill -9 $$" }
	}
	EOF
	mdsort -e - <<-EOF
	mdsort: sh: exited 137
	EOF
fi

if testcase "command not found"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all exec "command-not-found"
	}
	EOF
	mdsort -e >/dev/null
fi

if testcase "remote code execution"; then
	mkmd "src"
	cat <<-EOF >"$CONF"
	stdin {
		match header "To" /.*/ exec { "sh" "-c" "echo \0" }
	}
	EOF
	printf "To: user; echo pwned\n\n" >"$TMP1"
	mdsort -- - <"$TMP1" >"$TMP2"
	assert_file - "$TMP2" <<-EOF
	user
	pwned
	EOF
fi

# Ensure error path is free from memory leaks.
if testcase "interpolation out of bounds"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all exec { "sh" "-c" "ls \0" }
	}
	EOF
	mdsort -e >/dev/null
fi

# Ensure no unwanted file descriptors are leaked into the executed command.
if testcase -t fdleak "file descriptors"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-'EOF' >"$CONF"
	maildir "src" {
		match all exec { "sh" "-c" "i=3; while [ $i -lt 10 ]; do command >&$i && exit 1; i=$((i + 1)); done" }
	}
	EOF
	mdsort >/dev/null
fi

if testcase "label and exec"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all label "label" exec stdin "cat"
	}
	EOF
	mdsort >"$TMP1"
	assert_file - "$TMP1" <<-EOF
	Content-Type: text/plain
	X-Label: label

	EOF
fi

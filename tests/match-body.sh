if testcase "basic"; then
	mkmd "src" "dst"
	echo "Hello Bob" | mkmsg -b "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /Bob/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "negate"; then
	mkmd "src" "dst"
	echo "Hello Alice" | mkmsg -b "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match ! body /Bob/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "empty body"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /Bob/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_empty "dst/new"
fi

if testcase "malformed body"; then
	mkmd "src" "dst"
	printf 'To: user@example.com\nSubject: foo' | mkmsg "src/new" -
	cat <<-EOF >$CONF
	maildir "src" {
		match body /Bob/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_empty "dst/new"
fi

if testcase "empty message"; then
	mkmd "src" "dst"
	mkmsg -b "src/new" </dev/null
	cat <<-EOF >$CONF
	maildir "src" {
		match body /Bob/ or header "From" /Bob/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_empty "dst/new"
fi

if testcase "destination interpolation"; then
	mkmd "src" "example"
	echo "Hello example" | mkmsg -b "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /example/ move "\0"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "example/new"
fi

if testcase "dry run first line"; then
	mkmd "src" "dst"
	echo "Hello" | mkmsg -b "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /hello/i move "dst"
	}
	EOF
	cat <<EOF >$TMP1
mdsort.conf:2: Body: Hello
                     ^   $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	assert_file $TMP1 $TMP2
fi

if testcase "dry run first line no newline"; then
	mkmd "src" "dst"
	printf 'To: user@example.com\n\nHello' | mkmsg -b "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /hello/i move "dst"
	}
	EOF
	cat <<EOF >$TMP1
mdsort.conf:2: Body: Hello
                     ^   $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	assert_file $TMP1 $TMP2
fi

if testcase "dry run middle line"; then
	mkmd "src" "dst"
	mkmsg -b "src/new" <<-EOF
	To: user@example.com

	Hi,
	Hello hello
	Bye
	EOF
	cat <<-EOF >$CONF
	maildir "src" {
		match body /hello/ move "dst"
	}
	EOF
	cat <<EOF >$TMP1
mdsort.conf:2: Body: Hello hello
                           ^   $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	assert_file $TMP1 $TMP2
fi

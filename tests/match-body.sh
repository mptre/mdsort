if testcase "basic"; then
	echo "Hello Bob" | mkmsg "src/new" - -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /Bob/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	pass
fi

if testcase "negate"; then
	echo "Hello Alice" | mkmsg "src/new" - -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match ! body /Bob/ move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
	pass
fi

if testcase "empty body"; then
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /Bob/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_empty "dst/new"
	pass
fi

if testcase "malformed body"; then
	printf 'To: user@example.com\nSubject: foo' | mkmsg "src/new" -
	cat <<-EOF >$CONF
	maildir "src" {
		match body /Bob/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_empty "dst/new"
	pass
fi

if testcase "empty message"; then
	mkmsg "src/new" - </dev/null
	cat <<-EOF >$CONF
	maildir "src" {
		match body /Bob/ or header "From" /Bob/ move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_empty "dst/new"
	pass
fi

if testcase "destination interpolation"; then
	echo "Hello example" | mkmsg "src/new" - -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /example/ move "\0"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "example/new"
	pass
fi

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
	pass
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
	pass
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
	pass
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
	pass
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
	pass
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
	pass
fi

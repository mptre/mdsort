if testcase "basic"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "Subject" "subject"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match	header "Subject" /.*/ and
			command { "echo" "\0" }
			move "dst"
	}
	EOF
	mdsort - <<-EOF
	subject
	EOF
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "exit non-zero"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match command "false" move "dst"
	}
	EOF
	mdsort
	refute_empty "src/new"
fi

if testcase -t memleak "command not found"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match command "command-not-found" move "dst"
	}
	EOF
	mdsort -e - <<-EOF
	mdsort: command-not-found: No such file or directory
	EOF
	refute_empty "src/new"
fi

if testcase "dry run"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match command "true" move "dst"
	}
	EOF
	mdsort - -- -d <<-EOF
	$(findmsg "src/new") -> dst/new
	EOF
fi

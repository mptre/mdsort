if testcase "add header"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all add-header "Subject" "Hello"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_header "Subject" "Hello" "$(findmsg "src/new")"
fi

if testcase "interpolation"; then
	mkmd "src"
	mkmsg "src/new" -- "Subject" "Re: [mptre/mdsort] Fix bug"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match	header "Subject" /(Re: )?\[[^\]*\] (.*)$/
			add-header "Subject" "\2"
	}
	EOF
	mdsort
	refute_empty "src/new"
	assert_header "Subject" "Fix bug" "$(findmsg "src/new")"
fi

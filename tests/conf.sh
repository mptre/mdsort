testcase "sanity"
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "From" /user@example.com/ move "~/Maildir/Junk"

		match header { "Cc" "To" } /user@example.com/ \
			move "~/Maildir/Junk"

		match header "To" /user@example.com/ and \
			header "Subject" /hello/ move "~/Maildir/Junk"

		match header "To" /user@example.com/ or \
			header "Subject" /hello/ move "~/Maildir/Junk"

		match header "To" /user@example.com/ or \
			! header "Subject" /hello/ or \
			! new move "~/Maildir/Junk"
	}

	maildir "~/Maildir/test2" {
		match header "Received-SPF" /fail/i move "~/Maildir/Junk"
	}
	EOF
	mdsort - -n </dev/null

testcase "empty"
	>$CONF
	mdsort - -n </dev/null

testcase "comment"
	cat <<-EOF >$CONF
	# This is a comment.
	maildir "~/Maildir/test1" {
		match header "From" /user1@example.com/ \
			move "~/Maildir/user1" # comment
		match header "From" /user2@example.com/ move "~/Maildir/user2"
	}
	EOF
	mdsort - -n </dev/null

testcase "escape quote inside string"
	cat <<-EOF >$CONF
	maildir "~/Maildir\"" {}
	EOF
	mdsort - -n </dev/null

testcase "escape slash inside pattern"
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "From" /user\// move "~/Maildir/test2"
	}
	EOF
	mdsort - -n </dev/null

testcase -e "rule must end with newline"
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "From" /./ move "~/Maildir/Junk"}
	EOF
	mdsort - -n <<-EOF
	mdsort.conf:2: syntax error
	EOF

testcase -e "invalid line continuation"
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" \ {
	}
	EOF
	mdsort - -n <<-EOF
	mdsort.conf:1: syntax error
	EOF

testcase -e "missing file"
	mdsort - -n -f missing.conf <<-EOF
	mdsort: missing.conf: No such file or directory
	EOF

testcase -e "invalid regex"
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "From" /(/ move "~/Maildir/test2"
	}
	EOF
	mdsort - -n <<-EOF
	mdsort.conf:2: parentheses not balanced
	EOF

testcase -e "missing header name"
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "" /./ move "~/Maildir/test2"
		match header { "" } /./ move "~/Maildir/test2"
	}
	EOF
	mdsort - -n <<-EOF
	mdsort.conf:2: missing header name
	mdsort.conf:3: missing header name
	EOF

testcase -e "keyword too long"
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		$(randstr 1024 lower)
	}
	EOF
	mdsort - -n <<-EOF
	mdsort.conf:2: keyword too long
	mdsort.conf:2: syntax error
	EOF

testcase -e "string too long"
	cat <<-EOF >$CONF
	maildir "$(randstr 1024 alnum)" {}
	EOF
	mdsort - -n <<-EOF
	mdsort.conf:1: string too long
	mdsort.conf:1: syntax error
	EOF

testcase -e "pattern too long"
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "From" /$(randstr 1024 alnum)/ \
			move "~/Maildir/test2"
	}
	EOF
	mdsort - -n <<-EOF
	mdsort.conf:2: pattern too long
	mdsort.conf:2: syntax error
	EOF

testcase -e "maildir path too long after tilde expansion"
	cat <<-EOF >$CONF
	maildir "~/$(randstr 1012 alnum)" {}
	EOF
	HOME=/home/user mdsort - -n <<-EOF
	mdsort.conf:1: path too long
	EOF

testcase -e "destination path too long after tilde expansion"
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "From" /./ move "~/$(randstr 1012 alnum)"
	}
	EOF
	HOME=/home/user mdsort - -n <<-EOF
	mdsort.conf:2: path too long
	EOF

testcase -e "unknown pattern flag"
	cat <<-EOF >$CONF
	maildir "~/Maildir/INBOX" {
		match header "From" /./z move "~/Maildir/Junk"
		match header "From" /./iz move "~/Maildir/Junk"
	}
	EOF
	mdsort - -n <<-EOF
	mdsort.conf:2: syntax error
	mdsort.conf:3: syntax error
	EOF

testcase -e "and cannot be followed by or"
	cat <<-EOF >$CONF
	maildir "~/Maildir/INBOX" {
		match header "To" /to/ and header "Cc" /cc/ or \
			header "Bcc" /bcc/ move "~/Maildir/Junk"
	}
	EOF
	mdsort - -n <<-EOF
	mdsort.conf:2: and/or are disjoint
	EOF

testcase -e "or cannot be followed by and"
	cat <<-EOF >$CONF
	maildir "~/Maildir/INBOX" {
		match header "To" /to/ or header "Cc" /cc/ and \
			header "Bcc" /bcc/ move "~/Maildir/Junk"
	}
	EOF
	mdsort - -n <<-EOF
	mdsort.conf:2: and/or are disjoint
	EOF

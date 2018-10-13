if testcase "sanity"; then
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

		match body /hello/i move "~/Maildir/Junk"

		match ! body /hello/ move "~/Maildir/Junk"

		match !(new) move "~/Maildir/Junk"

		match (header "To" /user@example.com/ and \
				(new or ! body /hello/)) \
			move "~/Maildir/Junk"

		match header "To" /user1@example.com/ and body /yes/ {
			match header "From" /user2@example.com/ or body /no/ {
				match new move "~/Maildir/Junk"
			}
		}

		match new flag !new
		match new move "~/Maildir/Junk" flag new flag !new

		match all move "~/Maildir/Junk"

		match new discard
	}

	maildir "~/Maildir/test2" {
		match header "Received-SPF" /fail/i move "~/Maildir/Junk"
	}

	stdin {
		match all move "~/Maildir/INBOX"
	}
	EOF
	mdsort - -- -n </dev/null
	pass
fi

if testcase "empty"; then
	>$CONF
	mdsort - -- -n </dev/null
	pass
fi

if testcase "comments"; then
	cat <<-EOF >$CONF
	# This is a comment.
	maildir "~/Maildir/test1" {
		match header "From" /user1@example.com/ \\
			move "~/Maildir/user1" # comment
			# move "~/Maildir/user2"
		# Next rule...
		match header "From" /user2@example.com/ move "~/Maildir/user2"
	}
	EOF
	mdsort - -- -n </dev/null
	pass
fi

if testcase "escape quote inside string"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir\"" {
		match all move "dst"
	}
	EOF
	mdsort - -- -n </dev/null
	pass
fi

if testcase "escape slash inside pattern"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "From" /user\// move "~/Maildir/test2"
	}
	EOF
	mdsort - -- -n </dev/null
	pass
fi

if testcase -e "rule must end with newline"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "From" /./ move "~/Maildir/Junk"}
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:2: syntax error
	EOF
	pass
fi

if testcase -e "empty maildir path"; then
	cat <<-EOF >$CONF
	maildir "" {}
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:1: empty string
	EOF
	pass
fi

if testcase -e "unknown keyword"; then
	cat <<-EOF >$CONF
	noway
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:1: unknown keyword: noway
	EOF
	pass
fi

if testcase -e "invalid line continuation"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" \ {
	}
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:1: syntax error
	EOF
	pass
fi

if testcase "default path"; then
	cat <<-EOF >.mdsort.conf
	maildir "~/Maildir/test1" {
		match new move "~/Maildir/test2"
	}
	EOF
	HOME=$MAILDIR mdsort -D -- -n
	rm -f .mdsort.conf
	pass
fi

if testcase -e "missing file"; then
	mdsort - -- -n -f missing.conf <<-EOF
	mdsort: missing.conf: No such file or directory
	EOF
	pass
fi

if testcase -e "invalid pattern"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match body /(/ move "~/Maildir/test2"
		match header "From" /(/ move "~/Maildir/test2"
	}
	EOF
	mdsort -- -n >/dev/null
	pass
fi

if testcase -e "missing header name"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "" /./ move "~/Maildir/test2"
		match header { "" } /./ move "~/Maildir/test2"
	}
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:2: empty string
	mdsort.conf:3: empty string
	EOF
	pass
fi

if testcase -e "empty move destination"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match new move ""
	}
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:2: empty string
	EOF
	pass
fi

if testcase -e "keyword too long"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		$(randstr $BUFSIZ lower)
	}
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:2: keyword too long
	mdsort.conf:2: syntax error
	EOF
	pass
fi

if testcase -e "string too long"; then
	cat <<-EOF >$CONF
	maildir "$(randstr $BUFSIZ alnum)" {}
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:1: string too long
	mdsort.conf:1: syntax error
	EOF
	pass
fi

if testcase -e "string unterminated"; then
	cat <<-EOF >$CONF
	maildir "
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:1: unterminated string
	mdsort.conf:1: syntax error
	EOF
	pass
fi

if testcase -e "pattern too long"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "From" /$(randstr $BUFSIZ alnum)/ \
			move "~/Maildir/test2"
	}
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:2: pattern too long
	mdsort.conf:2: syntax error
	EOF
	pass
fi

if testcase -e "pattern unterminated"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "From" /
	}
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:2: unterminated pattern
	mdsort.conf:2: syntax error
	EOF
	pass
fi

if testcase -e "maildir path too long after tilde expansion"; then
	cat <<-EOF >$CONF
	maildir "~/$(randstr $((PATH_MAX - 10))  alnum)" {}
	EOF
	HOME=/home/user mdsort - -- -n <<-EOF
	mdsort.conf:1: path too long
	EOF
	pass
fi

if testcase -e "destination path too long after tilde expansion"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "From" /./ \
			move "~/$(randstr $((PATH_MAX - 10)) alnum)"
	}
	EOF
	HOME=/home/user mdsort - -- -n <<-EOF
	mdsort.conf:2: path too long
	EOF
	pass
fi

if testcase -e "missing left-hand expr with and"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/INBOX" {
		match and new move "~/Maildir/Junk"
	}
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:2: syntax error
	EOF
	pass
fi

if testcase -e "missing right-hand expr with and"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/INBOX" {
		match new and move "~/Maildir/Junk"
	}
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:2: syntax error
	EOF
	pass
fi

if testcase -e "empty match block"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/INBOX" {
	}
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:2: empty match block
	EOF
	pass
fi

if testcase -e "empty nested match block"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/INBOX" {
		match new {
		}
	}
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:3: empty nested match block
	EOF
	pass
fi

if testcase -e "missing action"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/INBOX" {
		match new
	}
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:3: missing action
	EOF
	pass
fi

if testcase -e "duplicate move actions"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/INBOX" {
		match new move "~/Maildir/one" move "~/Maildir/two"
	}
	EOF
	mdsort - -- -n <<-EOF
	mdsort.conf:2: move action already defined
	EOF
	pass
fi

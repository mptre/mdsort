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

		match body /hello/if move "~/Maildir/Junk"
		match body /hello/fi move "~/Maildir/Junk"

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

		match header "From" /user@example.com/ break

		match header "From" /user\+(.+)@example.com/ label "\1" \
			move "~/Maildir/Junk"
	}

	maildir "~/Maildir/test2" {
		match header "Received-SPF" /fail/i move "~/Maildir/Junk"
	}

	stdin {
		match all move "~/Maildir/INBOX"
	}
	EOF
	mdsort - -- -n </dev/null
fi

if testcase "empty"; then
	>$CONF
	mdsort - -- -n </dev/null
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
fi

if testcase "escape quote inside string"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir\"" {
		match all move "dst"
	}
	EOF
	mdsort - -- -n </dev/null
fi

if testcase "escape slash inside pattern"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "From" /user\// move "~/Maildir/test2"
	}
	EOF
	mdsort - -- -n </dev/null
fi

if testcase -t leaky "rule must end with newline"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "From" /./ move "~/Maildir/Junk"}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: syntax error
	EOF
fi

if testcase "empty maildir path"; then
	cat <<-EOF >$CONF
	maildir "" {}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:1: empty string
	EOF
fi

if testcase "unknown keyword"; then
	cat <<-EOF >$CONF
	noway
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:1: unknown keyword: noway
	EOF
fi

if testcase -t leaky "invalid line continuation"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" \ {
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:1: syntax error
	EOF
fi

if testcase "default path"; then
	cat <<-EOF >"${WRKDIR}/.mdsort.conf"
	maildir "~/Maildir/test1" {
		match new move "~/Maildir/test2"
	}
	EOF
	HOME=$WRKDIR mdsort -D -- -n
fi

if testcase "missing file"; then
	mdsort -e - -- -n -f missing.conf <<-EOF
	mdsort: missing.conf: No such file or directory
	EOF
fi

if testcase "invalid pattern"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match body /(/ move "~/Maildir/test2"
		match header "From" /(/ move "~/Maildir/test2"
	}
	EOF
	mdsort -e -- -n >/dev/null
fi

if testcase "missing header name"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "" /./ move "~/Maildir/test2"
		match header { "" } /./ move "~/Maildir/test2"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: empty string
	mdsort.conf:3: empty string
	EOF
fi

if testcase "empty move destination"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match new move ""
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: empty string
	EOF
fi

if testcase -t leaky "keyword too long"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		$(genstr $BUFSIZ)
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: keyword too long
	mdsort.conf:2: syntax error
	EOF
fi

if testcase "string too long"; then
	cat <<-EOF >$CONF
	maildir "$(genstr $BUFSIZ)" {}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:1: string too long
	mdsort.conf:1: syntax error
	EOF
fi

if testcase "string unterminated"; then
	cat <<-EOF >$CONF
	maildir "
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:1: unterminated string
	mdsort.conf:1: syntax error
	EOF
fi

if testcase -t leaky "pattern too long"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "From" /$(genstr $BUFSIZ)/ \
			move "~/Maildir/test2"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: pattern too long
	mdsort.conf:2: syntax error
	EOF
fi

if testcase -t leaky "pattern unterminated"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "From" /
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: unterminated pattern
	mdsort.conf:2: syntax error
	EOF
fi

if testcase -t tilde "maildir path too long after tilde expansion"; then
	cat <<-EOF >$CONF
	maildir "~/$(genstr $((PATH_MAX - 10)))" {}
	EOF
	HOME=/home/user mdsort -e - -- -n <<-EOF
	mdsort.conf:1: path too long
	EOF
fi

if testcase -t tilde "destination path too long after tilde expansion"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/test1" {
		match header "From" /./ \
			move "~/$(genstr $((PATH_MAX - 10)))"
	}
	EOF
	HOME=/home/user mdsort -e - -- -n <<-EOF
	mdsort.conf:2: path too long
	EOF
fi

if testcase -t leaky "missing left-hand expr with and"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/INBOX" {
		match and new move "~/Maildir/Junk"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: syntax error
	EOF
fi

if testcase -t leaky "missing right-hand expr with and"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/INBOX" {
		match new and move "~/Maildir/Junk"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: syntax error
	EOF
fi

if testcase "empty match block"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/INBOX" {
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: empty match block
	EOF
fi

if testcase "empty nested match block"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/INBOX" {
		match new {
		}
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:3: empty nested match block
	EOF
fi

if testcase "missing action"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/INBOX" {
		match new
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:3: missing action
	EOF
fi

if testcase "duplicate move actions"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/INBOX" {
		match new move "~/Maildir/one" move "~/Maildir/two"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: move action already defined
	EOF
fi

if testcase "duplicate force pattern flag"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/INBOX" {
		match header "From" /a/f and header "To" /b/f discard
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:3: pattern force flag cannot be used more than once
	EOF
fi

if testcase "lower and upper case flags are mutually exclusive"; then
	cat <<-EOF >$CONF
	maildir "~/Maildir/INBOX" {
		match header "From" /a/lu move "dst"
		match header "From" /a/ul move "dst"
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:2: \`l' and \`u' flags cannot be combined
	mdsort.conf:3: \`l' and \`u' flags cannot be combined
	EOF
fi

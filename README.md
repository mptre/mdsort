# mdsort

The
[mdsort(1)][mdsort]
utility moves messages from one maildir to another according to a set of rules
expressed in the
[mdsort.conf(5)][mdsort.conf]
format.

Example configuration:

```
$ cat ~/.mdsort.conf
maildir "~/Maildir/INBOX" {
	# Move mdsort notifications from GitHub.
	match header "From" /notifications@github.com/ and
		header "Subject" /mdsort/ move "~/Maildir/mdsort"

	# Move messages from OpenBSD mailing lists into dedicated directories.
	match header { "Cc" "To" } /(bugs|misc|ports|tech)@openbsd.org/i
		move "~/Maildir/openbsd-\1"

	# Label messages with the plus portion of the address.
	match header "To" /user\+(.+)@example.com/l label "\1"

	# Conditionally move to a maildir named after the plus portion of the
	# address.
	match header "To" /user\+(.+)@example.com/l and
		isdirectory "~/Maildir/\1" move "~/Maildir/\1"

	# Extract calendar attachments.
	match all attachment {
		match header "Content-Type" |text/calendar|
			exec stdin body "icalendar2calendar"
	}

	# Archive read messages.
	match ! new move "~/Maildir/Archive"
}

maildir { "~/Maildir/Junk" "~/Maildir/Trash" } {
	# Delete messages older than 2 weeks.
	match date > 2 weeks discard
}
```

Invoking
[mdsort(1)][mdsort]
with the `-d` option shows which messages would be moved and why, leaving the
maildir(s) unchanged:

```
$ mdsort -d
/home/anton/Maildir/INBOX/new/1521917775.89020_21.host -> /home/anton/Maildir/mdsort/new
~/.mdsort.conf:3: From: Charlie Root <notifications@github.com>
                                      ^                      $
~/.mdsort.conf:4: Subject: mptre commented on pull request mptre/mdsort#1337
                                                                 ^    $
```

In addition,
[mdsort(1)][mdsort]
can also act as a MDA reading messages from stdin:

```
$ cat ~/.mdsort.conf
stdin {
	match all move "~/Maildir/INBOX"
}
$ mdsort - <message
```

## Installation

### OpenBSD

```
# pkg_add mdsort
```

### From source

The installation prefix defaults to `/usr/local` and can be altered using the
`PREFIX` environment variable when invoking `configure`:

```
$ ./configure
$ make install
```

## License

Copyright (c) 2018-2023 Anton Lindqvist.
Distributed under the MIT license.

[mdsort]: https://www.basename.se/mdsort/
[mdsort.conf]: https://www.basename.se/mdsort/mdsort.conf.5.html

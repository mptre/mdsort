# mdsort

The
[mdsort(1)][mdsort]
utility moves messages from one maildir to another according to a set of rules
expressed in the
[mdsort.conf(5)][mdsort.conf]
format.

Example configuration:

```
maildir "~/Maildir/INBOX" {
	# Move notifications from GitHub.
	match header "From" /notifications@github.com/ move "~/Maildir/GitHub"

	# Move messages from OpenBSD mailing lists into dedicated directories.
	match header { "Cc" "To" } /(bugs|misc|ports|tech)@openbsd.org/i \
		move "~/Maildir/openbsd-\1"

	# Get rid of potential spam.
	match header "Received-SPF" /fail/ or header "X-Spam-Score" /[1-9]/ \
		move "~/Maildir/Spam"

	# Archive read messages.
	match ! new move "~/Maildir/Archive"
}
```

Invoking
[mdsort(1)][mdsort]
with the `-d` option shows which messages would be moved and why, leaving the
maildir(s) unchanged:

```
$ mdsort -d
/home/anton/Maildir/INBOX/new/1521917775.89020_21.host -> /home/anton/Maildir/mdsort/new
From: Charlie Root <notifications@github.com>
                    ^                      $
Subject: mptre commented on pull request mptre/mdsort#1337
                                               ^    $
```

## Installation

### OpenBSD

Available in ports under `mail/mdsort`.

### From source

The installation directory defaults to `/usr/local` and can be altered using the
`PREFIX` environment variable:

```sh
$ ./configure
$ make install
```

## License

Copyright (c) 2018 Anton Lindqvist.
Distributed under the MIT license.

[mdsort]: https://mptre.github.io/mdsort/
[mdsort.conf]: https://mptre.github.io/mdsort/mdsort.conf.5

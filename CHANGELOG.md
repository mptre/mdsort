# v3.0.0 - 2019-02-13

## Changes

- Matchers accepting a pattern now perform matching on line basis, making
  anchors (`^$`) usable.
  (a0907cb)
  (Anton Lindqvist)

- Rename option 'pass' to 'break' for clarity.
  (8bfd8fe)
  (Anton Lindqvist)

- Exit 75 on error when reading messages from stdin.
  (3e32658)
  (Anton Lindqvist)

## News

- Add break action. Especially useful when using nested match blocks since it
  allows more fine-grained rules followed by a fallback:
  (34d96bd)
  (Anton Lindqvist)

  ```
  maildir "~/Maildir/INBOX" {
    # Match messages from "spam" but discard any message addressed to "user".
    # Such messages will be caught by the last rule and moved to the archive.
    match header "From" /spam/ {
      match header "To" /user/ break
      match all move "~/Maildir/Spam"
    }
    match all move "~/Maildir/Archive"
  }
  ```

- Add support for `match date` which matches messages greater or less than a
  given time range.
  (e09a074)
  (Anton Lindqvist)

  ```
  maildir "~/Maildir/Trash" {
    # Delete messages older than 2 weeks.
    match date > 2 weeks discard
  }
  ```

- Add attachment matcher which matches messages with attachments, optionally
  specifying a required content type.
  (2e09f73)
  (Anton Lindqvist)

  ```
  maildir "~/Maildir/INBOX" {
    match attachment /text\/calendar/ move "~/Maildir/Calendar"
  }
  ```

- Annotate each dry run match with the matching config rule.
  (9a071bc)
  (Anton Lindqvist)

  ```
  $ cat ~/.mdsort.conf
  maildir "~/Maildir/INBOX" {
    match header "To" /example/ move "~/Maildir/Archive"
  }
  $ mdsort -d
  ~/.mdsort.conf:2: To: user@example.com
                             ^    $
  ```

- Handle `EXDEV` errors when moving messages.
  (3c0fdf9)
  (Anton Lindqvist)

# v2.0.0 - 2018-10-27

## Deprecations

- Remove `-C` option used to create missing maildirs.
  (c574ecc)
  (Anton Lindqvist)

## News

- Add support for headers with multiple values.
  (beaf3fd)
  (Anton Lindqvist)

- Add support for `match old` which matches messages that has been read but
  later flagged as not read.
  (3ddae2e)
  (Anton Lindqvist)

  ```
  $ cat ~/.mdsort.conf
  maildir "~/Maildir/INBOX" {
    match old move "~/Maildir/Archive"
  }
  ```

- Add `discard` action used to remove matching messages.
  (bd24834)
  (Anton Lindqvist)

  ```
  $ cat ~/.mdsort.conf
  maildir "~/Maildir/INBOX" {
    match header "Spam-Score" /[1-9]/ discard
  }
  ```

- Display all matching subexpressions for a header and body during dry run.
  (77aa29d, 64f61d0)
  (Anton Lindqvist)

  ```
  $ cat ~/.mdsort.conf
  maildir "~/Maildir/INBOX" {
    match header "To" /(example).com/ move "~/Maildir/Archive"
  }
  $ mdsort -d
  To: user@example.com
           ^         $
      user@example.com
           ^     $
  ```

- Add MDA support in which messages are read from stdin.
  (911429e)
  (Anton Lindqvist)

  ```
  $ cat ~/.mdsort.conf
  stdin {
    match header "Spam-Score" /[1-9]/ discard

    match all move "~/Maildir/INBOX"
  }
  $ mdsort - <message
  ```

## Bug fixes

- Remove seen flag when flagging as new.
  (2954399)
  (Anton Lindqvist)

# v1.1.0 - 2018-07-30

## News

- mdsort is now available on OpenBSD
  (a5e1fdc)
  (Anton Lindqvsit)

- Add support for headers spanning multiple lines
  (fb26ffc)
  (Anton Lindqvist)

- Add support for nested `match` blocks
  (eb8a481)
  (Anton Lindqvist)

- Create missing maildir directories by default
  (5dab57e)
  (Anton Lindqvist)

- Add a new action called `flag` used to mark as message as read or not
  (77c9eb5)
  (Anton Lindqvist)

- Add support for `match all` which matches any message.
  (608160a)
  (Anton Lindqvist)

## Bug fixes

- Stop passing non-POSIX `-R` option to grep in tests
  (6d4c264)
  (Anton Lindqvist)

# v1.0.1 - 2018-04-05

## Bug fixes

- Limit usage of malloc.conf options to OpenBSD since the options used have
  different meaning on other BSDs.
  (d908667)
  (Anton Lindqvist)

# v1.0.0 - 2018-04-02

## News

- First somewhat stable release
  (Anton Lindqvist)

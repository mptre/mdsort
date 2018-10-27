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

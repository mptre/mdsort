# 11.5.1 - 2023-02-14

## Bug fixes

- Headers encoded using quoted printable can be both preceded and followed by
  plain text.
  (d8662c4)
  (Anton Lindqvist)

- Prevent out of bounds read by add missing NUL check.
  (d4d1f9b)
  (Anton Lindqvist)

# 11.5.0 - 2022-10-19

## News

- Add add-header action used to add a header to a matched message.
  (c26a614)
  (Anton Lindqvist)

## Bug fixes

- Headers encoded using quoted printable can be followed by plain text.
  (0ef8a31, 4495422)
  (Anton Lindqvist)

# 11.4.0 - 2022-08-30

## News

- Prevent move action to temporary stdin directory.
  (d58b6d7)
  (Anton Lindqvist)

- Add flags action used to add message flags.
  (6a32b03)
  (Josuah Demangeon)

  ```
  stdin {
    match header "Subject" /important/i flags "F" pass
    match all move "~/Maildir/INBOX"
  }
  ```

## Bug fixes

- Preserve white space in header line continuations.
  (2b0a510)
  (Anton Lindqvist)

# 11.3.2 - 2022-02-19

## Bug fixes

- RFC 2047 encodings must be treated as case insensitive.
  (3e43062)
  (Anton Lindqvist)

# v11.3.1 - 2022-02-05

## Bug fixes

- Fix header unfold bug.
  Unfolding a header value consisting of multiple lines must discard any
  preceding whitespace on the subsequent lines.
  (a434b66)
  (Anton Lindqvist)

- Fix dry run UTF-8 output bug.
  (0602db3)
  (Anton Lindqvist)

# v11.3.0 - 2022-01-25

## News

- Handle headers encoded using quoted printable or base64.
  (29e149b)
  (Anton Lindqvist)

# v11.2.0 - 2022-01-09

## News

- Add command matcher, evalutes to true if command exits zero.
  (d350f45)
  (Anton Lindqvist)

  Can for instance be used to detect duplicates:

  ```
  stdin {
    match header "Message-ID" /.*/ and
          command { "grep" "-Rq" "\0" "~/Maildir/INBOX" }
          discard

    match all move "~/Maildir/INBOX"
  }
  ```

# v11.1.0 - 2021-10-28

## News

- Allow interpolation using matches from a specific pattern.
  One long standing annoyance with interpolation is the fact the matches from
  the first matching pattern are favored when there's multiple patterns in play.
  Therefore extend interpolation to optionally accept the pattern to operate on.
  (85298fc)
  (Anton Lindqvist)

  ```
  match	header "X-Spam" /no/ and
  	header "To" /user\+(.+)@example.com/
  	label "\1.1"
  ```

# v11.0.0 - 2021-10-25

## Changes

- Allow break action to be combined with all other actions.
  (c3c2b62)
  (Anton Lindqvist)

- Allow pass action to be combined with all other actions.
  (e4a3168)
  (Anton Lindqvist)

## News

- Add support for defining macros on the command line.
  Useful while performing one shot maildir operations.
  (b318a7f)
  (Josuah Demangeon)

  ```
  $ cat archive.conf
  maildir "${box}" {
    match date > 6 months move "~/Maildir/Archive"
  }
  $ ls | xargs -I{} mdsort -f archive.conf -D box={}
  ```

## Bug fixes

- Fix bison warnings regarding non-terminal types.
  (f1c7e2d)
  (Anton Lindqvist)

# v10.0.1 - 2021-07-30

## Bug fixes

- Make label changes visible to exec actions.
  (9957acd, f074701)
  (Anton Lindqvist)

# v10.0.0 - 2021-07-02

## Changes

- Stop requiring explicit new lines in mdsort.conf.
  (df1ab64)
  (Anton Lindqvist)

  The purpose of requiring explicit new lines after some grammar constructs was
  to simplify error handling as it allowed the current line to be discarded.
  This is a best effort approach allowing the parser to hopefully continue, in
  the hopes of reporting more errors instead of halting.

  Instead, stop requiring explicit new lines and skip the current line while
  encountering an error. This makes line continuations (i.e. backslash)
  redundant.

  Existing configurations must be adjusted as follows:

  ```
  $ cat mdsort.conf
  maildir "~/Maildir/INBOX" {
    match date > 2 weeks \
      move "~/Maildir/Archive"
  }
  $ sed -i -e 's/[[:space:]]*\\//' mdsort.conf
  $ cat mdsort.conf
  maildir "~/Maildir/INBOX" {
    match date > 2 weeks
      move "~/Maildir/Archive"
  }
  ```

## News

- Support more than one path per maildir declaration.
  Allows the same rules to be applied to multiple maildirs.
  (55f34f2)
  (Anton Lindqvist)

  ```
  maildir { "~/Maildir/Junk" "~/Maildir/Trash" } {
    match date modified > 2 weeks discard
  }
  ```

## Bug fixes

- Do not confuse macros and date scalars.
  (3cf1e2e)
  (Anton Lindqvist)

# v9.1.0 - 2021-04-29

## News

- Add support for decoding of quoted printable message bodies.
  (61b9408)
  (Anton Lindqvist)

# v9.0.0 - 2021-01-22

## Changes

- Remove message blacklist functionality used during maildir traversal.
  (3d8b345)
  (Anton Lindqvist)

- Remove support for the `sync` option used with `maildir`.
  Thus, always making use of fsync(2).
  (9d78550)
  (Anton Lindqvist)

## News

- Add `isdirectory` matcher, evalutes to true if the path refers to an existing
  directory.
  Allows move actions to be made conditional based on the destination maildir.
  (16a53aa)
  (Anton Lindqvist)

  ```
  match header "To" /user\+(.+)@example.com/l and \
    isdirectory "~/Maildir/\1" move "~/Maildir/\1"
  ```

- Best effort reduction of side effects when moving a message fails.
  (8eae8da, c58d0bb)
  (Anton Lindqvist)

## Bug fixes

- Check for presence of TAILQ_END during configure.
  This macro is not present on macOS, use the compat version instead.
  (b89983a)
  (Anton Lindqvist)

# v8.0.0 - 2020-12-16

## Changes

- Stop leaking interpolation matches between rules.
  (ddad961)
  (Anton Lindqvist)

- Remove force pattern flag.
  (1491385)
  (Anton Lindqvist)

## Bug fixes

- Skip mbox separator line.
  (c6002fb)
  (Anton Lindqvist)

- Support many label actions in conjunction with pass.
  (514bb3e)
  (Anton Lindqvist)

# v7.1.0 - 2020-12-03

## News

- Add support for pre defined macros in action context.
  The only available macro at this point is path which expands to the path of
  the matched message.
  (5350ec4)
  (Anton Lindqvist)

  ```
  maildir "~/Maildir/INBOX" {
    match all exec { "echo" "${path}" }
  }
  ```

- Add support for matching each attachment in a message.
  Its intended use is to extract calendar attachments:
  (209f9e5)
  (Anton Lindqvist)

  ```
  maildir "~/Maildir/INBOX" {
    match all attachment {
      match header "Content-Type" |text/calendar| \
        exec stdin body "icalendar2calendar"
    }
  }
  ```

## Bug fixes

- Fix label concatenation bug.
  (ba97d74)
  (Anton Lindqvist)

# v7.0.0 - 2020-11-05

## Changes

- Rework attachment grammar.
  (b236c0e)
  (Anton Lindqvist)

## Bug fixes

- Initialize file descriptor field for attachments.
  (3737a35)
  (Anton Lindqvist, Giovanni Simoni)

# v6.0.1 - 2020-09-14

## Bug fixes

- Fix null dereference when macro is absent.
  (87bc011)
  (Anton Lindqvist)

# v6.0.0 - 2020-09-12

## Changes

- Unfold header values into a single space separated line, according to RFC
  2822.
  (518a12f)
  (Anton Lindqvist)

## News

- Add support for macros to configuration.
  Macros allows repetitive parts of the configuration to be defined once.
  They can be interpolated inside any string in the configuration.
  (3f4d931)
  (Anton Lindqvist)

  ```
  prefix = "~/Maildir"
  inbox = "${prefix}/INBOX"

  maildir "${inbox}" {
    match date > 1 week move "${prefix}/Trash"
  }
  ```

# v5.3.0 - 2020-07-11

## News

- Add exec action which can be used to execute an arbitrary command for each
  matched message. Multiple arguments can be expressed using a string list.
  (73de2f8)
  (Anton Lindqvist)

# v5.2.3 - 2020-06-17

## Bug fixes

- Do not limit base64 decode to bodies of attachments as a message without
  attachments might be encoded as well.
  (4032953)
  (Anton Lindqvist)

# v5.2.2 - 2020-03-17

## Bug fixes

- Check for presence of `TAILQ_FOREACH_SAFE`.
  This macro is absent on many Linux distributions.
  (c447f39)
  (Anton Lindqvist)

# v5.2.1 - 2020-03-17

## Bug fixes

- Correct dangling `pass` action logic.
  (d410dcb)
  (Anton Lindqvist)

## News

- Rename `Makefile.inc` to `config.mk`.
  (2346012)
  (Anton Lindqvist)

# v5.2.0 - 2020-02-01

## Bug fixes

- Reject configurations consisting of only a `pass` action which is a NOP
  anyway, see example below.
  Performing a dry run would cause an assertion to trigger.
  (2ea995e)
  (Anton Lindqvist)

  ```
  maildir "~/Maildir/INBOX" {
    match header "From" /example/ pass
  }
  ```

- Fix potential write of out bounds.
  A path or label constructed using interpolation could cause a heap write out
  of bounds.
  (115abfc)
  (Anton Lindqvist)

# v5.1.0 - 2020-01-20

## Bug fixes

- Fix use-after-free.
  Only triggered by obscure configurations like the following one.
  (0b5ef03)
  (Anton Lindqvist)

  ```
  maildir "src" {
    match all flag new flag !new flag new
  }
  ```

# v5.0.0 - 2020-01-04

## Changes

- Remove undocumented and implicit duplicate label handling.
  (a92ce87)
  (Anton Lindqvist)

## Bug fixes

- If either the `new` or `cur` directory was missing from a maildir, `mdsort`
  would exit zero as opposed of non-zero.
  (f68df64)
  (Anton Lindqvist)

- Blacklist messages moved to the maildir currently being traversed in order to
  not operate on the same message more than once.
  (0cb06ee)
  (Anton Lindqvist)

# v4.3.0 - 2019-12-14

## News

- Support for using any character as the pattern delimiter.
  By default, `/` is the expected pattern delimiter.
  Instead support any character as the pattern delimiter, similar to how
  `sed(1)` behaves.
  This is especially useful when one wants to match a literal `/` inside a
  pattern; removing the need to escape it.
  (dc76344)
  (Anton Lindqvist)

  ```
  $ cat mdsort.conf
  maildir "~/Maildir/INBOX" {
    match header "Subject" |/sys/| label "sys"
  }
  ```

# v4.2.0 - 2019-09-14

## News

- Call `fsync(2)` after writing out messages read from stdin.
  The same behavior can also optionally be enabled for an ordinary maildir.
  (7b0822f)
  (Anton Lindqvist)

- The date matcher can now match based on either the access, modified or created
  file timestamp.
  By default, the date header is still used.
  (07cdf17)
  (Anton Lindqvist)

  ```
  maildir "~/Maildir/Trash" {
    match date created > 2 weeks discard
  }
  ```

## Bug fixes

- Using a date condition followed by another condition that produces matches to
  be used during interpolation did not work since the matches from the date
  condition erroneously was favored.
  (f464cd1)
  (Anton Lindqvist)

# v4.1.0 - 2019-08-21

## News

- Parse alternative body representations, favoring plain text over HTML.
  (90a3cf2)
  (Anton Lindqvist)

- Infer `INSTALL_MAN` from `INSTALL` if not present.
  (badd5bc)
  (Anton Lindqvist)

- Lower or upper case a matched string used during interpolation.
  (8aa5be6)
  (Anton Lindqvist)

## Bug fixes

- Add missing space for NUL-terminator when using `NAME_MAX` sized buffers.
  (9a59987)
  (Anton Lindqvist)

# v4.0.0 - 2019-07-04

## Changes

- Make attachment grammar more flexible by reusing existing grammar constructs.
  It's now possible to match any header(s) or body of an attachment.
  (c72a5e8)
  (Anton Lindqvist)

  ```
  maildir "~/Maildir/INBOX" {
    match attachment header "Content-Type" /text\/calendar/ \
      move "~/Maildir/Calendar"
  }
  ```

## News

- Add reject action used to reject a message read from stdin by letting
  `mdsort(1)` exit non-zero.
  (fcac5e6)
  (Anton Lindqvist)

  ```
  stdin {
    match header "Spam-Score" /[1-9]/ reject
  }
  ```

- Add pass action used to continue evaluation of the current block of
  rules up to the next matching rule.
  (ade4316)
  (Anton Lindqvist)

  ```
  maildir "~/Maildir/INBOX" {
    match header "From" /example/ label "\1" pass
    match all move "~/Maildir/Archive"
  }
  ```

# v3.1.0 - 2019-04-22

## Changes

- Honor `BINDIR` and `MANDIR` during configure and install.
  (3354bd0)
  (Anton Lindqvist)

- Exit non-zero on path interpolation failure.
  (ddc85f4)
  (Anton Lindqvist)

## News

- Add label action used to append labels to the `X-Label` header.
  (a8cf5c1)
  (Anton Lindqvist)

  ```
  maildir "~/Maildir/INBOX" {
    # Label messages with the plus portion of the address.
    match header "From" /(user|admin)@example.com/ and \
      header "To" /user\+(.+)@example.com/ label "\1"
  }
  ```

- Optionally specify the pattern to use during interpolation, useful when a rule
  consists of multiple patterns.
  (0e1ec8b)
  (Anton Lindqvist)

  ```
  maildir "~/Maildir/INBOX" {
    match header "List-ID" /tech.openbsd.org/ {
      match "Subject" /(csh|ksh)/f label "\1"
    }
  }
  ```

## Bug fixes

- Add fallback for unknown file types. Some file systems like XFS does not
  return the file type from `readdir(3)`.
  (3c3d35f)
  (Anton Lindqvist)

- Let `mktime(3)` figure out if DST is in effect.
  (3cd7378)
  (Anton Lindqvist)

# v3.0.0 - 2019-02-13

## Changes

- Matchers accepting a pattern now perform matching on line basis, making
  anchors (`^$`) usable.
  (a0907cb)
  (Anton Lindqvist)

- Rename option `pass` to `break` for clarity.
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

## Changes

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
  (Anton Lindqvist)

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

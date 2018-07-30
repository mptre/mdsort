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

- First somewhat stable release
  (Anton Lindqvist)

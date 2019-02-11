# By default, all maildirs used during testing are rooted in TMPDIR. All test
# cases in this file tries to use a destination maildir rooted in the current
# directory since it will hopefully reside on a different file system.

xmkmd() {
	XDIR="$(env "TMPDIR=${PWD}" mktemp -d -t mdsort.XXXXXX)"
	(WRKDIR=$XDIR mkmd "$@")
}

if testcase "move from disk"; then
	mkmd "src"
	mkmsg "src/new"
	xmkmd "dst"
	cat <<-EOF >$CONF
	maildir "src" {
		match all move "${XDIR}/dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "${XDIR}/dst/new"
fi
rm -rf "$XDIR"

if testcase "move from stdin"; then
	xmkmd "dst"
	cat <<-EOF >$CONF
	stdin {
		match all move "${XDIR}/dst"
	}
	EOF
	cat <<-EOF >$TMP1
	From: standard@input.com

	EOF
	mdsort -- - <$TMP1
	refute_empty "${XDIR}/dst/new"
	assert_file $XDIR/dst/new/* $TMP1
fi
rm -rf "$XDIR"

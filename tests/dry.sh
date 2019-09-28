if testcase "match body many subexpressions"; then
	mkmd "src"
	mkmsg -b "src/new" <<-EOF
	foo bar
	EOF
	cat <<-EOF >$CONF
	maildir "src" {
		match body /foo (bar)?/ move "dst"
	}
	EOF
	cat <<EOF >$TMP1
mdsort.conf:2: Body: foo bar
                     ^     $
                     foo bar
                         ^ $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	assert_file $TMP1 $TMP2
fi

if testcase "match many headers and body"; then
	mkmd "src"
	echo "Hello!" | mkmsg -b "src/new" -- \
		"Cc" "admin@example.com" "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "Cc" /admin/ and header "To" /user/ \
			and body /hello/i move "dst"
	}
	EOF
	cat <<EOF >$TMP1
mdsort.conf:2: Cc: admin@example.com
                   ^   $
mdsort.conf:2: To: user@example.com
                   ^  $
mdsort.conf:2: Body: Hello!
                     ^   $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	assert_file $TMP1 $TMP2
fi

if testcase "matches from previous evaluations are discarded"; then
	mkmd "src"
	echo "Bye!" | mkmsg -b "src/new" -- "To" "user@example.com"
	echo "Hello!" | mkmsg -b "src/new" -- "Cc" "admin@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match (header "Cc" /admin/ or header "To" /user/) and \
			body /hello/i move "dst"
	}
	EOF
	cat <<EOF >$TMP1
mdsort.conf:2: Cc: admin@example.com
                   ^   $
mdsort.conf:2: Body: Hello!
                     ^   $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	assert_file $TMP1 $TMP2
fi

if testcase "matches from previous evaluations are discarded, inverted"; then
	mkmd "src"
	echo "Bye!" | mkmsg -b "src/new" -- "Cc" "admin@example.com"
	echo "Hello!" | mkmsg -b "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match (header "Cc" /admin/ or header "To" /user/) and \
			body /hello/i move "dst"
	}
	EOF
	cat <<EOF >$TMP1
mdsort.conf:2: To: user@example.com
                   ^  $
mdsort.conf:2: Body: Hello!
                     ^   $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	assert_file $TMP1 $TMP2
fi

if testcase "match nested rules"; then
	mkmd "src"
	echo "Bye!" | mkmsg -b "src/new" -- "Cc" "admin@example.com"
	echo "Hello!" | mkmsg -b "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header { "Cc" "To" } /example/ {
			match body /hello/i move "dst"

			match body /gutentag/i move "dst"
		}
	}
	EOF
	cat <<EOF >$TMP1
mdsort.conf:2: To: user@example.com
                        ^     $
mdsort.conf:3: Body: Hello!
                     ^   $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	assert_file $TMP1 $TMP2
fi

if testcase "single character"; then
	mkmd "src"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /./ move "dst"
	}
	EOF
	cat <<EOF >$TMP1
mdsort.conf:2: To: user@example.com
                   ^$
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	assert_file $TMP1 $TMP2
fi

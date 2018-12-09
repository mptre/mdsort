if testcase "match body many subexpressions"; then
	mkmd "src" "dst"
	mkmsg -b "src/new" <<-EOF
	foo bar
	EOF
	cat <<-EOF >$CONF
	maildir "src" {
		match body /foo (bar)?/ move "dst"
	}
	EOF
	cat <<EOF >$TMP1
Body: foo bar
      ^     $
      foo bar
          ^ $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	fcmp $TMP1 $TMP2 && pass
fi

if testcase "match many headers and body"; then
	mkmd "src" "dst"
	echo "Hello!" | mkmsg -b "src/new" -- \
		"Cc" "admin@example.com" "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "Cc" /admin/ and header "To" /user/ \
			and body /hello/i move "dst"
	}
	EOF
	cat <<EOF >$TMP1
Cc: admin@example.com
    ^   $
To: user@example.com
    ^  $
Body: Hello!
      ^   $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	fcmp $TMP1 $TMP2 && pass
fi

if testcase "matches from previous evaluations are discarded"; then
	mkmd "src" "dst"
	echo "Bye!" | mkmsg -b "src/new" -- "To" "user@example.com"
	echo "Hello!" | mkmsg -b "src/new" -- "Cc" "admin@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match (header "Cc" /admin/ or header "To" /user/) and \
			body /hello/i move "dst"
	}
	EOF
	cat <<EOF >$TMP1
Cc: admin@example.com
    ^   $
Body: Hello!
      ^   $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	fcmp $TMP1 $TMP2 && pass
fi

if testcase "matches from previous evaluations are discarded, inverted"; then
	mkmd "src" "dst"
	echo "Bye!" | mkmsg -b "src/new" -- "Cc" "admin@example.com"
	echo "Hello!" | mkmsg -b "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match (header "Cc" /admin/ or header "To" /user/) and \
			body /hello/i move "dst"
	}
	EOF
	cat <<EOF >$TMP1
To: user@example.com
    ^  $
Body: Hello!
      ^   $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	fcmp $TMP1 $TMP2 && pass
fi

if testcase "match nested rules"; then
	mkmd "src" "dst"
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
To: user@example.com
         ^     $
Body: Hello!
      ^   $
EOF
	mdsort -- -d | tail -n +2 >$TMP2
	fcmp $TMP1 $TMP2 && pass
fi

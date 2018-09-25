if testcase "match header on first line"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /example.com/ move "dst"
	}
	EOF
	cat <<EOF >$TMP1
To: user@example.com
         ^         $
EOF
	mdsort -d | tail -n +2 >$TMP2
	fcmp $TMP1 $TMP2 && pass
fi

if testcase "match header on middle line"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" \
		"$(printf 'admin@a.com,\n\tuser@a.com,\n\tno-reply@a.com')"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /user/ move "dst"
	}
	EOF
	cat <<EOF >$TMP1
To: admin@a.com,user@a.com,no-reply@a.com
                ^  $
EOF
	mdsort -d | tail -n +2 >$TMP2
	fcmp $TMP1 $TMP2 && pass
fi

if testcase "match header on last line"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" \
		"$(printf 'admin@example.com,\n\tuser@example.com')"
	cat <<-EOF >$CONF
	maildir "src" {
		match header "To" /user/ move "dst"
	}
	EOF
	cat <<EOF >$TMP1
To: admin@example.com,user@example.com
                      ^  $
EOF
	mdsort -d | tail -n +2 >$TMP2
	fcmp $TMP1 $TMP2 && pass
fi

if testcase "match header negate"; then
	mkmd "src" "dst"
	mkmsg "src/new" -- "To" "admin@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match ! header "To" /user/ move "dst"
	}
	EOF
	mdsort -d >$TMP1
	grep -q '^src/new.* -> dst/new$' $TMP1 || fail "expected move line"
	pass
fi

if testcase "match body on first line"; then
	mkmd "src" "dst"
	echo "Hello" | mkmsg -b "src/new" -- "To" "user@example.com"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /hello/i move "dst"
	}
	EOF
	cat <<-EOF >$TMP1
	Hello
	^   $
	EOF
	mdsort -d | tail -n +2 >$TMP2
	fcmp $TMP1 $TMP2 && pass
fi

if testcase "match body on first line no newline"; then
	mkmd "src" "dst"
	printf 'To: user@example.com\n\nHello' | mkmsg -b "src/new"
	cat <<-EOF >$CONF
	maildir "src" {
		match body /hello/i move "dst"
	}
	EOF
	cat <<-EOF >$TMP1
	Hello
	^   $
	EOF
	mdsort -d | tail -n +2 >$TMP2
	fcmp $TMP1 $TMP2 && pass
fi

if testcase "match body on middle line"; then
	mkmd "src" "dst"
	mkmsg -b "src/new" <<-EOF
	To: user@example.com

	Hi,
	Hello hello
	Bye
	EOF
	cat <<-EOF >$CONF
	maildir "src" {
		match body /hello/ move "dst"
	}
	EOF
	cat <<-EOF >$TMP1
	Hello hello
	      ^   $
	EOF
	mdsort -d | tail -n +2 >$TMP2
	fcmp $TMP1 $TMP2 && pass
fi

if testcase "match body spanning multiple lines"; then
	mkmd "src" "dst"
	mkmsg -b "src/new" -- "To" "user@example.com" <<-EOF
	He
	llo
	EOF
	cat <<-EOF >$CONF
	maildir "src" {
		match body /h.*/i move "dst"
	}
	EOF
	mdsort -d | tail -n +2 >$TMP1
	fcmp - $TMP1 </dev/null && pass
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
Hello!
^   $
EOF
	mdsort -d | tail -n +2 >$TMP2
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
Hello!
^   $
EOF
	mdsort -d | tail -n +2 >$TMP2
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
Hello!
^   $
EOF
	mdsort -d | tail -n +2 >$TMP2
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
Hello!
^   $
EOF
	mdsort -d | tail -n +2 >$TMP2
	fcmp $TMP1 $TMP2 && pass
fi

if testcase "unsupported actions"; then
	mkmd "src"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all attachment {
			match all move "dst"
		}
	}
	EOF
	mdsort -e - -- -n <<-EOF
	mdsort.conf:4: attachment cannot be combined with action(s)
	EOF
fi

if testcase "multiple actions"; then
	mkmd "src" "dst"
	mkmsg -A "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all attachment {
			match all exec "true"
		} move "dst"
	}
	EOF
	mdsort
	assert_empty "src/new"
	refute_empty "dst/new"
fi

if testcase "action exec"; then
	mkmd "src"
	mkmsg -A "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all attachment {
			match body /.*/ exec { "echo" "\\0" }
		}
	}
	EOF
	mdsort - -- <<-EOF
	First attachment.
	Second attachment.
	EOF
fi

if testcase "action exec stdin"; then
	mkmd "src"
	mkmsg -A "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all attachment {
			match body /.*/ exec stdin "cat"
		}
	}
	EOF
	mdsort - -- <<-EOF
	Content-Type: text/plain

	First attachment.
	Content-Type: text/calendar

	Second attachment.
	EOF
fi

if testcase "action exec stdin body"; then
	mkmd "src"
	mkmsg -A "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all attachment {
			match all exec stdin body "cat"
		}
	}
	EOF
	mdsort - -- <<-EOF
	First attachment.
	Second attachment.
	EOF
fi

if testcase "interpolation"; then
	mkmd "src"
	mkmsg -A "src/new" -- "To" "user@example.com"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match header "To" /.*/ attachment {
			match header "Content-Type" /.*/ exec { "echo" "\\0" }
		}
	}
	EOF
	mdsort - <<-EOF
	text/plain
	text/calendar
	EOF
fi

if testcase "no attachments"; then
	mkmd "src"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all attachment {
			match all exec "true"
		}
	}
	EOF
	mdsort
fi

if testcase "no matching attachments"; then
	mkmd "src"
	mkmsg -A "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all attachment {
			match body /nein/ exec "true"
		}
	}
	EOF
	mdsort
fi

if testcase "dry run"; then
	mkmd "src"
	mkmsg -A "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all attachment {
			match header "Content-Type" /.*/ exec "cat"
		}
	}
	EOF
	mdsort - -- -d <<EOF
$(findmsg "src/new") -> <exec>
mdsort.conf:3: Content-Type: text/plain
                             ^        $
$(findmsg "src/new") -> <exec>
mdsort.conf:3: Content-Type: text/calendar
                             ^           $
EOF
fi

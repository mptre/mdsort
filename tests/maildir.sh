if testcase "many maildirs"; then
	mkmd "src1" "src2" "dst"
	mkmsg "src1/new"
	mkmsg "src2/new"
	cat <<-EOF >"$CONF"
	maildir { "src1" "src2" } {
		match all move "dst"
	}
	EOF
	mdsort
	assert_empty "src1/new"
	assert_empty "src2/new"
	refute_empty "dst/new"
fi

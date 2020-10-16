if testcase "many move actions"; then
	mkmd "src" "dst1" "dst2" "dst3"
	mkmsg "src/new"
	cat <<-EOF >"$CONF"
	maildir "src" {
		match all move "dst1" move "dst2" move "dst3"
	}
	EOF
	mdsort
	assert_empty "src/new"
	assert_empty "dst1/new"
	assert_empty "dst2/new"
	refute_empty "dst3/new"
fi

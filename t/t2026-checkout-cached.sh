#!/bin/sh

test_description='checkout --cached <pathspec>'

. ./test-lib.sh

test_expect_success 'checkout --cached <pathspec>' '
	echo 1 >file1 &&
	echo 2 >file2 &&
	git add file1 file2 &&
	test_tick &&
	git commit -m files &&
	git rm file2 &&
	echo 3 >file3 &&
	echo 4 >file1 &&
	git add file1 file3 &&
	git checkout --cached HEAD -- file1 file2 &&
	test_must_fail git diff --quiet &&

	cat >expect <<-\EOF &&
	diff --git a/file1 b/file1
	index d00491f..b8626c4 100644
	--- a/file1
	+++ b/file1
	@@ -1 +1 @@
	-1
	+4
	diff --git a/file2 b/file2
	deleted file mode 100644
	index 0cfbf08..0000000
	--- a/file2
	+++ /dev/null
	@@ -1 +0,0 @@
	-2
	EOF
	git diff >actual &&
	test_cmp expect actual &&

	cat >expect <<-\EOF &&
	diff --git a/file3 b/file3
	new file mode 100644
	index 0000000..00750ed
	--- /dev/null
	+++ b/file3
	@@ -0,0 +1 @@
	+3
	EOF
	git diff --cached >actual &&
	test_cmp expect actual
'

test_expect_success 'checking out an unmodified path is a no-op' '
	git reset --hard &&
	git checkout --cached HEAD -- file1 &&
	git diff-files --exit-code &&
	git diff-index --cached --exit-code HEAD
'

test_expect_success 'checking out specific path that is unmerged' '
	test_commit file3 file3 &&
	git rm --cached file2 &&
	echo 1234 >file2 &&
	F1=$(git rev-parse HEAD:file1) &&
	F2=$(git rev-parse HEAD:file2) &&
	F3=$(git rev-parse HEAD:file3) &&
	{
		echo "100644 $F1 1	file2" &&
		echo "100644 $F2 2	file2" &&
		echo "100644 $F3 3	file2"
	} | git update-index --index-info &&
	git ls-files -u &&
	git checkout --cached HEAD file2 &&
	test_must_fail git diff --quiet &&
	git diff-index --exit-code --cached HEAD
'

test_expect_success '--cached without --no-overlay does not remove entry from index' '
	test_must_fail git checkout --cached HEAD^ file3 &&
	git ls-files --error-unmatch -- file3
'

test_expect_success 'file is removed from the index with --no-overlay' '
	git checkout --cached --no-overlay HEAD^ file3 &&
	test_path_is_file file3 &&
	test_must_fail git ls-files --error-unmatch -- file3
'

test_expect_success 'test checkout --cached --no-overlay at given paths' '
	mkdir sub &&
	>sub/file1 &&
	>sub/file2 &&
	git update-index --add sub/file1 sub/file2 &&
	T=$(git write-tree) &&
	git checkout --cached --no-overlay HEAD sub/file2 &&
	test_must_fail git diff --quiet &&
	U=$(git write-tree) &&
	echo "$T" &&
	echo "$U" &&
	test_must_fail git diff-index --cached --exit-code "$T" &&
	test "$T" != "$U"
'

test_done

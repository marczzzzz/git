#!/bin/sh

test_description='backup-log'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit initial &&
	mkdir sub
'

test_expect_success 'backup-log add new item' '
	ID=$(git rev-parse HEAD:./initial.t) &&
	test_tick &&
	git -C sub backup-log --id=index update foo $ZERO_OID $ID &&
	echo "$ZERO_OID $ID $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $test_tick -0700	foo" >expected &&
	test_cmp expected .git/index.bkl
'

test_expect_success 'add writes backup log' '
	test_tick &&
	echo update >>initial.t &&
	OLD=$(git rev-parse :./initial.t) &&
	NEW=$(git hash-object initial.t) &&
	git -c core.backupLog=true add initial.t &&
	echo "$OLD $NEW $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $test_tick -0700	initial.t" >expected &&
	tail -n1 .git/index.bkl >actual &&
	test_cmp expected actual
'

test_expect_success 'update-index --keep-backup writes backup log' '
	test_tick &&
	echo update-index >>initial.t &&
	OLD=$(git rev-parse :./initial.t) &&
	git -c core.backupLog=true update-index --keep-backup initial.t &&
	NEW=$(git hash-object initial.t) &&
	echo "$OLD $NEW $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $test_tick -0700	initial.t" >expected &&
	tail -n1 .git/index.bkl >actual &&
	test_cmp expected actual
'

test_expect_success 'commit -a writes backup log' '
	test_tick &&
	echo update-again >>initial.t &&
	OLD=$(git rev-parse :./initial.t) &&
	NEW=$(git hash-object initial.t) &&
	git -c core.backupLog=true commit -am update-again &&
	echo "$OLD $NEW $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $test_tick -0700	initial.t" >expected &&
	tail -n1 .git/index.bkl >actual &&
	test_cmp expected actual
'

test_expect_success 'partial commit writes backup log' '
	rm -f .git/index.bkl &&
	test_tick &&
	echo rising >>new-file &&
	git add -N new-file &&
	OLD=$EMPTY_BLOB &&
	NEW=$(git hash-object new-file) &&
	git -c core.backupLog=true commit -m update-once-more -- new-file initial.t &&
	echo "$OLD $NEW $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $test_tick -0700	new-file" >expected &&
	# expect file deletion to not log anything &&
	test_cmp expected .git/index.bkl
'

test_expect_success 'apply --cached writes backup log' '
	rm -f .git/index.bkl &&
	git reset --hard &&
	test_tick &&
	echo to-be-deleted >to-be-deleted &&
	echo to-be-modified >to-be-modified &&
	OLD_M=$(git hash-object to-be-modified) &&
	git add . &&
	git commit -m first &&
	rm to-be-deleted &&
	echo modified >>to-be-modified &&
	NEW_M=$(git hash-object to-be-modified) &&
	OLD_A=$ZERO_OID &&
	echo to-be-added >to-be-added &&
	NEW_A=$(git hash-object to-be-added) &&
	git add . &&
	git commit -m second &&
	git diff HEAD^ HEAD >patch &&
	git reset --hard HEAD^ &&
	git -c core.backupLog=true apply --cached --keep-backup patch &&
	echo "$OLD_A $NEW_A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $test_tick -0700	to-be-added" >expected &&
	echo "$OLD_M $NEW_M $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $test_tick -0700	to-be-modified" >>expected &&
	test_cmp expected .git/index.bkl
'

test_expect_success 'backup-log cat' '
	OLD=$(git rev-parse :./initial.t) &&
	echo update >>initial.t &&
	test_tick &&
	git -c core.backupLog=true add initial.t &&
	NEW=$(git rev-parse :./initial.t) &&
	git backup-log --id=index cat --before --hash $test_tick initial.t >actual &&
	echo $OLD >expected &&
	test_cmp expected actual &&
	git backup-log --id=index cat --hash $test_tick initial.t >actual &&
	echo $NEW >expected &&
	test_cmp expected actual &&
	git backup-log --id=index cat $test_tick initial.t >actual &&
	git cat-file blob $NEW >expected &&
	test_cmp expected actual
'

test_expect_success 'backup-log diff' '
	echo first >initial.t &&
	git add initial.t &&
	echo second >initial.t &&
	test_tick &&
	git -c core.backupLog=true add initial.t &&
	git backup-log --id=index diff $test_tick >actual &&
	cat >expected <<-\EOF &&
	diff --git a/initial.t b/initial.t
	index 9c59e24..e019be0 100644
	--- a/initial.t
	+++ b/initial.t
	@@ -1 +1 @@
	-first
	+second
	EOF
	test_cmp expected actual &&
	git backup-log --id=index diff --stat $test_tick >stat.actual &&
	cat >stat.expected <<-\EOF &&
	 initial.t | 2 +-
	 1 file changed, 1 insertion(+), 1 deletion(-)
	EOF
	test_cmp stat.expected stat.actual
'

test_expect_success 'backup-log log' '
	rm -f .git/index.bkl &&
	echo first >initial.t &&
	git add initial.t &&
	echo second >initial.t &&
	test_tick &&
	git -c core.backupLog=true add initial.t &&
	echo third >initial.t &&
	# note, same tick!
	git -c core.backupLog=true add initial.t &&
	test_tick &&
	echo forth >initial.t &&
	git -c core.backupLog=true add initial.t &&
	git backup-log --id=index log --stat --date=iso >actual &&
	cat >expected <<-\EOF &&
	Change-Id: 1112912593
	Date: 2005-04-07 15:23:13 -0700

	--
	 initial.t | 2 +-
	 1 file changed, 1 insertion(+), 1 deletion(-)

	Change-Id: 1112912533
	Date: 2005-04-07 15:22:13 -0700

	--
	 initial.t | 2 +-
	 initial.t | 2 +-
	 2 files changed, 2 insertions(+), 2 deletions(-)

	EOF
	test_cmp expected actual
'

test_expect_success 'config --edit makes a backup' '
	cat >edit.sh <<-EOF &&
	#!$SHELL_PATH
	echo ";Edited" >>"\$1"
	EOF
	chmod +x edit.sh &&
	OLD=$(git hash-object .git/config) &&
	EDITOR=./edit.sh git -c core.backupLog=true config --edit &&
	NEW=$(git hash-object .git/config) &&
	grep config .git/common/gitdir.bkl &&
	grep ^$OLD .git/common/gitdir.bkl &&
	grep $NEW .git/common/gitdir.bkl
'

test_expect_success 'deleted reflog is kept' '
	git checkout -b foo &&
	git commit -am everything-else &&
	test_commit two &&
	test_commit three &&
	git checkout -f master &&
	OLD=$(git hash-object .git/logs/refs/heads/foo) &&
	git -c core.backupLog=true branch -D foo &&
	grep logs/refs/heads/foo .git/common/gitdir.bkl &&
	grep ^$OLD .git/common/gitdir.bkl
'

test_expect_success 'overwritten ignored file is backed up' '
	git init overwrite-ignore &&
	(
		cd overwrite-ignore &&
		echo ignored-overwritten >ignored &&
		NEW=$(git hash-object ignored) &&
		git add ignored &&
		git commit -m ignored &&
		git rm --cached ignored &&
		echo /ignored >.gitignore &&
		git add .gitignore &&
		git commit -m first-commit-no-ignored &&
		echo precious >ignored &&
		OLD=$(git hash-object ignored) &&
		test_tick &&
		git -c core.backupLog=true checkout --detach HEAD^ &&
		echo "$OLD $NEW $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $test_tick -0700	ignored" >expected &&
		test_cmp expected .git/worktree.bkl
	)
'

test_expect_success 'overwritten out-of-date file is backed up' '
	git init overwrite-outofdate &&
	(
		cd overwrite-outofdate &&
		test_commit haha &&
		NEW=$(git hash-object haha.t) &&
		echo bad >>haha.t &&
		OLD=$(git hash-object haha.t) &&
		git -c core.backupLog reset --hard &&
		echo "$OLD $NEW $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $test_tick -0700	haha.t" >expected &&
		test_cmp expected .git/worktree.bkl
	)
'

test_done

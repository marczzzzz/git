#include "cache.h"
#include "builtin.h"
#include "backup-log.h"
#include "diff.h"
#include "diffcore.h"
#include "dir.h"
#include "object-store.h"
#include "parse-options.h"
#include "revision.h"

static char const * const backup_log_usage[] = {
	N_("git backup-log [--path=<path> | --id=<id>] update <path> <old-hash> <new-hash>"),
	N_("git backup-log [--path=<path> | --id=<id>] cat [<options>] <change-id> <path>"),
	N_("git backup-log [--path=<path> | --id=<id>] diff [<diff-options>] <change-id>"),
	N_("git backup-log [--path=<path> | --id=<id>] log [<options>] [--] [<path>]"),
	NULL
};

static int update(int argc, const char **argv,
		  const char *prefix, const char *log_path)
{
	struct strbuf sb = STRBUF_INIT;
	struct object_id oid_from, oid_to;
	const char *path;
	int ret;

	if (argc != 4)
		usage_with_options(backup_log_usage, NULL);

	path = argv[1];

	if (get_oid(argv[2], &oid_from))
		die(_("not a valid object name: %s"), argv[2]);

	if (get_oid(argv[3], &oid_to))
		die(_("not a valid object name: %s"), argv[3]);

	bkl_append(&sb, path, &oid_from, &oid_to);
	ret = bkl_write(log_path, &sb);
	strbuf_release(&sb);

	return ret;
}

struct cat_options {
	timestamp_t id;
	char *path;
	int before;
	int show_hash;
};

static int cat_parse(struct strbuf *line, void *data)
{
	struct cat_options *opts = data;
	struct bkl_entry entry;
	struct object_id *oid;
	void *buf;
	unsigned long size;
	enum object_type type;

	if (bkl_parse_entry(line, &entry))
		return -1;

	if (entry.timestamp < opts->id)
		return 2;

	if (entry.timestamp != opts->id ||
	    fspathcmp(entry.path, opts->path))
		return 0;

	if (opts->before)
		oid = &entry.old_oid;
	else
		oid = &entry.new_oid;

	if (opts->show_hash) {
		puts(oid_to_hex(oid));
		return 1;
	}

	if (is_null_oid(oid))
		return 1;	/* treat null oid like empty blob */

	buf = read_object_file(oid, &type, &size);
	if (!buf)
		die(_("object not found: %s"), oid_to_hex(oid));
	if (type != OBJ_BLOB)
		die(_("not a blob: %s"), oid_to_hex(oid));

	write_in_full(1, buf, size);
	free(buf);

	return 1;
}

static int cat(int argc, const char **argv,
		const char *prefix, const char *log_path)
{
	struct cat_options opts;
	struct option options[] = {
		OPT_BOOL(0, "before", &opts.before, N_("select the version before the update")),
		OPT_BOOL(0, "hash", &opts.show_hash, N_("show the hash instead of the content")),
		OPT_END()
	};
	char *end = NULL;
	int ret;

	memset(&opts, 0, sizeof(opts));
	argc = parse_options(argc, argv, prefix, options, backup_log_usage, 0);
	if (argc != 2)
		usage_with_options(backup_log_usage, options);
	opts.id = strtol(argv[0], &end, 10);
	if (!end || *end)
		die(_("not a valid change id: %s"), argv[0]);
	opts.path = prefix_path(prefix, prefix ? strlen(prefix) : 0, argv[1]);
	setup_pager();
	ret = bkl_parse_file_reverse(log_path, cat_parse, &opts);
	if (ret < 0)
		die(_("failed to parse '%s'"), log_path);
	return ret - 1;
}

static void queue_blk_entry_for_diff(const struct bkl_entry *e)
{
	unsigned mode = canon_mode(S_IFREG | 0644);
	struct diff_filespec *one, *two;
	const struct object_id *old, *new;

	if (is_null_oid(&e->old_oid))
		old = the_hash_algo->empty_blob;
	else
		old = &e->old_oid;

	if (is_null_oid(&e->new_oid))
		new = the_hash_algo->empty_blob;
	else
		new = &e->new_oid;

	if (oideq(old, new))
		return;

	one = alloc_filespec(e->path);
	two = alloc_filespec(e->path);

	fill_filespec(one, old, 1, mode);
	fill_filespec(two, new, 1, mode);
	diff_queue(&diff_queued_diff, one, two);
}

static int diff_parse(struct strbuf *line, void *data)
{
	timestamp_t id = *(timestamp_t *)data;
	struct bkl_entry entry;

	if (bkl_parse_entry(line, &entry))
		return -1;

	if (entry.timestamp < id)
		return 1;

	if (entry.timestamp > id)
		return 0;

	queue_blk_entry_for_diff(&entry);
	return 0;
}

static int diff(int argc, const char **argv,
		const char *prefix, const char *log_path)
{
	char *end = NULL;
	struct diff_options diffopt;
	timestamp_t id = -1;
	int ret, i, found_dash_dash = 0;

	repo_diff_setup(the_repository, &diffopt);
	i = 1;
	while (i < argc) {
		const char *arg = argv[i];

		if (!found_dash_dash && *arg == '-') {
			if (!strcmp(arg, "--")) {
				found_dash_dash = 1;
				i++;
				continue;
			}
			ret = diff_opt_parse(&diffopt, argv + i, argc - i, prefix);
			if (ret < 0)
				exit(128);
			i += ret;
			continue;
		}

		id = strtol(arg, &end, 10);
		if (!end || *end)
			die(_("not a valid change id: %s"), arg);
		i++;
		break;
	}
	if (!diffopt.output_format)
		diffopt.output_format = DIFF_FORMAT_PATCH;
	diff_setup_done(&diffopt);
	if (i != argc || id == -1)
		usage_with_options(backup_log_usage, NULL);

	ret = bkl_parse_file_reverse(log_path, diff_parse, &id);
	if (ret < 0)
		die(_("failed to parse '%s'"), log_path);

	if (ret == 1) {
		setup_pager();
		diffcore_std(&diffopt);
		diff_flush(&diffopt);
	}
	return ret - 1;
}

struct log_options {
	struct rev_info revs;
	timestamp_t last_time;
	int last_tz;
};

static void dump(struct bkl_entry *entry, struct log_options *opts)
{
	timestamp_t last_time = opts->last_time;
	int last_tz = opts->last_tz;

	if (entry) {
		opts->last_time = entry->timestamp;
		opts->last_tz = entry->tz;
	}

	if (last_time != -1 &&
	    ((!entry && diff_queued_diff.nr) ||
	     (entry && last_time != entry->timestamp))) {
		printf("Change-Id: %"PRItime"\n", last_time);
		printf("Date: %s\n", show_date(last_time, last_tz, &opts->revs.date_mode));
		printf("\n--\n");
		diffcore_std(&opts->revs.diffopt);
		diff_flush(&opts->revs.diffopt);
		printf("\n");
	}
}

static int log_parse(struct strbuf *line, void *data)
{
	struct log_options *opts = data;
	struct bkl_entry entry;

	if (bkl_parse_entry(line, &entry))
		return -1;

	if (opts->revs.max_age != -1 && entry.timestamp < opts->revs.max_age)
		return 1;

	if (!match_pathspec(the_repository->index, &opts->revs.prune_data,
			    entry.path, strlen(entry.path),
			    0, NULL, 0))
		return 0;

	dump(&entry, opts);

	queue_blk_entry_for_diff(&entry);

	return 0;
}

static int log_(int argc, const char **argv,
		const char *prefix, const char *log_path)
{
	struct log_options opts;
	int ret;

	memset(&opts, 0, sizeof(opts));
	opts.last_time = -1;
	opts.last_tz = -1;

	repo_init_revisions(the_repository, &opts.revs, prefix);
	opts.revs.date_mode.type = DATE_RELATIVE;
	argc = setup_revisions(argc, argv, &opts.revs, NULL);
	if (!opts.revs.diffopt.output_format) {
		opts.revs.diffopt.output_format = DIFF_FORMAT_PATCH;
		diff_setup_done(&opts.revs.diffopt);
	}

	setup_pager();
	if (opts.revs.reverse) {
		/*
		 * Default order is reading file from the bottom. --reverse
		 * makes it read from the top instead (i.e. forward)
		 */
		ret = bkl_parse_file(log_path, log_parse, &opts);
	} else {
		ret = bkl_parse_file_reverse(log_path, log_parse, &opts);
	}
	if (ret < 0)
		die(_("failed to parse '%s'"), log_path);
	dump(NULL, &opts);	/* flush */

	return ret;
}

static int prune(int argc, const char **argv,
		 const char *prefix, const char *log_path)
{
	timestamp_t expire = time(NULL) - 90 * 24 * 3600;
	struct option options[] = {
		OPT_EXPIRY_DATE(0, "expire", &expire,
				N_("expire objects older than <time>")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, backup_log_usage, 0);

	return bkl_prune(the_repository, log_path, expire);
}

static char *log_id_to_path(const char *id)
{
	if (!strcmp(id, "index"))
		return git_pathdup("index.bkl");
	else if (!strcmp(id, "worktree"))
		return git_pathdup("worktree.bkl");
	else if (!strcmp(id, "gitdir"))
		return git_pathdup("common/gitdir.bkl");
	else
		die(_("backup log id '%s' is not recognized"), id);
}

int cmd_backup_log(int argc, const char **argv, const char *prefix)
{
	const char *log_id = NULL;
	const char *log_path = NULL;
	char *to_free = NULL;
	struct option common_opts[] = {
		OPT_STRING(0, "id", &log_id, N_("id"), N_("backup log file id")),
		OPT_FILENAME(0, "path", &log_path, N_("backup log file path")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, common_opts,
			     backup_log_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc)
		die(_("expected a subcommand"));

	if (log_id) {
		if (log_path)
			die(_("--id and --path are incompatible"));
		log_path = to_free = log_id_to_path(log_id);
	}
	if (!log_path)
		die(_("either --id or --path is required"));

	if (!strcmp(argv[0], "update"))
		return update(argc, argv, prefix, log_path);
	else if (!strcmp(argv[0], "cat"))
		return cat(argc, argv, prefix, log_path);
	else if (!strcmp(argv[0], "diff"))
		return diff(argc, argv, prefix, log_path);
	else if (!strcmp(argv[0], "log"))
		return log_(argc, argv, prefix, log_path);
	else if (!strcmp(argv[0], "prune"))
		return prune(argc, argv, prefix, log_path);
	else
		die(_("unknown subcommand: %s"), argv[0]);

	free(to_free);
	return 0;
}

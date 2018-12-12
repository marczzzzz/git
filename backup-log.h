#ifndef __BACKUP_LOG_H__
#define __BACKUP_LOG_H__

#include "cache.h"

struct repository;
struct rev_info;
struct strbuf;

struct bkl_entry
{
	struct object_id old_oid;
	struct object_id new_oid;
	const char *email;
	timestamp_t timestamp;
	int tz;
	const char *path;
};

void bkl_append(struct strbuf *output, const char *path,
		const struct object_id *from,
		const struct object_id *to);

int bkl_write(const char *path, struct strbuf *new_log);

int bkl_parse_entry(struct strbuf *sb, struct bkl_entry *re);
int bkl_parse_file_reverse(const char *path,
			   int (*parse)(struct strbuf *line, void *data),
			   void *data);
int bkl_parse_file(const char *path,
		   int (*parse)(struct strbuf *line, void *data),
		   void *data);

void add_backup_logs_to_pending(struct rev_info *revs, unsigned flags);
int bkl_prune(struct repository *r, const char *id, timestamp_t expire);
void bkl_prune_all_or_die(struct repository *r, timestamp_t expire);

#endif

#include "git-compat-util.h"
#include "parse-options.h"
#include "cache.h"
#include "config.h"
#include "commit.h"
#include "color.h"
#include "utf8.h"

#define OPT_SHORT 1
#define OPT_UNSET 2

int optbug(const struct option *opt, const char *reason)
{
	if (opt->long_name) {
		if (opt->short_name)
			return error("BUG: switch '%c' (--%s) %s",
				     opt->short_name, opt->long_name, reason);
		return error("BUG: option '%s' %s", opt->long_name, reason);
	}
	return error("BUG: switch '%c' %s", opt->short_name, reason);
}

static int get_arg(struct parse_opt_ctx_t *p, const struct option *opt,
		   int flags, const char **arg)
{
	if (p->opt) {
		*arg = p->opt;
		p->opt = NULL;
	} else if (p->argc == 1 && (opt->flags & PARSE_OPT_LASTARG_DEFAULT)) {
		*arg = (const char *)opt->defval;
	} else if (p->argc > 1) {
		p->argc--;
		*arg = *++p->argv;
	} else
		return error(_("%s requires a value"), optname(opt, flags));
	return 0;
}

static void fix_filename(const char *prefix, const char **file)
{
	if (!file || !*file || !prefix || is_absolute_path(*file)
	    || !strcmp("-", *file))
		return;
	*file = prefix_filename(prefix, *file);
}

static int opt_command_mode_error(const struct option *opt,
				  const struct option *all_opts,
				  int flags)
{
	const struct option *that;
	struct strbuf that_name = STRBUF_INIT;

	/*
	 * Find the other option that was used to set the variable
	 * already, and report that this is not compatible with it.
	 */
	for (that = all_opts; that->type != OPTION_END; that++) {
		if (that == opt ||
		    that->type != OPTION_CMDMODE ||
		    that->value != opt->value ||
		    that->defval != *(int *)opt->value)
			continue;

		if (that->long_name)
			strbuf_addf(&that_name, "--%s", that->long_name);
		else
			strbuf_addf(&that_name, "-%c", that->short_name);
		error(_("%s is incompatible with %s"),
		      optname(opt, flags), that_name.buf);
		strbuf_release(&that_name);
		return -1;
	}
	return error(_("%s : incompatible with something else"),
		     optname(opt, flags));
}

static int get_value(struct parse_opt_ctx_t *p,
		     const struct option *opt,
		     const struct option *all_opts,
		     int flags)
{
	const char *s, *arg;
	const int unset = flags & OPT_UNSET;
	int err;

	if (unset && p->opt)
		return error(_("%s takes no value"), optname(opt, flags));
	if (unset && (opt->flags & PARSE_OPT_NONEG))
		return error(_("%s isn't available"), optname(opt, flags));
	if (!(flags & OPT_SHORT) && p->opt && (opt->flags & PARSE_OPT_NOARG))
		return error(_("%s takes no value"), optname(opt, flags));

	switch (opt->type) {
	case OPTION_LOWLEVEL_CALLBACK:
		return (*(parse_opt_ll_cb *)opt->callback)(p, opt, unset);

	case OPTION_BIT:
		if (unset)
			*(int *)opt->value &= ~opt->defval;
		else
			*(int *)opt->value |= opt->defval;
		return 0;

	case OPTION_NEGBIT:
		if (unset)
			*(int *)opt->value |= opt->defval;
		else
			*(int *)opt->value &= ~opt->defval;
		return 0;

	case OPTION_COUNTUP:
		if (*(int *)opt->value < 0)
			*(int *)opt->value = 0;
		*(int *)opt->value = unset ? 0 : *(int *)opt->value + 1;
		return 0;

	case OPTION_SET_INT:
		*(int *)opt->value = unset ? 0 : opt->defval;
		return 0;

	case OPTION_CMDMODE:
		/*
		 * Giving the same mode option twice, although is unnecessary,
		 * is not a grave error, so let it pass.
		 */
		if (*(int *)opt->value && *(int *)opt->value != opt->defval)
			return opt_command_mode_error(opt, all_opts, flags);
		*(int *)opt->value = opt->defval;
		return 0;

	case OPTION_STRING:
		if (unset)
			*(const char **)opt->value = NULL;
		else if (opt->flags & PARSE_OPT_OPTARG && !p->opt)
			*(const char **)opt->value = (const char *)opt->defval;
		else
			return get_arg(p, opt, flags, (const char **)opt->value);
		return 0;

	case OPTION_FILENAME:
		err = 0;
		if (unset)
			*(const char **)opt->value = NULL;
		else if (opt->flags & PARSE_OPT_OPTARG && !p->opt)
			*(const char **)opt->value = (const char *)opt->defval;
		else
			err = get_arg(p, opt, flags, (const char **)opt->value);

		if (!err)
			fix_filename(p->prefix, (const char **)opt->value);
		return err;

	case OPTION_CALLBACK:
		if (unset)
			return (*opt->callback)(opt, NULL, 1) ? (-1) : 0;
		if (opt->flags & PARSE_OPT_NOARG)
			return (*opt->callback)(opt, NULL, 0) ? (-1) : 0;
		if (opt->flags & PARSE_OPT_OPTARG && !p->opt)
			return (*opt->callback)(opt, NULL, 0) ? (-1) : 0;
		if (get_arg(p, opt, flags, &arg))
			return -1;
		return (*opt->callback)(opt, arg, 0) ? (-1) : 0;

	case OPTION_INTEGER:
		if (unset) {
			*(int *)opt->value = 0;
			return 0;
		}
		if (opt->flags & PARSE_OPT_OPTARG && !p->opt) {
			*(int *)opt->value = opt->defval;
			return 0;
		}
		if (get_arg(p, opt, flags, &arg))
			return -1;
		*(int *)opt->value = strtol(arg, (char **)&s, 10);
		if (*s)
			return error(_("%s expects a numerical value"),
				     optname(opt, flags));
		return 0;

	case OPTION_MAGNITUDE:
		if (unset) {
			*(unsigned long *)opt->value = 0;
			return 0;
		}
		if (opt->flags & PARSE_OPT_OPTARG && !p->opt) {
			*(unsigned long *)opt->value = opt->defval;
			return 0;
		}
		if (get_arg(p, opt, flags, &arg))
			return -1;
		if (!git_parse_ulong(arg, opt->value))
			return error(_("%s expects a non-negative integer value"
				       " with an optional k/m/g suffix"),
				     optname(opt, flags));
		return 0;

	default:
		BUG("opt->type %d should not happen", opt->type);
	}
}

static int parse_short_opt(struct parse_opt_ctx_t *p, const struct option *options)
{
	const struct option *all_opts = options;
	const struct option *numopt = NULL;

	for (; options->type != OPTION_END; options++) {
		if (options->short_name == *p->opt) {
			p->opt = p->opt[1] ? p->opt + 1 : NULL;
			return get_value(p, options, all_opts, OPT_SHORT);
		}

		/*
		 * Handle the numerical option later, explicit one-digit
		 * options take precedence over it.
		 */
		if (options->type == OPTION_NUMBER)
			numopt = options;
	}
	if (numopt && isdigit(*p->opt)) {
		size_t len = 1;
		char *arg;
		int rc;

		while (isdigit(p->opt[len]))
			len++;
		arg = xmemdupz(p->opt, len);
		p->opt = p->opt[len] ? p->opt + len : NULL;
		rc = (*numopt->callback)(numopt, arg, 0) ? (-1) : 0;
		free(arg);
		return rc;
	}
	return -2;
}

static int parse_long_opt(struct parse_opt_ctx_t *p, const char *arg,
                          const struct option *options)
{
	const struct option *all_opts = options;
	const char *arg_end = strchrnul(arg, '=');
	const struct option *abbrev_option = NULL, *ambiguous_option = NULL;
	int abbrev_flags = 0, ambiguous_flags = 0;

	for (; options->type != OPTION_END; options++) {
		const char *rest, *long_name = options->long_name;
		int flags = 0, opt_flags = 0;

		if (!long_name)
			continue;

again:
		if (!skip_prefix(arg, long_name, &rest))
			rest = NULL;
		if (options->type == OPTION_ARGUMENT) {
			if (!rest)
				continue;
			if (*rest == '=')
				return error(_("%s takes no value"),
					     optname(options, flags));
			if (*rest)
				continue;
			p->out[p->cpidx++] = arg - 2;
			return 0;
		}
		if (!rest) {
			/* abbreviated? */
			if (!strncmp(long_name, arg, arg_end - arg)) {
is_abbreviated:
				if (abbrev_option) {
					/*
					 * If this is abbreviated, it is
					 * ambiguous. So when there is no
					 * exact match later, we need to
					 * error out.
					 */
					ambiguous_option = abbrev_option;
					ambiguous_flags = abbrev_flags;
				}
				if (!(flags & OPT_UNSET) && *arg_end)
					p->opt = arg_end + 1;
				abbrev_option = options;
				abbrev_flags = flags ^ opt_flags;
				continue;
			}
			/* negation allowed? */
			if (options->flags & PARSE_OPT_NONEG)
				continue;
			/* negated and abbreviated very much? */
			if (starts_with("no-", arg)) {
				flags |= OPT_UNSET;
				goto is_abbreviated;
			}
			/* negated? */
			if (!starts_with(arg, "no-")) {
				if (starts_with(long_name, "no-")) {
					long_name += 3;
					opt_flags |= OPT_UNSET;
					goto again;
				}
				continue;
			}
			flags |= OPT_UNSET;
			if (!skip_prefix(arg + 3, long_name, &rest)) {
				/* abbreviated and negated? */
				if (starts_with(long_name, arg + 3))
					goto is_abbreviated;
				else
					continue;
			}
		}
		if (*rest) {
			if (*rest != '=')
				continue;
			p->opt = rest + 1;
		}
		return get_value(p, options, all_opts, flags ^ opt_flags);
	}

	if (ambiguous_option) {
		error(_("ambiguous option: %s "
			"(could be --%s%s or --%s%s)"),
			arg,
			(ambiguous_flags & OPT_UNSET) ?  "no-" : "",
			ambiguous_option->long_name,
			(abbrev_flags & OPT_UNSET) ?  "no-" : "",
			abbrev_option->long_name);
		return -3;
	}
	if (abbrev_option)
		return get_value(p, abbrev_option, all_opts, abbrev_flags);
	return -2;
}

static int parse_nodash_opt(struct parse_opt_ctx_t *p, const char *arg,
			    const struct option *options)
{
	const struct option *all_opts = options;

	for (; options->type != OPTION_END; options++) {
		if (!(options->flags & PARSE_OPT_NODASH))
			continue;
		if (options->short_name == arg[0] && arg[1] == '\0')
			return get_value(p, options, all_opts, OPT_SHORT);
	}
	return -2;
}

static void check_typos(const char *arg, const struct option *options)
{
	if (strlen(arg) < 3)
		return;

	if (starts_with(arg, "no-")) {
		error(_("did you mean `--%s` (with two dashes ?)"), arg);
		exit(129);
	}

	for (; options->type != OPTION_END; options++) {
		if (!options->long_name)
			continue;
		if (starts_with(options->long_name, arg)) {
			error(_("did you mean `--%s` (with two dashes ?)"), arg);
			exit(129);
		}
	}
}

static void parse_options_check(const struct option *opts)
{
	int err = 0;
	char short_opts[128];

	memset(short_opts, '\0', sizeof(short_opts));
	for (; opts->type != OPTION_END; opts++) {
		if ((opts->flags & PARSE_OPT_LASTARG_DEFAULT) &&
		    (opts->flags & PARSE_OPT_OPTARG))
			err |= optbug(opts, "uses incompatible flags "
					"LASTARG_DEFAULT and OPTARG");
		if (opts->short_name) {
			if (0x7F <= opts->short_name)
				err |= optbug(opts, "invalid short name");
			else if (short_opts[opts->short_name]++)
				err |= optbug(opts, "short name already used");
		}
		if (opts->flags & PARSE_OPT_NODASH &&
		    ((opts->flags & PARSE_OPT_OPTARG) ||
		     !(opts->flags & PARSE_OPT_NOARG) ||
		     !(opts->flags & PARSE_OPT_NONEG) ||
		     opts->long_name))
			err |= optbug(opts, "uses feature "
					"not supported for dashless options");
		switch (opts->type) {
		case OPTION_COUNTUP:
		case OPTION_BIT:
		case OPTION_NEGBIT:
		case OPTION_SET_INT:
		case OPTION_NUMBER:
			if ((opts->flags & PARSE_OPT_OPTARG) ||
			    !(opts->flags & PARSE_OPT_NOARG))
				err |= optbug(opts, "should not accept an argument");
		default:
			; /* ok. (usually accepts an argument) */
		}
		if (opts->argh &&
		    strcspn(opts->argh, " _") != strlen(opts->argh))
			err |= optbug(opts, "multi-word argh should use dash to separate words");
	}
	if (err)
		exit(128);
}

void parse_options_start(struct parse_opt_ctx_t *ctx,
			 int argc, const char **argv, const char *prefix,
			 const struct option *options, int flags)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->argc = ctx->total = argc - 1;
	ctx->argv = argv + 1;
	ctx->out  = argv;
	ctx->prefix = prefix;
	ctx->cpidx = ((flags & PARSE_OPT_KEEP_ARGV0) != 0);
	ctx->flags = flags;
	if ((flags & PARSE_OPT_KEEP_UNKNOWN) &&
	    (flags & PARSE_OPT_STOP_AT_NON_OPTION))
		BUG("STOP_AT_NON_OPTION and KEEP_UNKNOWN don't go together");
	parse_options_check(options);
}

static void show_negated_gitcomp(const struct option *opts, int nr_noopts)
{
	int printed_dashdash = 0;

	for (; opts->type != OPTION_END; opts++) {
		int has_unset_form = 0;
		const char *name;

		if (!opts->long_name)
			continue;
		if (opts->flags & (PARSE_OPT_HIDDEN | PARSE_OPT_NOCOMPLETE))
			continue;
		if (opts->flags & PARSE_OPT_NONEG)
			continue;

		switch (opts->type) {
		case OPTION_STRING:
		case OPTION_FILENAME:
		case OPTION_INTEGER:
		case OPTION_MAGNITUDE:
		case OPTION_CALLBACK:
		case OPTION_BIT:
		case OPTION_NEGBIT:
		case OPTION_COUNTUP:
		case OPTION_SET_INT:
			has_unset_form = 1;
			break;
		default:
			break;
		}
		if (!has_unset_form)
			continue;

		if (skip_prefix(opts->long_name, "no-", &name)) {
			if (nr_noopts < 0)
				printf(" --%s", name);
		} else if (nr_noopts >= 0) {
			if (nr_noopts && !printed_dashdash) {
				printf(" --");
				printed_dashdash = 1;
			}
			printf(" --no-%s", opts->long_name);
			nr_noopts++;
		}
	}
}

static int show_gitcomp(struct parse_opt_ctx_t *ctx,
			const struct option *opts)
{
	const struct option *original_opts = opts;
	int nr_noopts = 0;

	for (; opts->type != OPTION_END; opts++) {
		const char *suffix = "";

		if (!opts->long_name)
			continue;
		if (opts->flags & (PARSE_OPT_HIDDEN | PARSE_OPT_NOCOMPLETE))
			continue;

		switch (opts->type) {
		case OPTION_GROUP:
			continue;
		case OPTION_STRING:
		case OPTION_FILENAME:
		case OPTION_INTEGER:
		case OPTION_MAGNITUDE:
		case OPTION_CALLBACK:
			if (opts->flags & PARSE_OPT_NOARG)
				break;
			if (opts->flags & PARSE_OPT_OPTARG)
				break;
			if (opts->flags & PARSE_OPT_LASTARG_DEFAULT)
				break;
			suffix = "=";
			break;
		default:
			break;
		}
		if (opts->flags & PARSE_OPT_COMP_ARG)
			suffix = "=";
		if (starts_with(opts->long_name, "no-"))
			nr_noopts++;
		printf(" --%s%s", opts->long_name, suffix);
	}
	show_negated_gitcomp(original_opts, -1);
	show_negated_gitcomp(original_opts, nr_noopts);
	fputc('\n', stdout);
	return PARSE_OPT_COMPLETE;
}

static int usage_with_options_internal(struct parse_opt_ctx_t *,
				       const char * const *,
				       const struct option *, int, int);

int parse_options_step(struct parse_opt_ctx_t *ctx,
		       const struct option *options,
		       const char * const usagestr[])
{
	int internal_help = !(ctx->flags & PARSE_OPT_NO_INTERNAL_HELP);

	/* we must reset ->opt, unknown short option leave it dangling */
	ctx->opt = NULL;

	for (; ctx->argc; ctx->argc--, ctx->argv++) {
		const char *arg = ctx->argv[0];

		if (*arg != '-' || !arg[1]) {
			if (parse_nodash_opt(ctx, arg, options) == 0)
				continue;
			if (ctx->flags & PARSE_OPT_STOP_AT_NON_OPTION)
				return PARSE_OPT_NON_OPTION;
			ctx->out[ctx->cpidx++] = ctx->argv[0];
			continue;
		}

		/* lone -h asks for help */
		if (internal_help && ctx->total == 1 && !strcmp(arg + 1, "h"))
			goto show_usage;

		/* lone --git-completion-helper is asked by git-completion.bash */
		if (ctx->total == 1 && !strcmp(arg + 1, "-git-completion-helper"))
			return show_gitcomp(ctx, options);

		if (arg[1] != '-') {
			ctx->opt = arg + 1;
			switch (parse_short_opt(ctx, options)) {
			case -1:
				return PARSE_OPT_ERROR;
			case -2:
				if (ctx->opt)
					check_typos(arg + 1, options);
				if (internal_help && *ctx->opt == 'h')
					goto show_usage;
				goto unknown;
			}
			if (ctx->opt)
				check_typos(arg + 1, options);
			while (ctx->opt) {
				switch (parse_short_opt(ctx, options)) {
				case -1:
					return PARSE_OPT_ERROR;
				case -2:
					if (internal_help && *ctx->opt == 'h')
						goto show_usage;

					/* fake a short option thing to hide the fact that we may have
					 * started to parse aggregated stuff
					 *
					 * This is leaky, too bad.
					 */
					ctx->argv[0] = xstrdup(ctx->opt - 1);
					*(char *)ctx->argv[0] = '-';
					goto unknown;
				}
			}
			continue;
		}

		if (!arg[2]) { /* "--" */
			if (!(ctx->flags & PARSE_OPT_KEEP_DASHDASH)) {
				ctx->argc--;
				ctx->argv++;
			}
			break;
		}

		if (internal_help && !strcmp(arg + 2, "help-all"))
			return usage_with_options_internal(ctx, usagestr, options, 1, 0);
		if (internal_help && !strcmp(arg + 2, "help"))
			goto show_usage;
		switch (parse_long_opt(ctx, arg + 2, options)) {
		case -1:
			return PARSE_OPT_ERROR;
		case -2:
			goto unknown;
		case -3:
			goto show_usage;
		}
		continue;
unknown:
		if (!(ctx->flags & PARSE_OPT_KEEP_UNKNOWN))
			return PARSE_OPT_UNKNOWN;
		ctx->out[ctx->cpidx++] = ctx->argv[0];
		ctx->opt = NULL;
	}
	return PARSE_OPT_DONE;

 show_usage:
	return usage_with_options_internal(ctx, usagestr, options, 0, 0);
}

int parse_options_end(struct parse_opt_ctx_t *ctx)
{
	MOVE_ARRAY(ctx->out + ctx->cpidx, ctx->argv, ctx->argc);
	ctx->out[ctx->cpidx + ctx->argc] = NULL;
	return ctx->cpidx + ctx->argc;
}

int parse_options(int argc, const char **argv, const char *prefix,
		  const struct option *options, const char * const usagestr[],
		  int flags)
{
	struct parse_opt_ctx_t ctx;

	parse_options_start(&ctx, argc, argv, prefix, options, flags);
	switch (parse_options_step(&ctx, options, usagestr)) {
	case PARSE_OPT_HELP:
	case PARSE_OPT_ERROR:
		exit(129);
	case PARSE_OPT_COMPLETE:
		exit(0);
	case PARSE_OPT_NON_OPTION:
	case PARSE_OPT_DONE:
		break;
	default: /* PARSE_OPT_UNKNOWN */
		if (ctx.argv[0][1] == '-') {
			error(_("unknown option `%s'"), ctx.argv[0] + 2);
		} else if (isascii(*ctx.opt)) {
			error(_("unknown switch `%c'"), *ctx.opt);
		} else {
			error(_("unknown non-ascii option in string: `%s'"),
			      ctx.argv[0]);
		}
		usage_with_options(usagestr, options);
	}

	precompose_argv(argc, argv);
	return parse_options_end(&ctx);
}

static int usage_argh(const struct option *opts, FILE *outfile)
{
	const char *s;
	int literal = (opts->flags & PARSE_OPT_LITERAL_ARGHELP) ||
		!opts->argh || !!strpbrk(opts->argh, "()<>[]|");
	if (opts->flags & PARSE_OPT_OPTARG)
		if (opts->long_name)
			s = literal ? "[=%s]" : "[=<%s>]";
		else
			s = literal ? "[%s]" : "[<%s>]";
	else
		s = literal ? " %s" : " <%s>";
	return utf8_fprintf(outfile, s, opts->argh ? _(opts->argh) : _("..."));
}

#define USAGE_OPTS_WIDTH 24
#define USAGE_GAP         2

static int usage_with_options_internal(struct parse_opt_ctx_t *ctx,
				       const char * const *usagestr,
				       const struct option *opts, int full, int err)
{
	FILE *outfile = err ? stderr : stdout;
	int need_newline;

	if (!usagestr)
		return PARSE_OPT_HELP;

	if (!err && ctx && ctx->flags & PARSE_OPT_SHELL_EVAL)
		fprintf(outfile, "cat <<\\EOF\n");

	fprintf_ln(outfile, _("usage: %s"), _(*usagestr++));
	while (*usagestr && **usagestr)
		/*
		 * TRANSLATORS: the colon here should align with the
		 * one in "usage: %s" translation.
		 */
		fprintf_ln(outfile, _("   or: %s"), _(*usagestr++));
	while (*usagestr) {
		if (**usagestr)
			fprintf_ln(outfile, _("    %s"), _(*usagestr));
		else
			fputc('\n', outfile);
		usagestr++;
	}

	need_newline = 1;

	for (; opts->type != OPTION_END; opts++) {
		size_t pos;
		int pad;

		if (opts->type == OPTION_GROUP) {
			fputc('\n', outfile);
			need_newline = 0;
			if (*opts->help)
				fprintf(outfile, "%s\n", _(opts->help));
			continue;
		}
		if (!full && (opts->flags & PARSE_OPT_HIDDEN))
			continue;

		if (need_newline) {
			fputc('\n', outfile);
			need_newline = 0;
		}

		pos = fprintf(outfile, "    ");
		if (opts->short_name) {
			if (opts->flags & PARSE_OPT_NODASH)
				pos += fprintf(outfile, "%c", opts->short_name);
			else
				pos += fprintf(outfile, "-%c", opts->short_name);
		}
		if (opts->long_name && opts->short_name)
			pos += fprintf(outfile, ", ");
		if (opts->long_name)
			pos += fprintf(outfile, "--%s", opts->long_name);
		if (opts->type == OPTION_NUMBER)
			pos += utf8_fprintf(outfile, _("-NUM"));

		if ((opts->flags & PARSE_OPT_LITERAL_ARGHELP) ||
		    !(opts->flags & PARSE_OPT_NOARG))
			pos += usage_argh(opts, outfile);

		if (pos <= USAGE_OPTS_WIDTH)
			pad = USAGE_OPTS_WIDTH - pos;
		else {
			fputc('\n', outfile);
			pad = USAGE_OPTS_WIDTH;
		}
		fprintf(outfile, "%*s%s\n", pad + USAGE_GAP, "", _(opts->help));
	}
	fputc('\n', outfile);

	if (!err && ctx && ctx->flags & PARSE_OPT_SHELL_EVAL)
		fputs("EOF\n", outfile);

	return PARSE_OPT_HELP;
}

void NORETURN usage_with_options(const char * const *usagestr,
			const struct option *opts)
{
	usage_with_options_internal(NULL, usagestr, opts, 0, 1);
	exit(129);
}

void NORETURN usage_msg_opt(const char *msg,
		   const char * const *usagestr,
		   const struct option *options)
{
	fprintf(stderr, "fatal: %s\n\n", msg);
	usage_with_options(usagestr, options);
}

const char *optname(const struct option *opt, int flags)
{
	static struct strbuf sb = STRBUF_INIT;

	strbuf_reset(&sb);
	if (flags & OPT_SHORT)
		strbuf_addf(&sb, "switch `%c'", opt->short_name);
	else if (flags & OPT_UNSET)
		strbuf_addf(&sb, "option `no-%s'", opt->long_name);
	else
		strbuf_addf(&sb, "option `%s'", opt->long_name);

	return sb.buf;
}

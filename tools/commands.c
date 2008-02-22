/*
 * Copyright (C) 2004,2005  Heinz Mauelshagen, Red Hat GmbH.
 *                          All rights reserved.
 *
 * See file LICENSE at the top of this source tree for license information.
 */

#ifdef HAVE_GETOPTLONG
# define _GNU_SOURCE
# include <getopt.h>
#endif

#include <string.h>
#include <unistd.h>
#include <dmraid/dmraid.h>
#include "../lib/log/log.h"
#include "commands.h"
#include "toollib.h"
#include "version.h"

/* Action flags */
enum action action = UNDEF;

/*
 * Command line options.
 */
static char const *short_opts = "a:hip"
#ifndef	DMRAID_MINI
				"bc::dDEf:gl"
#ifdef	DMRAID_NATIVE_LOG
				"n"
#endif
				"rs::tv"
#endif
				"V";

#ifdef HAVE_GETOPTLONG
static struct option long_opts[] = {
	{"activate", required_argument, NULL, 'a'},
	{"format", required_argument, NULL, 'f'},
	{"no_partitions", no_argument, NULL, 'p'},
# ifndef DMRAID_MINI
	{"block_devices", no_argument, NULL, 'b'},
	{"display_columns", optional_argument, NULL, 'c'},
	{"debug", no_argument, NULL, 'd'},
	{"dump_metadata", no_argument, NULL, 'D'},
	{"erase_metadata", no_argument, NULL, 'E'},
	{"display_group", no_argument, NULL, 'g'},
# endif
	{"help", no_argument, NULL, 'h'},
	{"ignorelocking", no_argument, NULL, 'i'},
# ifndef DMRAID_MINI
	{"list_formats", no_argument, NULL, 'l'},
#  ifdef DMRAID_NATIVE_LOG
	{"native_log", no_argument, NULL, 'n'},
#  endif
	{"raid_devices", no_argument, NULL, 'r'},
	{"sets", optional_argument, NULL, 's'},
	{"separator", required_argument, NULL, SEPARATOR}, /* long only. */
	{"test", no_argument, NULL, 't'},
	{"verbose", no_argument, NULL, 'v'},
# endif
	{"version", no_argument, NULL, 'V'},
	{NULL, no_argument, NULL, 0}
};
#endif /* #ifdef HAVE_GETOPTLONG */

/* Definitions of option strings and actions for check_optarg(). */
struct optarg_def {
	const char *str;
	const enum action action;
};

/* Check option argument. */
static int check_optarg(struct lib_context *lc, const char option,
			struct optarg_def *def)
{
	size_t len;
	struct optarg_def *d;

	if (optarg)
		str_tolower(optarg);
	else
		return 1;

	for (d = def, len = strlen(optarg); d->str; d++) {
		if (!strncmp(optarg, d->str, len)) {
			action |= d->action;
			return 1;
		}
	}

	LOG_ERR(lc, 0, "Invalid option argument for -%c", option);
}

/* Check activate/deactivate option arguments. */
static int check_activate(struct lib_context *lc, int arg)
{
	struct optarg_def def[] = {
		{ "yes", ACTIVATE },
		{ "no",  DEACTIVATE },
		{ NULL,  UNDEF },
	};

	return check_optarg(lc, 'a', def);
}

#ifndef	DMRAID_MINI
/* Check active/inactive option arguments. */
static int check_active(struct lib_context *lc, int arg)
{
	struct optarg_def def[] = {
		{ "active",   ACTIVE },
		{ "inactive", INACTIVE },
		{ NULL,  UNDEF },
	};

	lc_inc_opt(lc, LC_SETS);

	return check_optarg(lc, 's', def);
}

/* Check and store option arguments. */
static int check_identifiers(struct lib_context *lc, int o)
{
	if (optarg) {
		const char delim = *OPT_STR_SEPARATOR(lc);
		char *p = optarg;

		p = remove_white_space(lc, p, strlen(p));
		p = collapse_delimiter(lc, p, strlen(p), delim);
		if (!lc_strcat_opt(lc, o, p, delim))
			return 0;
	}

	lc_inc_opt(lc, o);

	return 1;
}

/* Check and store option argument/output field separator. */
static int check_separator(struct lib_context *lc, int arg)
{
	if (strlen(optarg) != 1)
		LOG_ERR(lc, 0, "Invalid separator \"%s\"", optarg);

	return lc_stralloc_opt(lc, LC_SEPARATOR, optarg) ? 1 : 0;
}
#endif

/* Display help information */
static int help(struct lib_context *lc, int arg)
{
	char *c = lc->cmd;

#ifdef	DMRAID_MINI
	log_print(lc, "%s: Device-Mapper Software RAID tool "
		  "[Early Boot Version]\n", c);
	log_print(lc, "%s\t{-a|--activate} {y|n|yes|no} [-i|--ignorelocking]\n"
		  "\t[-f|--format FORMAT[,FORMAT...]]\n"
		  "\t[-p|--no_partitions]\n"
		  "\t[--separator SEPARATOR]\n"
		  "\t[RAID-set...]\n", c);
	log_print(lc, "%s\t{-h|--help}\n", c);
	log_print(lc, "%s\t{-V/--version}\n", c);
#else
	log_print(lc, "%s: Device-Mapper Software RAID tool\n", c);
	log_print(lc, "* = [-d|--debug]... [-v|--verbose]... [-i|--ignorelocking]\n");
	log_print(lc, "%s\t{-a|--activate} {y|n|yes|no} *\n"
		  "\t[-f|--format FORMAT[,FORMAT...]]\n"
		  "\t[-p|--no_partitions]\n"
		  "\t[--separator SEPARATOR]\n"
		  "\t[-t|--test]\n"
		  "\t[RAID-set...]\n", c);
	log_print(lc, "%s\t{-b|--block_devices} *\n"
		  "\t[-c|--display_columns][FIELD[,FIELD...]]...\n"
		  "\t[device-path...]\n", c);
	log_print(lc, "%s\t{-h|--help}\n", c);
	log_print(lc, "%s\t{-l|--list_formats} *\n", c);
#  ifdef	DMRAID_NATIVE_LOG
	log_print(lc, "%s\t{-n|--native_log} *\n"
		  "\t[-f|--format FORMAT[,FORMAT...]]\n"
		  "\t[--separator SEPARATOR]\n"
		  "\t[device-path...]\n", c);
#  endif
	log_print(lc, "%s\t{-r|--raid_devices} *\n"
		  "\t[-c|--display_columns][FIELD[,FIELD...]]...\n"
		  "\t[-D|--dump_metadata]\n"
		  "\t[-f|--format FORMAT[,FORMAT...]]\n"
		  "\t[--separator SEPARATOR]\n"
		  "\t[device-path...]\n", c);
	log_print(lc, "%s\t{-r|--raid_devices} *\n"
		  "\t{-E|--erase_metadata}\n"
		  "\t[-f|--format FORMAT[,FORMAT...]]\n"
		  "\t[--separator SEPARATOR]\n"
		  "\t[device-path...]\n", c);
	log_print(lc, "%s\t{-s|--sets}...[a|i|active|inactive] *\n"
		  "\t[-c|--display_columns][FIELD[,FIELD...]]...\n"
		  "\t[-f|--format FORMAT[,FORMAT...]]\n"
		  "\t[-g|--display_group]\n"
		  "\t[--separator SEPARATOR]\n"
		  "\t[RAID-set...]\n", c);
	log_print(lc, "%s\t{-V/--version}\n", c);
#endif

	return 1;
}

/*
 * Action flag definitions for set_action()
 *
 * 'Early' options can be handled directly in set_action() by calling
 * the functions registered here (set on_set member).
 */
static struct actions actions[] = {
	/* [De]activate option. */
	{ 'a',
	  UNDEF, /* Set in check_activate() by mandatory option argument. */
	  UNDEF,
	  ACTIVATE|DEACTIVATE|FORMAT|HELP|IGNORELOCKING|NOPARTITIONS|SEPARATOR
#ifndef DMRAID_MINI
	  |DBG|TEST|VERBOSE
#endif
 	  , ARGS,
	  check_activate,
	  0,
	},

	/* Format option. */
	{ 'f',
	  FORMAT,
	  ACTIVATE|DEACTIVATE
#ifndef DMRAID_MINI
#  ifdef DMRAID_NATIVE_LOG
	  |NATIVE_LOG
#  endif
	  |RAID_DEVICES|RAID_SETS,
	  ACTIVE|INACTIVE|COLUMN|DBG|DUMP|ERASE|GROUP|HELP|
	  IGNORELOCKING|NOPARTITIONS|SEPARATOR|TEST|VERBOSE
#else
	  , UNDEF
#endif
	  , ARGS,
#ifndef DMRAID_MINI
	  check_identifiers,
#else
	  NULL,
#endif
	  LC_FORMAT,
	},

	/* Partition option. */
	{ 'p',
	  NOPARTITIONS,
	  ACTIVATE|DEACTIVATE,
	  FORMAT|HELP|IGNORELOCKING|SEPARATOR
#ifndef DMRAID_MINI
	  |DBG|TEST|VERBOSE
#endif
	  , ARGS,
	  NULL,
	  0,
	},

#ifndef	DMRAID_MINI
	/* Block devices option. */
	{ 'b',
	  BLOCK_DEVICES,
	  UNDEF,
	  COLUMN|DBG|HELP|IGNORELOCKING|SEPARATOR|VERBOSE,
	  ARGS,
	  lc_inc_opt,
	  LC_DEVICES,
	},

	/* Columns display option. */
	{ 'c',
	  COLUMN,
	  BLOCK_DEVICES|RAID_DEVICES|RAID_SETS,
	  ACTIVE|INACTIVE|DBG|DUMP|FORMAT|GROUP|HELP|IGNORELOCKING
	  |SEPARATOR|VERBOSE,
	  ARGS,
	  check_identifiers,
	  LC_COLUMN,
	},

	/* Debug option. */
	{ 'd',
	  DBG,
	  ALL_FLAGS,
	  ALL_FLAGS,
	  ARGS,
	  lc_inc_opt,
	  LC_DEBUG,
	},

	/* Dump metadata option. */
	{ 'D',
	  DUMP,
	  RAID_DEVICES,
	  COLUMN|DBG|FORMAT|HELP|IGNORELOCKING|SEPARATOR|VERBOSE,
	  ARGS,
	  lc_inc_opt,
	  LC_DUMP,
	},

	/* Erase metadata option. */
	{ 'E',
	  ERASE,
	  RAID_DEVICES,
	  COLUMN|DBG|FORMAT|HELP|IGNORELOCKING|SEPARATOR|VERBOSE,
	  ARGS,
	  NULL,
	  0,
	},

	/* RAID groups option. */
	{ 'g',
	  GROUP,
	  RAID_SETS,
	  ACTIVE|INACTIVE|DBG|COLUMN|FORMAT|HELP|IGNORELOCKING
	  |SEPARATOR|VERBOSE,
	  ARGS,
	  lc_inc_opt,
	  LC_GROUP,
	},

#endif
	/* Help option. */
	{ 'h',
	  HELP,
	  UNDEF,
	  ALL_FLAGS,
	  ARGS,
	  help,
	  0, 
	},

	/* ignorelocking option. */
	{ 'i',
	  IGNORELOCKING,
	  UNDEF,
	  ALL_FLAGS,
	  ARGS,
	  lc_inc_opt,
	  LC_IGNORELOCKING,
	},

#ifndef	DMRAID_MINI
	/* List metadata format handlers option. */
	{ 'l',
	  LIST_FORMATS,
	  UNDEF,
	  DBG|HELP|IGNORELOCKING|VERBOSE,
	  NO_ARGS,
	  NULL,
	  0,
	},

#  ifdef DMRAID_NATIVE_LOG
	/* Native log option. */
	{ 'n',
	  NATIVE_LOG,
	  UNDEF,
	  DBG|FORMAT|HELP|IGNORELOCKING|SEPARATOR|VERBOSE,
	  ARGS,
	  NULL,
	  0,
	},

#  endif
	/* Display RAID devices option. */
	{ 'r',
	  RAID_DEVICES,
	  UNDEF,
	  COLUMN|DBG|DUMP|ERASE|FORMAT|HELP|IGNORELOCKING|SEPARATOR|VERBOSE,
	  ARGS,
	  NULL,
	  0,
	},

	/* Display RAID sets option. */
	{ 's',
	  RAID_SETS,
	  UNDEF,
	  ACTIVE|INACTIVE|COLUMN|DBG|FORMAT|GROUP|HELP|IGNORELOCKING
	  |SEPARATOR|VERBOSE,
	  ARGS,
	  check_active,
	  0,
	},

	/* Display RAID sets option. */
	{ SEPARATOR,
	  SEPARATOR,
	  COLUMN|FORMAT,
	  ALL_FLAGS,
	  ARGS,
	  check_separator,
	  0,
	},


	/* Test run option. */
	{ 't',
	  TEST,
	  ACTIVATE|DEACTIVATE,
	  ACTIVATE|DEACTIVATE|DBG|FORMAT|HELP|IGNORELOCKING|
	  NOPARTITIONS|VERBOSE,
	  ARGS,
	  lc_inc_opt,
	  LC_TEST,
	},

	/* Verbose option. */
	{ 'v',
	  VERBOSE,
	  ALL_FLAGS,
	  ALL_FLAGS,
	  ARGS,
	  lc_inc_opt,
	  LC_VERBOSE,
	},
#endif /* #ifndef DMRAID_MINI */

	/* Version option. */
	{ 'V',
	  VERSION,
	  UNDEF,
#ifdef DMRAID_MINI
	  HELP,IGNORELOCKING,
#else
	  DBG|HELP|IGNORELOCKING|VERBOSE,
#endif
	  NO_ARGS,
	  NULL,
	  0,
	},
};

/*
 * Set action flag and call optional function.
 */
static int set_action(struct lib_context *lc, int o)
{
	struct actions *a;

	for (a = actions; a < ARRAY_END(actions); a++) {
		if (o == a->option) {
			action |= a->action;	/* Set action flag. */
			a->allowed |= a->action;/* Merge to allowed flags. */
			a->allowed |= a->needed;
			if (a->f_set)	/* Optionally call function. */
				return a->f_set(lc, a->arg);

			break;
		}
	}

	return 1;
}

/* Check for invalid option combinations */
static int check_actions(struct lib_context *lc, char **argv)
{
	struct actions *a;

	for (a = actions; a < ARRAY_END(actions); a++) {
		if (a->action & action) {
			if (a->needed != UNDEF &&
			    !(a->needed & action))
				LOG_ERR(lc, 0,
					"option missing/invalid option "
					"combination with -%c",
					a->option);

			if (~a->allowed & action)
				LOG_ERR(lc, 0, "Invalid option combination"
					   " (-h for help)");

			if (a->args == NO_ARGS && argv[optind])
				LOG_ERR(lc, 0,
					"No arguments allowed with -%c\n",
					a->option);
		}
	}

	if (!action)
		LOG_ERR(lc, 0, "Options missing\n");

#ifndef DMRAID_MINI
	if ((action & (DBG|VERBOSE)) == action)
		LOG_ERR(lc, 0, "More options needed with -d/-v");

	if (action & ERASE) {
		action |= DUMP;
		lc_inc_opt(lc, LC_DUMP);
	}
#endif

	return 1;
}

/* Check for invalid option argumengts. */
static int check_actions_arguments(struct lib_context *lc)
{
	if (valid_format(lc, OPT_STR_FORMAT(lc)))
		return 1;

	LOG_ERR(lc, 0, "Invalid format for -f at (see -l)");
}

/* Parse and handle the command line arguments */
int handle_args(struct lib_context *lc, int argc, char ***argv)
{
	int o, ret = 0;
#ifdef HAVE_GETOPTLONG
	int opt_idx;
#endif

	if (argc < 2)
		LOG_ERR(lc, 0, "No arguments/options given (-h for help)\n");

#ifdef HAVE_GETOPTLONG
	/* Walk the options (and option arguments) */
	while ((o = getopt_long(argc, *argv, short_opts,
				long_opts, &opt_idx)) != -1) {
#else
	while ((o = getopt(argc, *argv, short_opts)) != -1) {
#endif
		/* Help already displayed -> exit ok. */
		if ((ret = set_action(lc, o)) && (HELP & action))
			return 1;

		if (!ret || o == ':' || o == '?')
			return 0;
	}

	/* Force deactivation of stacked partition devices. */
	/* FIXME: remove partiton code in favour of kpartx ? */
	if (DEACTIVATE & action)
		action &= ~NOPARTITIONS;

	if ((ret = check_actions(lc, *argv)) && OPT_FORMAT(lc))
		ret = check_actions_arguments(lc);

	*argv += optind;

	return ret;
}

static int version(struct lib_context *lc, int arg)
{
	char v[80];

	dm_version(lc, v, sizeof(v));
	log_print(lc, "%s version:\t\t%s\n"
		      "%s library version:\t%s %s\n"
		      "device-mapper version:\t%s",
		      lc->cmd, DMRAID_VERSION,
		      lc->cmd, libdmraid_version(lc), libdmraid_date(lc), v);

	return 1;
}

/*********************************************************************
 * Perform pre/post functions for requested actions.
 */
/* Post Activate/Deactivate RAID set. */
#ifndef DMRAID_MINI
/* Pre and post display_set() functions. */
static int _display_sets_arg(int arg)
{
	return (action & ACTIVE) ?
	       D_ACTIVE : ((action & INACTIVE) ? D_INACTIVE : D_ALL);
}

static int _display_set(struct lib_context *lc, void *rs, int type)
{
	display_set(lc, rs, type, 0);

	return 1;
}

static int _display_sets(struct lib_context *lc, int type)
{
	process_sets(lc, _display_set, type, SETS);

	return 1;
}

static int _display_devices(struct lib_context *lc, int type)
{
	display_devices(lc, type);

	return 1;
}

static int _erase(struct lib_context *lc, int arg)
{
	return erase_metadata(lc);
}
#endif

/* Retrieve and build metadata. */
static int get_metadata(struct lib_context *lc, struct prepost *p, char **argv)
{
	if (!(M_DEVICE & p->metadata))
		return 1;

	if (!discover_devices(lc, OPT_DEVICES(lc) ? argv : NULL))
		LOG_ERR(lc, 0, "failed to discover devices");

	if(!count_devices(lc, DEVICE)) {
		log_print(lc, "no block devices found");
		return 1;
	}

	if (!(M_RAID & p->metadata))
		return 1;

#ifndef	DMRAID_MINI
	/* Discover RAID disks and keep RAID metadata (eg, hpt45x) */
	discover_raid_devices(lc, 
# ifdef	DMRAID_NATIVE_LOG
		    ((NATIVE_LOG|RAID_DEVICES) & action) ? argv : NULL);
# else
		    (RAID_DEVICES & action) ? argv : NULL);
# endif
#else
	discover_raid_devices(lc, NULL);
#endif
	if (!count_devices(lc, RAID)) {
		format_error(lc, "disks", argv);
		return 1;
	}

	if (M_SET & p->metadata) {
		/* Group RAID sets. */
		build_sets(lc, argv);
		if (!count_devices(lc, SET)) {
			format_error(lc, "sets", argv);
			return 0;
		}
	}

	return 1;
}

/*
 * Function abstraction which takes pre- and post-function calls
 * to prepare an argument in pre() to be used by post().
 *
 * perform() is the call handler for all functions which need metadata
 * as displaying, erasing and activation/deactivation of RAID sets.
 *
 * The necessary metadata describing disks, RAID devices and RAID sets
 * gets automatically generated by this function.
 *
 * A lock gets taken out in case of metadata accesses in order to
 * prevent multiple tool runs from occurring in parallel.
 * For now I just lock globally, which will change when I get to monitoring
 * of RAID sets, where finer grained locks on RAID sets need to be taken out.
 */

/*
 * Definition of pre- and post functions to perform.
 */
struct prepost prepost[] = {
	/* (De)activate RAID set. */
	{ ACTIVATE|DEACTIVATE,
	  M_DEVICE|M_RAID|M_SET,
	  ROOT,
	  LOCK,
	  NULL,
	  0,
	  activate_or_deactivate_sets,
	},

#ifndef DMRAID_MINI
	/* Display block devices. */
	{ BLOCK_DEVICES,
	  M_DEVICE,
	  ROOT,
	  NO_LOCK,
	  NULL,
	  DEVICE,
	  _display_devices,
	},

	/* Erase metadata. */
	{ ERASE,
	  M_DEVICE|M_RAID, 
	  ROOT,
	  LOCK,
	  NULL,
	  0,
	  _erase,
	},

	/* List metadata format handlers. */
	{ LIST_FORMATS,
	  M_NONE, 
	  ANY_ID,
	  NO_LOCK,
	  NULL,
	  0,
	  list_formats,
	},

#  ifdef DMRAID_NATIVE_LOG
	/* Native metadata log. */
	{ NATIVE_LOG,
	  M_DEVICE|M_RAID, 
	  ROOT,
	  LOCK,
	  NULL,
	  NATIVE,
	  _display_devices,
	},
#  endif

	/* Display RAID devices. */
	{ RAID_DEVICES,
	  M_DEVICE|M_RAID,
	  ROOT,
	  LOCK,
	  NULL,
	  RAID,
	  _display_devices,
	},

	/* Display RAID sets. */
	{ RAID_SETS,
	  M_DEVICE|M_RAID|M_SET,
	  ROOT,
	  LOCK,
	  _display_sets_arg,
	  0,
	  _display_sets,
	},
#endif

	/* Display version. */
	{ VERSION,
	  M_NONE,
	  ANY_ID,
	  NO_LOCK,
	  NULL,
	  0,
	  version,
	},
};

static int _perform(struct lib_context *lc, struct prepost *p,
		    char **argv)
{
	int ret = 0;

	if (ROOT == p->id && geteuid())
		LOG_ERR(lc, 0, "you must be root");
	
	/* Lock against parallel runs. Resource NULL for now. */
	if (LOCK == p->lock && !lock_resource(lc, NULL))
		LOG_ERR(lc, 0, "lock failure");
	
	if (get_metadata(lc, p, argv))
		ret = p->post(lc, p->pre ? p->pre(p->arg) : p->arg);
	
	if (LOCK == p->lock)
		unlock_resource(lc, NULL);
	
	return ret;
}

int perform(struct lib_context *lc, char **argv)
{
	struct prepost *p;

	/* Special case, because help can be asked for at any time. */
	if (HELP & action)
		return 1;

	/* Find appropriate action. */
	for (p = prepost; p < ARRAY_END(prepost); p++) {
		if (p->action & action)
			return _perform(lc, p, argv);
	}

	return 0;
}

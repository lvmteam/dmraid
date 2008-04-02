/*
 * Copyright (C) 2004,2005  Heinz Mauelshagen, Red Hat GmbH.
 *                          All rights reserved.
 *
 * See file LICENSE at the top of this source tree for license information.
 */

#ifndef _LIB_CONTEXT_H_
#define _LIB_CONTEXT_H_

#include <dmraid/list.h>
#include <dmraid/locking.h>
#include <dmraid/misc.h>

enum lc_lists {
	LC_FORMATS = 0,	/* Metadata format handlers. */
	LC_DISK_INFOS,	/* Disks discovered. */
	LC_RAID_DEVS,	/* Raid devices discovered. */
	LC_RAID_SETS,	/* Raid sets grouped. */
	/* Add new lists below here ! */
	LC_LISTS_SIZE,	/* Must be the last enumerator. */
};

/* List access macros. */
#define	LC_FMT(lc)	(lc_list((lc), LC_FORMATS))
#define	LC_DI(lc)	(lc_list((lc), LC_DISK_INFOS))
#define	LC_RD(lc)	(lc_list((lc), LC_RAID_DEVS))
#define	LC_RS(lc)	(lc_list((lc), LC_RAID_SETS))

enum lc_options {
	LC_COLUMN = 0,
	LC_DEBUG,
	LC_DUMP,
	LC_FORMAT,
	LC_GROUP,
	LC_SETS,
	LC_TEST,
	LC_VERBOSE,
	LC_IGNORELOCKING,
	LC_SEPARATOR,
	LC_DEVICES,
	LC_PARTCHAR,	  /* Add new options below this one ! */
	LC_OPTIONS_SIZE,  /* Must be the last enumerator. */
};

/* Options access macros. */
/* Return option counter. */
#define	OPT_COLUMN(lc)		(lc_opt(lc, LC_COLUMN))
#define	OPT_DEBUG(lc)		(lc_opt(lc, LC_DEBUG))
#define	OPT_DEVICES(lc)		(lc_opt(lc, LC_DEVICES))
#define	OPT_DUMP(lc)		(lc_opt(lc, LC_DUMP))
#define	OPT_GROUP(lc)		(lc_opt(lc, LC_GROUP))
#define	OPT_FORMAT(lc)		(lc_opt(lc, LC_FORMAT))
#define	OPT_IGNORELOCKING(lc)	(lc_opt(lc, LC_IGNORELOCKING))
#define	OPT_SEPARATOR(lc)	(lc_opt(lc, LC_SEPARATOR))
#define	OPT_SETS(lc)		(lc_opt(lc, LC_SETS))
#define	OPT_TEST(lc)		(lc_opt(lc, LC_TEST))
#define	OPT_VERBOSE(lc)		(lc_opt(lc, LC_VERBOSE))
#define	OPT_PARTCHAR(lc)	(lc_opt(lc, LC_PARTCHAR))

/* Return option value. */
#define	OPT_STR(lc, o)		(lc->options[o].arg.str)
#define	OPT_STR_COLUMN(lc)	OPT_STR(lc, LC_COLUMN)
#define	OPT_STR_FORMAT(lc)	OPT_STR(lc, LC_FORMAT)
#define	OPT_STR_SEPARATOR(lc)	OPT_STR(lc, LC_SEPARATOR)
#define	OPT_STR_PARTCHAR(lc)	OPT_STR(lc, LC_PARTCHAR)

struct lib_version {
	const char *text;
	const char *date;
	struct {
		unsigned int major;
		unsigned int minor;
		unsigned int sub_minor;
		const char *suffix;
	} v;
};

struct lib_options {
	int opt;
	union {
		const char *str;
		uint64_t u64;
		uint64_t u32;
	} arg;
};

struct lib_context {
	struct lib_version version;
	char *cmd;

	/* Option counters used throughout the library. */
	struct lib_options options[LC_OPTIONS_SIZE];

	/*
	 * Lists for:
	 *
	 *	o metadata format handlers the library supports
	 * 	o block devices discovered
	 * 	o RAID devices discovered
	 * 	o RAID sets grouped
	 */
	struct list_head lists[LC_LISTS_SIZE];

	char *locking_name;		/* Locking mechanism selector. */
	struct	locking *lock;		/* Resource locking. */

	mode_t	mode;			/* File/directrory create modes. */

	struct {
		const char *error;	/* For error mappings. */
	} path;
};

extern struct lib_context *alloc_lib_context(char **argv);
extern void free_lib_context(struct lib_context *lc);
extern int lc_opt(struct lib_context *lc, enum lc_options o);
const char *lc_opt_arg(struct lib_context *lc, enum lc_options o);
const char *lc_stralloc_opt(struct lib_context *lc, enum lc_options o,
			   char *arg);
const char *lc_strcat_opt(struct lib_context *lc, enum lc_options o,
			  char *arg, const char delim);
extern int lc_inc_opt(struct lib_context *lc, int o);
extern struct list_head *lc_list(struct lib_context *lc, int l);

extern const char *libdmraid_date(struct lib_context *lc);
extern const char *libdmraid_version(struct lib_context *lc);


#endif

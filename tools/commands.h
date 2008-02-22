/*
 * Copyright (C) 2004,2005  Heinz Mauelshagen, Red Hat GmbH.
 *                          All rights reserved.
 *
 * See file LICENSE at the top of this source tree for license information.
 */

#ifndef	_COMMANDS_H
#define	_COMMANDS_H

#include <dmraid/lib_context.h>

#define ARRAY_SIZE(a)	(sizeof(a) / sizeof(*a))
#define ARRAY_END(a)	(a + ARRAY_SIZE(a))

/* Options actions dmraid performs. */
enum action {
	UNDEF		= 0x0,
	ACTIVATE	= 0x1,
	DEACTIVATE	= 0x2,
	FORMAT		= 0x4,
#ifndef	DMRAID_MINI
	BLOCK_DEVICES	= 0x8,
	COLUMN		= 0x10,
	DBG		= 0x20,
	DUMP		= 0x40,
	ERASE		= 0x80,
	GROUP		= 0x100,
#endif
	HELP		= 0x200,
#ifndef	DMRAID_MINI
	LIST_FORMATS	= 0x400,
#  ifdef	DMRAID_NATIVE_LOG
	NATIVE_LOG	= 0x800,
#  endif
#endif
	NOPARTITIONS	= 0x1000,
#ifndef	DMRAID_MINI
	RAID_DEVICES	= 0x2000,
	RAID_SETS	= 0x4000,
	TEST		= 0x8000,
	VERBOSE		= 0x10000,
	ACTIVE		= 0x20000,
	INACTIVE	= 0x40000,
	SEPARATOR	= 0x80000,
#endif
	VERSION		= 0x100000,
	IGNORELOCKING	= 0x200000,
};

#define	ALL_FLAGS	((enum action) -1)

/* Arguments allowed ? */
enum args {
	NO_ARGS,
	ARGS,
};

/*
 * Action flag definitions for set_action().
 *
 * 'Early' options can be handled directly in set_action() by calling
 * the functions registered here (f_set member) handing in arg.
 */
struct actions {
	int option;		/* Option character/value. */
	enum action action;	/* Action flag for this option or UNDEF. */
	enum action needed;	/* Mandatory options or UNDEF if alone */
	enum action allowed;	/* Allowed flags (ie, other options allowed) */

	enum args args;		/* Arguments allowed ? */

	/* Function to call on hit or NULL */
	int (*f_set)(struct lib_context *lc, int arg);
	int arg;		/* Argument for above function call */
};

/* Define which metadata is needed before we can call post functions. */
enum metadata_need {
	M_NONE	 = 0x00,
	M_DEVICE = 0x01,
	M_RAID	 = 0x02,
	M_SET	 = 0x04,
};

enum id {
	ROOT,
	ANY_ID,
};

enum lock {
	LOCK,
	NO_LOCK,
};

/* 
 * Pre and Post functions to perform for an option.
 */
struct prepost {
	enum action action;
	enum metadata_need metadata;
	enum id id;
	enum lock lock;
	int (*pre)(int arg);
	int arg;
	int (*post)(struct lib_context *lc, int arg);
};

int handle_args(struct lib_context *lc, int argc, char ***argv);
int perform(struct lib_context *lc, char **argv);

#endif

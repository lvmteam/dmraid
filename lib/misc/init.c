/*
 * Copyright (C) 2004,2005  Heinz Mauelshagen, Red Hat GmbH.
 *                          All rights reserved.
 *
 * See file LICENSE at the top of this source tree for license information.
 */

#include "internal.h"

/* Library initialization. */
struct lib_context *libdmraid_init(int argc, char **argv)
{
	struct lib_context *lc;

	if ((lc = alloc_lib_context(argv))) {
		if (!register_format_handlers(lc)) {
			libdmraid_exit(lc);
			lc = NULL;
		} else
			/* FIXME: do we need this forever ? */
			sysfs_workaround(lc);
	}

	return lc;
}

/* Library exit processing. */
void libdmraid_exit(struct lib_context *lc)
{
	free_raid_set(lc, NULL);	/* Free all RAID sets. */
	free_raid_dev(lc, NULL);	/* Free all RAID devices. */
	free_dev_info(lc, NULL);	/* Free all disk infos. */
	unregister_format_handlers(lc);	/* Unregister all format handlers. */
	free_lib_context(lc);		/* Release library context. */
}

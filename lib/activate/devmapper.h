/*
 * Copyright (C) 2004,2005  Heinz Mauelshagen, Red Hat GmbH.
 *                          All rights reserved.
 *
 * See file LICENSE at the top of this source tree for license information.
 */

#ifndef _DEVMAPPER_H_
#define _DEVMAPPER_H

char *mkdm_path(struct lib_context *lc, const char *name);
int dm_create(struct lib_context *lc, struct raid_set *rs, char *table);
int dm_remove(struct lib_context *lc, struct raid_set *rs);
int dm_status(struct lib_context *lc, struct raid_set *rs);
int dm_version(struct lib_context *lc, char *version, size_t size);

#endif

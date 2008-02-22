/*
 * Copyright (C) 2006 IBM, all rights reserved.
 * Written by Darrick Wong <djwong@us.ibm.com>,
 * James Simshaw <simshawj@us.ibm.com>, and
 * Adam DiCarlo <bikko@us.ibm.com>
 *
 * Copyright (C) 2006 Heinz Mauelshagen, Red Hat GmbH
 * 		      All rights reserved
 *
 * See file LICENSE at the top of this source tree for license information.
 */
#include "internal.h"

#define	add_to_log(entry, log)	\
	list_add_tail(&(entry)->changes, &(log));

static inline int alloc_entry(struct change **entry)
{
	return (*entry = dbg_malloc(sizeof (*entry))) ? 0 : -ENOMEM;
}

static int nuke_spare(struct lib_context *lc, struct raid_dev *rd)
{
	printf("Nuking Spare\n");
	list_del_init(&rd->devs);
	return 0;
}

/* Add a device to a RAID1 set and start the resync */
static int add_dev_to_raid1(struct lib_context *lc, struct raid_set *rs,
			    struct raid_dev *rd)
{
	int ret;
	struct raid_dev *tmp;
	struct change *entry;
	LIST_HEAD(log); /* playback log */

	/* Add device to the raid set */
	ret = alloc_entry(&entry);
	if (ret)
		goto err;

	entry->type = ADD_TO_SET;
	entry->rs = rs;
	entry->rd = rd;
	add_to_log(entry, log);
	list_add_tail(&rd->devs, &rs->devs);
	rd->type = t_raid1;

	/* Check that this is a sane configuration */
	list_for_each_entry(tmp, &rs->devs, devs) {
		ret = tmp->fmt->check(lc, rs);
		if (ret)
			goto err;
	}

	/* Write the metadata of the drive we're adding _first_ */
	ret = alloc_entry(&entry);
	if (ret)
		goto err;

	entry->type = WRITE_METADATA;
	entry->rd = rd;
	add_to_log(entry, log);
	ret = write_dev(lc, rd, 0);
	if (!ret)
		goto err;

	/* Write metadatas of every device in the set */
	list_for_each_entry(tmp, &rs->devs, devs) {
		if (tmp != rd) {
			ret = alloc_entry(&entry);
			if (ret)
				goto err;

			entry->type = WRITE_METADATA;
			entry->rd = tmp;
			add_to_log(entry, log);
			ret = write_dev(lc, tmp, 0);
			if (!ret)
				goto err;
		}
	}

	/* Reconfigure device mapper */
	// FIXME: is nosync enough? rs->status |= s_inconsistent;
	rs->status |= s_nosync;
	change_set(lc, A_ACTIVATE, rs);
	ret = change_set(lc, A_RELOAD, rs);
	// FIXME: might need this later: change_set(lc, A_DEACTIVATE,rs);
	if (!ret)
		goto err;

	/* End transaction */
	end_log(lc, &log);
	return 0;

err:
	revert_log(lc, &log);
	return ret;
}

/* Remove a disk from a raid1 */
static int del_dev_in_raid1(struct lib_context *lc, struct raid_set *rs,
			    struct raid_dev *rd)
{
	int ret;
	struct raid_dev *tmp;
	struct change *entry;
	LIST_HEAD(log); /* Playback log */

	/* Remove device from the raid set */
	ret = alloc_entry(&entry);
	if (ret)
		goto err;

	entry->type = DELETE_FROM_SET;
	entry->rs = rs;
	entry->rd = rd;
	add_to_log(entry, log);
	list_del_init(&rd->devs);
	rd->type = t_spare;

	/* Check that this is a sane configuration */
	list_for_each_entry(tmp, &rs->devs, devs) {
		ret = tmp->fmt->check(lc, rs);
		if (ret)
			goto err;
	}

	/* Write the metadata of the drive we're removing _first_ */
	ret = alloc_entry(&entry);
	if (ret)
		goto err;

	entry->type = WRITE_METADATA;
	entry->rd = rd;
	add_to_log(entry, log);
	ret = write_dev(lc, rd, 0);
	if (!ret)
		goto err;

	/* Write metadatas of every device in the set */
	list_for_each_entry(tmp, &rs->devs, devs) {
		if (tmp == rd)
			continue;

		ret = alloc_entry(&entry);
		if (ret)
			goto err;

		entry->type = WRITE_METADATA;
		entry->rd = tmp;
		add_to_log(entry, log);
		ret = write_dev(lc, tmp, 0);
		if (!ret)
			goto err;
	}

	/* Reconfigure device mapper */
	rs->status |= s_inconsistent;
	rs->status |= s_nosync;
	ret = change_set(lc, A_RELOAD, rs);
	if (!ret)
		goto err;

	/* End transaction */
	end_log(lc, &log);
	return 0;

err:
	revert_log(lc, &log);
	return ret;
}

/* Corelate type and function to handle addition/removel of RAID device */
struct handler {
	enum change_type type;
	int (*func) (struct lib_context *lc, struct raid_set *rs,
		     struct raid_dev *rd);
};

/* Call the function to handle addition/removal of a RAID device */
static int handle_dev(struct lib_context *lc, struct handler *h,
		      struct raid_set *rs, struct raid_dev *rd)
{
	do {
		if (h->type == rs->type)
			return h->func(lc, rs, rd);
	} while ((++h)->type != t_undef);

	LOG_ERR(lc, -ENOENT, "%s: no handler for %x", __func__, rs->type);
}

/* Add a disk to an array. */
int add_dev_to_set(struct lib_context *lc, struct raid_set *rs,
		   struct raid_dev *rd)
{
	struct handler handlers[] = {
		{t_raid1, add_dev_to_raid1},
		{t_undef, NULL},
	};

	if (T_SPARE(rd))
		nuke_spare(lc, rd);
	else if (!list_empty(&rd->devs))
		LOG_ERR(lc, -EBUSY, "%s: disk already in another set!",
			__func__);

	if (T_GROUP(rd))
		LOG_ERR(lc, -EISDIR,
			"%s: can't add a group raid_dev to a raid_set.",
			__func__);

	return handle_dev(lc, handlers, rs, rd);
}

/* Remove a disk from an array */
int del_dev_in_set(struct lib_context *lc, struct raid_set *rs,
		   struct raid_dev *rd)
{
	struct handler handlers[] = {
		{t_raid1, del_dev_in_raid1},
		{t_undef, NULL},
	};

	if (list_empty(&rd->devs))
		LOG_ERR(lc, -EBUSY, "%s: disk is not in a set!", __func__);

	/* FIXME: Not sure if this is true. */
	if (T_GROUP(rd))
		LOG_ERR(lc, -EISDIR,
			"%s: can't remove a group raid_dev from a raid_set.",
			__func__);

	return handle_dev(lc, handlers, rs, rd);
}

/*
 * Copyright (C) 2004,2005  Heinz Mauelshagen, Red Hat GmbH.
 *                          All rights reserved.
 *
 * See file LICENSE at the top of this source tree for license information.
 */

/*
 * Activate/Deactivate code for hierarchical RAID Sets.
 */

#include "internal.h"
#include "devmapper.h"

static int valid_rd(struct raid_dev *rd)
{
	return S_OK(rd->status) && !T_SPARE(rd);
}

static int valid_rs(struct raid_set *rs)
{
	return S_OK(rs->status) && !T_SPARE(rs);
}

/* Return rounded size in case of unbalanced mappings */
static uint64_t maximize(struct raid_set *rs, uint64_t sectors,
			 uint64_t last, uint64_t min)
{
	return sectors > min ? min(last, sectors) : last;
}

/* Find smallest set/disk larger than given minimum. */
static uint64_t _smallest(struct lib_context *lc,
			  struct raid_set *rs, uint64_t min)
{
	uint64_t ret = ~0;
	struct raid_set *r;
	struct raid_dev *rd;

	list_for_each_entry(r, &rs->sets, list)
		ret = maximize(r, total_sectors(lc, r), ret, min);

	list_for_each_entry(rd, &rs->devs, devs) {
		if (!T_SPARE(rd))
			ret = maximize(rs, rd->sectors, ret, min);
	}

	return ret == (uint64_t) ~0 ? 0 : ret;
}

/*
 * Definitions of mappings.
 */

/* Undefined/-supported mapping. */
static int _dm_un(struct lib_context *lc, char **table,
		  struct raid_set *rs, const char *what)
{
	LOG_ERR(lc, 0, "Un%sed RAID type %s[%u] on %s", what,
		get_set_type(lc, rs), rs->type, rs->name);
}

static int dm_undef(struct lib_context *lc, char **table, struct raid_set *rs)
{
	return _dm_un(lc, table, rs, "defin");
}

static int dm_unsup(struct lib_context *lc, char **table, struct raid_set *rs)
{
	return _dm_un(lc, table, rs, "support");
}


/* "Spare mapping". */
static int dm_spare(struct lib_context *lc, char **table, struct raid_set *rs)
{
	LOG_ERR(lc, 0, "spare set");
}

/* Push path and offset onto a table. */
static int _dm_path_offset(struct lib_context *lc, char **table,
			   int valid, const char *path, uint64_t offset)
{
	return p_fmt(lc, table, " %s %U",
		     valid ? path : lc->path.error, offset);
}

/*
 * Create dm table for linear mapping.
 */
static int _dm_linear(struct lib_context *lc, char **table, int valid,
		      const char *path, uint64_t start, uint64_t sectors,
		      uint64_t offset)
{
	return p_fmt(lc, table, "%U %U %s", start, sectors,
		     get_dm_type(lc, t_linear)) ?
		     _dm_path_offset(lc, table, valid, path, offset) : 0;
}

static int dm_linear(struct lib_context *lc, char **table,
		     struct raid_set *rs)
{
	unsigned int segments = 0;
	uint64_t start = 0, sectors = 0;
	struct raid_dev *rd;
	struct raid_set *r;

	/* Stacked linear sets. */
	list_for_each_entry(r, &rs->sets, list) {
		if (!T_SPARE(r)) {
			int ret;
			char *path;

			if (!(path = mkdm_path(lc, r->name)))
				goto err;
	
			sectors = total_sectors(lc, r);
			ret = _dm_linear(lc, table, valid_rs(r), path,
					 start, sectors, 0);
			dbg_free(path);
			segments++;
			start += sectors;
	
			if (!ret ||
			    (r->sets.next != &rs->sets &&
			     !p_fmt(lc, table, "\n")))
				goto err;
		}
	}

	/* Devices of a linear set. */
	list_for_each_entry(rd, &rs->devs, devs) {
		if (!T_SPARE(rd)) {
			if (!_dm_linear(lc, table, valid_rd(rd), rd->di->path,
					start, rd->sectors, rd->offset))
				goto err;
	
			segments++;
			start += rd->sectors;

			if (rd->devs.next != &rs->devs &&
			    !p_fmt(lc, table, "\n"))
				goto err;
		}
	}

	return segments ? 1 : 0;

   err:
	return log_alloc_err(lc, __func__);
}

/*
 * Create dm table for a partition mapping.
 *
 * Partitioned RAID set with 1 RAID device
 * defining a linear partition mapping.
 */
static int dm_partition(struct lib_context *lc, char **table,
			struct raid_set *rs)
{
	return dm_linear(lc, table, rs);
}

/*
 * Create dm table for striped mapping taking
 * different disk sizes and the stride size into acccount.
 *
 * If metadata format handler requests a maximized mapping,
 * more than one mapping table record will be created and
 * stride boundaries will get paid attention to.
 *
 * Eg, 3 disks of 80, 100, 120 GB capacity:
 *
 * 0     240GB striped /dev/sda 0 /dev/sdb 0 /dev/sdc 0 
 * 240GB 40GB  striped /dev/sdb 80GB /dev/sdc 80GB
 * 280GB 20GB  linear /dev/sdc 100GB
 *
 */
/* Push begin of line onto a RAID0 table. */
static int _dm_raid0_bol(struct lib_context *lc, char **table,
			 uint64_t min, uint64_t last_min,
			 unsigned int n, unsigned int stride)
{
	return p_fmt(lc, table,
		     n > 1 ? "%U %U %s %u %u" : "%U %U %s",
		     last_min * n, (min - last_min) * n,
		     get_dm_type(lc, n > 1 ? t_raid0 : t_linear),
		     n, stride);
}

/* Push end of line onto a RAID0 table. */
static int _dm_raid0_eol(struct lib_context *lc,
			 char **table, struct raid_set *rs,
			 unsigned int *stripes, uint64_t last_min)
{
	struct raid_set *r;
	struct raid_dev *rd;

	/* Stacked striped sets. */
	list_for_each_entry(r, &rs->sets, list) {
		if (total_sectors(lc, r) > last_min) {
			int ret;
			char *path;

			if (!(path = mkdm_path(lc, r->name)))
				goto err;

			ret = _dm_path_offset(lc, table, valid_rs(r),
					      path, last_min);
			dbg_free(path);

			if (!ret)
				goto err;

			(*stripes)++;
		}
	}
	
	list_for_each_entry(rd, &rs->devs, devs) {
		if (!T_SPARE(rd) &&
		    rd->sectors > last_min &&
		    !_dm_path_offset(lc, table, valid_rd(rd), rd->di->path,
				     rd->offset + last_min))
			goto err;

		(*stripes)++;
	}

	return 1;

   err:
	return 0;
}

/* Count RAID sets/devices larger than given minimum size. */
static unsigned int _dm_raid_devs(struct lib_context *lc,
				  struct raid_set *rs, uint64_t min)
{
	unsigned int ret = 0;
	struct raid_set *r;
	struct raid_dev *rd;

	/* Stacked sets. */
	list_for_each_entry(r, &rs->sets, list) {
		if (!T_SPARE(r) && total_sectors(lc, r) > min)
			ret++;
	}
	
	list_for_each_entry(rd, &rs->devs, devs) {
		if (!T_SPARE(rd) && rd->sectors > min)
			ret++;
	}

	return ret;
}
	
static int dm_raid0(struct lib_context *lc, char **table,
		    struct raid_set *rs)
{
	unsigned int stripes = 0;
	uint64_t min, last_min = 0;

	for (; (min = _smallest(lc, rs, last_min)); last_min = min) {
		if (last_min && !p_fmt(lc, table, "\n"))
			goto err;

		if (!_dm_raid0_bol(lc, table, round_down(min, rs->stride),
				   last_min, _dm_raid_devs(lc, rs, last_min),
				   rs->stride) ||
		    !_dm_raid0_eol(lc, table, rs, &stripes, last_min))
			goto err;

		if (!F_MAXIMIZE(rs))
			break;
	}

	return stripes ? 1 : 0;

   err:
	return log_alloc_err(lc, __func__);
}

/*
 * Create dm table for mirrored mapping.
 */

/* Calculate dirty log region size. */
static unsigned int calc_region_size(struct lib_context *lc, uint64_t sectors)
{
	const unsigned int mb_128 = 128*2*1024;
	unsigned int max, region_size;

	if ((max = sectors / 1024) > mb_128)
		max = mb_128;

	for (region_size = 128; region_size < max; region_size <<= 1);

	return region_size >> 1;
}

static unsigned int get_rds(struct raid_set *rs, int valid)
{
	unsigned int ret = 0;
	struct raid_dev *rd;

	list_for_each_entry(rd, &rs->devs, devs) {
		if (valid) {
			if (valid_rd(rd))
				ret++;
		} else
			ret++;
	}

	return ret;
}

static unsigned int get_dm_devs(struct raid_set *rs, int valid)
{
	unsigned int ret = 0;
	struct raid_set *r;

	/* Stacked mirror sets. */
	list_for_each_entry(r, &rs->sets, list) {
		if (valid) {
			if (valid_rs(r))
				ret++;
		} else
			ret++;
	}

	ret+= get_rds(rs, valid);

	return ret;
}

/* Push begin of line onto a RAID1 table. */
/* FIXME: persistent dirty log. */
static int _dm_raid1_bol(struct lib_context *lc, char **table,
			 struct raid_set *rs,
			 uint64_t sectors, unsigned int mirrors)
{
	return (p_fmt(lc, table, "0 %U %s core 2 %u %s %u",
		      sectors, get_dm_type(lc, t_raid1),
		      calc_region_size(lc, sectors),
		      (S_INCONSISTENT(rs->status) || S_NOSYNC(rs->status)) ?
		      "sync" : "nosync", mirrors));
}

static int dm_raid1(struct lib_context *lc, char **table, struct raid_set *rs)
{
	uint64_t sectors = 0;
	unsigned int mirrors = get_dm_devs(rs, 1);
	struct raid_set *r;
	struct raid_dev *rd;

	switch (mirrors) {
	case 0:
		return 0;

	case 1:
		/*
		 * In case we only have one mirror left,
		 * a linear mapping will do.
		 */
		log_err(lc, "creating degraded mirror mapping for \"%s\"",
			rs->name);
		return dm_linear(lc, table, rs);
	}
		
	if (!(sectors = _smallest(lc, rs, 0)))
		LOG_ERR(lc, 0, "can't find smallest mirror!");

	if (!_dm_raid1_bol(lc, table, rs, sectors, mirrors))
		goto err;

	/* Stacked mirror sets. */
	list_for_each_entry(r, &rs->sets, list) {
		if (valid_rs(r)) {
			int ret;
			char *path;

			if (!(path = mkdm_path(lc, r->name)))
				goto err;

			ret = _dm_path_offset(lc, table, 1, path, 0);
			dbg_free(path);

			if (!ret)
				goto err;
		}
	}

	/* Lowest level mirror devices */
	list_for_each_entry(rd, &rs->devs, devs) {
		if (valid_rd(rd) &&
		    !_dm_path_offset(lc, table, 1, rd->di->path, rd->offset))
			goto err;
	}

	/* Append the flag/feature required for dmraid1 
	 * event handling in the kernel driver 
	 */
	if(p_fmt(lc, table, " 1 handle_errors"))
		return 1;

   err:
	return log_alloc_err(lc, __func__);
}

/*
 * Create dm table for RAID5 mapping.
 */

/* Push begin of line onto a RAID5 table. */
/* FIXME: persistent dirty log. */
static int _dm_raid45_bol(struct lib_context *lc, char **table,
			 struct raid_set *rs,
			 uint64_t sectors, unsigned int members)
{
	return p_fmt(lc, table, "0 %U %s core 2 %u %s %s 1 %u %u -1",
		     sectors, get_dm_type(lc, rs->type),
		     calc_region_size(lc, total_sectors(lc, rs) / _dm_raid_devs(lc, rs, 0)),
		     (S_INCONSISTENT(rs->status) || S_NOSYNC(rs->status)) ?
		     "sync" : "nosync",
		     get_type(lc, rs->type), rs->stride, members);
}

static int dm_raid45(struct lib_context *lc, char **table, struct raid_set *rs)
{
	uint64_t sectors = 0;
	unsigned int members = get_dm_devs(rs, 0);
	struct raid_dev *rd;
	struct raid_set *r;

	if (!(sectors = _smallest(lc, rs, 0)))
		LOG_ERR(lc, 0, "can't find smallest RAID4/5 member!");

	/* Adjust sectors with chunk size: only whole chunks count */
	sectors = sectors / rs->stride * rs->stride;

	/*
	 * Multiply size of smallest member by the number of data
	 * devices to get the total sector count for the mapping.
	 */
	sectors *= members - 1;

	if (!_dm_raid45_bol(lc, table, rs, sectors, members))
		goto err;

	/* Stacked RAID sets (for RAID50 etc.) */
	list_for_each_entry(r, &rs->sets, list) {
		int ret;
		char *path;

		if (!(path = mkdm_path(lc, r->name)))
			goto err;

		ret = _dm_path_offset(lc, table, valid_rs(r), path, 0);
		dbg_free(path);

		if (!ret)
			goto err;
	}

	/* Lowest level RAID devices */
	list_for_each_entry(rd, &rs->devs, devs) {
		    if (!_dm_path_offset(lc, table, valid_rd(rd), rd->di->path,
					 rd->offset))
			goto err;
	}

	return 1;

   err:
	return log_alloc_err(lc, __func__);
}

/*
 * Activate/deactivate (sub)sets.
 */

/*
 * Array of handler functions for the various types.
 */
static struct type_handler {
	const enum type type;
	int(*f)(struct lib_context *lc, char **table, struct raid_set *rs);
} type_handler[] = {
	{ t_undef, dm_undef }, /* Needs to stay here! */
	{ t_partition, dm_partition },
	{ t_spare, dm_spare },
	{ t_linear, dm_linear },
	{ t_raid0, dm_raid0 },
	{ t_raid1, dm_raid1 },
	{ t_raid4, dm_raid45 },
	{ t_raid5_ls, dm_raid45 },
	{ t_raid5_rs, dm_raid45 },
	{ t_raid5_la, dm_raid45 },
	{ t_raid5_ra, dm_raid45 },
	/* RAID types below not supported (yet) */
	{ t_raid6, dm_unsup },
};

/* Retrieve type handler from array. */
static struct type_handler *handler(struct raid_set *rs)
{
	struct type_handler *th = type_handler;

	do {
		if (rs->type == th->type)
			return th;
	} while (++th < ARRAY_END(type_handler));

	return type_handler;
}

/* Return mapping table */
char *libdmraid_make_table(struct lib_context *lc, struct raid_set *rs)
{
	char *ret = NULL;

	if (T_GROUP(rs))
		return NULL;

	if (!(handler(rs))->f(lc, &ret, rs))
		LOG_ERR(lc, NULL, "no mapping possible for RAID set %s",
			rs->name);

	return ret;
}


enum dm_what { DM_ACTIVATE, DM_REGISTER};

/* Register devices of the RAID set with the dmeventd. */
/* REMOVEME: dummy functions once linking to the real ones. */
#define	ALL_EVENTS 0xffffffff
static int dm_register_for_event(char *a, char *b, int c)
{
	return 1;
}

static int dm_unregister_for_event(char *a, char *b, int c)
{
	return 1;
}

static int do_device(struct lib_context *lc, struct raid_set *rs,
		     int (*f)()) // char *, char *, enum event_type))
{
	int ret = 0;
	struct raid_dev *rd;

	if (OPT_TEST(lc))
		return 1;

	return 1; /* REMOVEME: */

	list_for_each_entry(rd, &rs->devs, devs) {
		if (!(ret = f("dmraid", rd->di->path, ALL_EVENTS)))
			break;
	}

	return ret ? 1 : 0;
}

static int register_devices(struct lib_context *lc, struct raid_set *rs)
{
	return do_device(lc, rs, dm_register_for_event);
}

/* Unregister devices of the RAID set with the dmeventd. */
static int unregister_devices(struct lib_context *lc, struct raid_set *rs)
{
	return do_device(lc, rs, dm_unregister_for_event);
}

/* Reload a single set. */
static int reload_subset(struct lib_context *lc, struct raid_set *rs)
{
	int ret = 0;
	char *table = NULL;

	if (T_GROUP(rs))
		return 1;

	/* Suspend device */
	if (!(ret = dm_suspend(lc, rs)))
		LOG_ERR(lc, ret, "Device suspend failed.");

	/* Call type handler */
	if ((ret = (handler(rs))->f(lc, &table, rs))) {
		if (OPT_TEST(lc))
			display_table(lc, rs->name, table);
		else
			ret = dm_reload(lc, rs, table);
	} else
		log_err(lc, "no mapping possible for RAID set %s", rs->name);

	free_string(lc, &table);

	/* Try to resume */
	if (ret)
		dm_resume(lc, rs);
	else
		if (!(ret = dm_resume(lc, rs)))
			LOG_ERR(lc, ret, "Device resume failed.");

	return ret;
}

/* Reload a RAID set recursively (eg, RAID1 on top of RAID0). */
static int reload_set(struct lib_context *lc, struct raid_set *rs)
{
	struct raid_set *r;

	/* FIXME: Does it matter if the set is (in)active? */
#if 0
	if (!OPT_TEST(lc) &&
	    what == DM_ACTIVATE &&
	    dm_status(lc, rs)) {
		log_print(lc, "RAID set \"%s\" already active", rs->name);
		return 1;
	}
#endif

	/* Recursively walk down the chain of stacked RAID sets */
	list_for_each_entry(r, &rs->sets, list) {
		/* Activate set below this one */
		if (!reload_set(lc, r) && !T_GROUP(rs))
			return 0;
	}

	return reload_subset(lc, rs);
}

/* Activate a single set. */
static int activate_subset(struct lib_context *lc, struct raid_set *rs,
			   enum dm_what what)
{
	int ret = 0;
	char *table = NULL;

	if (T_GROUP(rs))
		return 1;

	if (what == DM_REGISTER)
		return register_devices(lc, rs);

	/* Call type handler */
	if ((ret = (handler(rs))->f(lc, &table, rs))) {
		if (OPT_TEST(lc))
			display_table(lc, rs->name, table);
		else
			ret = dm_create(lc, rs, table);
	} else
		log_err(lc, "no mapping possible for RAID set %s", rs->name);

	free_string(lc, &table);

	return ret;
}

/* Activate a RAID set recursively (eg, RAID1 on top of RAID0). */
static int activate_set(struct lib_context *lc, struct raid_set *rs,
			enum dm_what what)
{
	struct raid_set *r;

	if (!OPT_TEST(lc) &&
	    what == DM_ACTIVATE &&
	    dm_status(lc, rs)) {
		log_print(lc, "RAID set \"%s\" already active", rs->name);
		return 1;
	}

	/* Recursively walk down the chain of stacked RAID sets */
	list_for_each_entry(r, &rs->sets, list) {
		/* Activate set below this one */
		if (!activate_set(lc, r, what) && !T_GROUP(rs))
			return 0;
	}

	return activate_subset(lc, rs, what);
}

/* Deactivate a single set (one level of a device stack). */
static int deactivate_superset(struct lib_context *lc, struct raid_set *rs,
			       enum dm_what what)
{
	int ret = 1, status;

	if (what == DM_REGISTER)
		return unregister_devices(lc, rs);
		
	status = dm_status(lc, rs);
	if (OPT_TEST(lc))
		log_print(lc, "%s [%sactive]", rs->name, status ? "" : "in");
	else if (status)
		ret = dm_remove(lc, rs);
	else {
		log_print(lc, "RAID set \"%s\" is not active", rs->name);
		ret = 1;
	}

	return ret;
}

/* Deactivate a RAID set. */
static int deactivate_set(struct lib_context *lc, struct raid_set *rs,
			  enum dm_what what)
{
	struct raid_set *r;

	/*
	 * Deactivate myself if not a group set,
	 * which gets never activated itself.
	 */
	if (!T_GROUP(rs) &&
	    !deactivate_superset(lc, rs, what))
		return 0;

	/* Deactivate any subsets recursively. */
	list_for_each_entry(r, &rs->sets, list) {
		if (!deactivate_set(lc, r, what))
			return 0;
	}

	return 1;
}


/* External (de)activate interface. */
int change_set(struct lib_context *lc, enum activate_type what, void *v)
{
	int ret = 0;
	struct raid_set *rs = v;

	switch (what) {
	case A_ACTIVATE:
		ret = activate_set(lc, rs, DM_ACTIVATE) &&
		      activate_set(lc, rs, DM_REGISTER);
		break;

	case A_DEACTIVATE:
		ret = deactivate_set(lc, rs, DM_REGISTER) &&
		      deactivate_set(lc, rs, DM_ACTIVATE);
		break;

	case A_RELOAD:
		ret = reload_set(lc, rs);
		break;
	}

	return ret;
}

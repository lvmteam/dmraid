/*
 * Copyright (C) 2004,2005  Heinz Mauelshagen, Red Hat GmbH.
 *                          All rights reserved.
 *
 * See file LICENSE at the top of this source tree for license information.
 */

#include "internal.h"
#include "activate/devmapper.h"

/*
 * Type -> ascii definitions.
 *
 * dsp_ascii : the string used for display purposes (eg, "dmraid -s").
 * dm_ascii  :         "       in device-mapper tables as the target keyword.
 */
static const struct {
	const enum type type;
	const char *dsp_ascii;
	const char *dm_ascii;
} ascii_type[] = {
	/* enum        text         dm-target id */
	{ t_undef,     NULL,        NULL },
	{ t_group,     "GROUP",	    NULL },
	{ t_partition, "partition", NULL },
	{ t_spare,     "spare",     NULL },
	{ t_linear,    "linear",    "linear" },
	{ t_raid0,     "stripe",    "striped" },
	{ t_raid1,     "mirror",    "mirror" },
	{ t_raid4,     "raid4",     "raid45" },
	{ t_raid5_ls,  "raid5_ls",  "raid45" },
	{ t_raid5_rs,  "raid5_rs",  "raid45" },
	{ t_raid5_la,  "raid5_la",  "raid45" },
	{ t_raid5_ra,  "raid5_ra",  "raid45" },
	{ t_raid6,     "raid6",     NULL },
};

static const char *stacked_ascii_type[][5] = {
	{ "raid10", "raid30", "raid40", "raid50", "raid60" },
	{ "raid01", "raid03", "raid04", "raid05", "raid06" },
};

/*
 * State definitions.
 */
static const struct {
	const enum status status;
	const char *ascii;
} ascii_status[] = {
	{ s_undef,        NULL },
	{ s_setup,        "setup" },
	{ s_broken,       "broken" },
	{ s_inconsistent, "inconsistent" },
	{ s_nosync,       "nosync" },
	{ s_ok,           "ok" },
};

/* Fetch the respective ASCII string off the types array. */
static unsigned int get_type_index(enum type type)
{
	unsigned int ret = ARRAY_SIZE(ascii_type);

	while (ret--) {
		if (type & ascii_type[ret].type)
			return ret;
	}

	return 0;
}

const char *get_type(struct lib_context *lc, enum type type)
{
	return ascii_type[get_type_index(type)].dsp_ascii;
}

const char *get_dm_type(struct lib_context *lc, enum type type)
{
	return ascii_type[get_type_index(type)].dm_ascii;
}

/* Return the RAID type of a stacked RAID set (eg, raid10). */
static const char *get_stacked_type(void *v)
{
	struct raid_set *rs = v;
	unsigned int t = (T_RAID0(rs) ? get_type_index((RS_RS(rs))->type) :
					get_type_index(rs->type))
			 - get_type_index(t_raid1);

	return stacked_ascii_type[T_RAID0(rs) ? 1 : 0]
				 [t > t_raid0 ? t_undef : t];
}

/* Check, if a RAID set is stacked (ie, hierachical). */
static inline int is_stacked(struct raid_set *rs)
{
	return !T_GROUP(rs) && SETS(rs);
}

/* Return the ASCII type for a RAID set. */
const char *get_set_type(struct lib_context *lc, void *v)
{
	struct raid_set *rs = v;

	/* Check, if a RAID set is stacked. */
	return is_stacked(rs) ? get_stacked_type(rs) : get_type(lc, rs->type);
}

/* Fetch the respective ASCII string off the state array. */
const char *get_status(struct lib_context *lc, enum status status)
{
	unsigned int i = ARRAY_SIZE(ascii_status);

	while (i-- && !(status & ascii_status[i].status));

	return ascii_status[i].ascii;
}

/*
 * Calculate the size of the set by recursively summing
 * up the size of the devices in the subsets.
 *
 * Pay attention to RAID > 0 types.
 */
static uint64_t add_sectors(struct raid_set *rs, uint64_t sectors,
			    uint64_t add)
{
	add = rs->stride ? round_down(add, rs->stride) : add;

	if (T_RAID1(rs)) {
		if (!sectors || sectors > add)
			sectors = add;
	} else
		sectors += add;

	return sectors;
}

/* FIXME: proper calculation of unsymetric sets ? */
static uint64_t smallest_disk(struct raid_set *rs)
{
	uint64_t ret = ~0;
	struct raid_dev *rd;

	list_for_each_entry(rd, &rs->devs, devs)
		ret = min(ret, rd->sectors);

	return ret;
}

/* Count subsets of a set. */
static unsigned int count_subsets(struct lib_context *lc, struct raid_set *rs)
{
	unsigned ret = 0;
	struct raid_set *r;

	list_for_each_entry(r, &rs->sets, list)
		ret++;

	return ret;
}

/* Calculate total sectors of a (hierarchical) RAID set. */
uint64_t total_sectors(struct lib_context *lc, struct raid_set *rs)
{
        uint64_t sectors = 0;
        struct raid_dev *rd;

	/* Stacked RAID sets. */
	if (!T_GROUP(rs)) {
        	struct raid_set *r;

		list_for_each_entry(r, &rs->sets, list)
			sectors = add_sectors(rs, sectors,
					      total_sectors(lc, r));
	}

	/* RAID device additions taking size maximization into account. */
	if (DEVS(rs)) {
		uint64_t min = F_MAXIMIZE(rs) ? 0 : smallest_disk(rs);

		list_for_each_entry(rd, &rs->devs, devs) {
			if (!T_SPARE(rd))
				sectors = add_sectors(rs, sectors,
						      F_MAXIMIZE(rs) ?
						      rd->sectors : min);
		}
	}

	/* Size correction for higher RAID levels */
	if (T_RAID4(rs) || T_RAID5(rs) || T_RAID6(rs)) {
		unsigned int i = count_subsets(lc, rs);
		uint64_t sub = sectors / (i ? i : count_devs(lc, rs, ct_dev));

		sectors -= sub;
		if (T_RAID6(rs))
			sectors -= sub;
	}

        return sectors;
}

/* Check if a RAID device should be counted. */
static unsigned int _count_dev(struct raid_dev *rd, enum count_type type)
{
	return ((type == ct_dev && !T_SPARE(rd)) ||
		(type == ct_spare && T_SPARE(rd)) ||
		type == ct_all) ? 1 : 0;
}

/* Count devices in a set recursively. */
unsigned int count_devs(struct lib_context *lc, struct raid_set *rs,
			enum count_type count_type)
{
	unsigned int ret = 0;
	struct raid_set *r;
	struct raid_dev *rd;

	list_for_each_entry(r, &rs->sets, list) {
		if (!T_GROUP(rs))
			ret += count_devs(lc, r, count_type);
	}

	list_for_each_entry(rd, &rs->devs, devs)
		ret += _count_dev(rd, count_type);

	return ret;
}

/*
 * Create list of unique memory pointers of a RAID device and free them.
 *
 * This prevents me from having a destructor method in the metadata
 * format handlers so far. If life becomes more complex, I might need
 * one though...
 */
static void _free_dev_pointers(struct lib_context *lc, struct raid_dev *rd)
{
	int area, i, idx = 0;
	void **p;

	/* Count private and area pointers. */
	if (!(area = (rd->private.ptr ? 1 : 0) + rd->areas))
		return;

	/* Allocate and initialize temporary pointer list. */
	if (!(p = dbg_malloc(area * sizeof(*p))))
		LOG_ERR(lc, , "allocating pointer array");

	/* Add private pointer to list. */
	if (rd->private.ptr)
		p[idx++] = rd->private.ptr;

	/* Add metadata area pointers to list. */
	for (area = 0; area < rd->areas; area++) {
		/* Handle multiple pointers to the same memory. */
		for (i = 0; i < idx; i++) {
			if (p[i] == rd->meta_areas[area].area)
				break;
		}
	
		if (i == idx)
			p[idx++] = rd->meta_areas[area].area;
	}

	if (rd->meta_areas)
		dbg_free(rd->meta_areas);

	/* Free all RAID device pointers. */
	while (idx--)
		dbg_free(p[idx]);
	
	dbg_free(p);
}

/* Allocate dev_info struct and keep the device path */
struct dev_info *alloc_dev_info(struct lib_context *lc, char *path)
{
	struct dev_info *di;

	if ((di = dbg_malloc(sizeof(*di)))) {
		if ((di->path = dbg_strdup(path)))
			INIT_LIST_HEAD(&di->list);
		else {
			dbg_free(di);
			di = NULL;
			log_alloc_err(lc, __func__);
		}
	}

	return di;
}

/* Free dev_info structure */
static void _free_dev_info(struct lib_context *lc, struct dev_info *di)
{
	if (di->serial)
		dbg_free(di->serial);

	dbg_free(di->path);
	dbg_free(di);
}

static inline void _free_dev_infos(struct lib_context *lc)
{
	struct list_head *elem, *tmp;

	list_for_each_safe(elem, tmp, LC_DI(lc)) {
		list_del(elem);
		_free_dev_info(lc, list_entry(elem, struct dev_info, list));
	}
}

/*
 * Free dev_info structure or all registered
 * dev_info structures in case di = NULL.
 */
void free_dev_info(struct lib_context *lc, struct dev_info *di)
{
	di ? _free_dev_info(lc, di) : _free_dev_infos(lc);
}

/* Allocate/Free RAID device (member of a RAID set). */
struct raid_dev *alloc_raid_dev(struct lib_context *lc, const char *who)
{
	struct raid_dev *ret;

	if ((ret = dbg_malloc(sizeof(*ret)))) {
		INIT_LIST_HEAD(&ret->list);
		INIT_LIST_HEAD(&ret->devs);
		ret->status = s_setup;
	} else
		log_alloc_err(lc, who);

	return ret;
}

static void _free_raid_dev(struct lib_context *lc, struct raid_dev **rd)
{
	struct raid_dev *r = *rd;

	/* Remove if on global list. */
	if (!list_empty(&r->list))
		list_del(&r->list);

	/*
	 * Create list of memory pointers allocated by
	 * the metadata format handler and free them.
	 */
	_free_dev_pointers(lc, r);

	dbg_free(r->name);
	dbg_free(r);
	*rd = NULL;
}

static inline void _free_raid_devs(struct lib_context *lc)
{
	struct list_head *elem, *tmp;
	struct raid_dev *rd;

	list_for_each_safe(elem, tmp, LC_RD(lc)) {
		rd = list_entry(elem, struct raid_dev, list);
		_free_raid_dev(lc, &rd);
	}
}

/* Free RAID device structure or all registered RAID devices if rd == NULL. */
void free_raid_dev(struct lib_context *lc, struct raid_dev **rd)
{
	rd ? _free_raid_dev(lc, rd) : _free_raid_devs(lc);
}

/* Allocate/Free RAID set. */
struct raid_set *alloc_raid_set(struct lib_context *lc, const char *who)
{
	struct raid_set *ret;

	if ((ret = dbg_malloc(sizeof(*ret)))) {
		INIT_LIST_HEAD(&ret->list);
		INIT_LIST_HEAD(&ret->sets);
		INIT_LIST_HEAD(&ret->devs);
		ret->status = s_setup;
		ret->type   = t_undef;
	} else
		log_alloc_err(lc, who);

	return ret;
}

/* Free a single RAID set structure and its RAID devices. */
static void _free_raid_set(struct lib_context *lc, struct raid_set *rs)
{
	struct raid_dev *rd;
	struct list_head *elem, *tmp;

	log_dbg(lc, "freeing devices of RAID set \"%s\"", rs->name);
	list_for_each_safe(elem, tmp, &rs->devs) {
		list_del(elem);
		rd = RD(elem);

		log_dbg(lc, "freeing device \"%s\", path \"%s\"",
			rd->name, rd->di->path);

		/* FIXME: remove partition code in favour of kpartx ? */
		/*
		 * Special case for partitioned sets.
		 *
		 * We don't hook dev_info structures for partitioned
		 * sets up the global list, so delete them here.
		 */
		if (partitioned_set(lc, rs))
			free_dev_info(lc, rd->di);

		/*
		 * We don't hook raid_dev structures for GROUP
		 * sets up the global list, so delete them here.
		 */
		if (list_empty(&rd->list))
			free_raid_dev(lc, &rd);
	}

	list_del(&rs->list);
	dbg_free(rs->name);
	dbg_free(rs);
}

/* Remove a set or all sets (in case rs = NULL) recursively. */
void free_raid_set(struct lib_context *lc, struct raid_set *rs)
{
	struct list_head *elem, *tmp;

	list_for_each_safe(elem, tmp, rs ? &rs->sets : LC_RS(lc))
		free_raid_set(lc, RS(elem));

	if (rs)
		_free_raid_set(lc, rs);
	else if (!list_empty(LC_RS(lc)))
		log_fatal(lc, "lib context RAID set list not empty");
}

/* Return != 0 in case of a partitioned RAID set type. */
int partitioned_set(struct lib_context *lc, void *rs)
{
	return T_PARTITION((struct raid_set*) rs);
}

/* Return != 0 in case of a partitioned base RAID set. */
int base_partitioned_set(struct lib_context *lc, void *rs)
{
	return ((struct raid_set*) rs)->flags & f_partitions;
}

/* Return RAID set name. */
const char *get_set_name(struct lib_context *lc, void *rs)
{
	return ((struct raid_set*) rs)->name;
}

/*
 * Find RAID set by name.
 *
 * Search top level RAID set list only if where = FIND_TOP.
 * Recursive if where = FIND_ALL.
 */
static struct raid_set *_find_set(struct lib_context *lc,
				  struct list_head *list,
				  const char *name, enum find where)
{
	struct raid_set *r, *ret = NULL;

	log_dbg(lc, "%s: searching %s", __func__, name);
	list_for_each_entry(r, list, list) {
		if (!strcmp(r->name, name)) {
			ret = r;
			goto out;
		}
	}

	if (where == FIND_ALL) {
		list_for_each_entry(r, list, list) {
			if ((ret = _find_set(lc, &r->sets, name, where)))
				break;
		}
	}

  out:
	log_dbg(lc, "_find_set: %sfound %s", ret ? "" : "not ", name);

	return ret;
}

struct raid_set *find_set(struct lib_context *lc,
			  struct list_head *list,
			  const char *name, enum find where)
{
	return _find_set(lc, list ? list : LC_RS(lc), name, where);
}

struct raid_set *find_or_alloc_raid_set(struct lib_context *lc,
				char *name, enum find where,
				struct raid_dev *rd,
				struct list_head *list,
				void (*f_create) (struct raid_set *super,
						  void *private),
				void *private)
{
	struct raid_set *rs;

	if ((rs = find_set(lc, NULL, name, where)))
		goto out;

	if (!(rs = alloc_raid_set(lc, __func__)))
		goto out;

	if (!(rs->name = dbg_strdup(name)))
		goto err;

	if (rd && ((rs->type = rd->type), T_SPARE(rd)))
		rs->type = t_undef;

	/* If caller hands a list in, add to it. */
	if (list)
		list_add_tail(&rs->list, list);

	/* Call any create callback. */
	if (f_create)
		f_create(rs, private);

   out:
	return rs;

   err:
	dbg_free(rs);
	log_alloc_err(lc, __func__);

	return NULL;
}

/* Return # of raid sets build */
unsigned int count_sets(struct lib_context *lc, struct list_head *list)
{
	int ret = 0;
	struct list_head *elem;

	list_for_each(elem, list)
		ret++;

	return ret;
}

/*
 * Count devices found
 */
static unsigned int _count_devices(struct lib_context *lc, enum dev_type type)
{
	unsigned int ret = 0;
	struct list_head *elem, *list;

	if (DEVICE & type)
		list = LC_DI(lc);
	else if (((RAID|NATIVE) & type))
		list = LC_RD(lc);
	else
		return 0;
	
	list_for_each(elem, list)
			ret++;

	return ret;
}

unsigned int count_devices(struct lib_context *lc, enum dev_type type)
{
	return type == SET ? count_sets(lc, LC_RS(lc)) :
			     _count_devices(lc, type);
}

/*
 * Read RAID metadata off a device by trying
 * all/selected registered format handlers in turn.
 */
static int _want_format(struct dmraid_format *fmt, const char *format,
			enum fmt_type type)
{
	return fmt->format != type ||
	       (format && strncmp(format, fmt->name, strlen(format))) ? 0 : 1;
}

static struct raid_dev *_dmraid_read(struct lib_context *lc,
				       struct dev_info *di,
				       struct dmraid_format *fmt)
{
	struct raid_dev *rd;

	log_notice(lc, "%s: %-7s discovering", di->path, fmt->name);
	if ((rd = fmt->read(lc, di))) {
		log_notice(lc, "%s: %s metadata discovered",
			   di->path, fmt->name);
		rd->fmt = fmt;
	}

	return rd;
}

static struct raid_dev *dmraid_read(struct lib_context *lc,
				      struct dev_info *di, char const *format,
				      enum fmt_type type)
{
	struct format_list *fl;
	struct raid_dev *rd = NULL, *rd_tmp;

	/* FIXME: dropping multiple formats ? */
	list_for_each_entry(fl, LC_FMT(lc), list) {
		if (_want_format(fl->fmt, format, type) &&
		    (rd_tmp = _dmraid_read(lc, di, fl->fmt))) {
			if (rd) {
				log_print(lc, "%s: \"%s\" and \"%s\" formats "
					  "discovered (using %s)!",
					  di->path, rd_tmp->fmt->name,
					  rd->fmt->name, rd->fmt->name);
				free_raid_dev(lc, &rd_tmp);
			} else
				rd = rd_tmp;
		}
	}

	return rd;
}

/*
 * Write RAID metadata to a device.
 */
int write_dev(struct lib_context *lc, struct raid_dev *rd, int erase)
{
	int ret = 0;
	struct dmraid_format *fmt = rd->fmt;

	if (fmt->write) {
		log_notice(lc, "%sing metadata %s %s",
			   erase ? "Eras" : "Writ",
			   erase ? "on" : "to",
			   rd->di->path);
		ret = fmt->write(lc, rd, erase);
	} else
		log_err(lc, "format \"%s\" doesn't support writing metadata",
			fmt->name);

	return ret;
}

/*
 * Group RAID device into a RAID set.
 */
static inline struct raid_set *dmraid_group(struct lib_context *lc,
					    struct raid_dev *rd)
{
	return rd->fmt->group(lc, rd);
}

/* Check that device names are members of the devices list. */
static int _want_device(struct dev_info *di, char **devices)
{
	char **dev;

	if (!devices || !*devices)
		return 1;

	for (dev = devices; *dev; dev++) {
		if (!strcmp(*dev, di->path))
			return 1;
	}

	return 0;
}

/* Discover RAID devices. */
void discover_raid_devices(struct lib_context *lc, char **devices)
{
	struct dev_info *di;
	char *names = NULL;
	const char delim = *OPT_STR_SEPARATOR(lc);

	/* In case we've got format identifiers -> duplicate string for loop. */
	if (OPT_FORMAT(lc) &&
	    (!(names = dbg_strdup((char*) OPT_STR_FORMAT(lc))))) {
		log_alloc_err(lc, __func__);
		return;
	}

	/* Walk the list of discovered block devices. */
	list_for_each_entry(di, LC_DI(lc), list) {
		if (_want_device(di, devices)) {
			char *p, *sep = names;
			struct raid_dev *rd;

			do {
				p = sep;
				sep = remove_delimiter(sep, delim);

				if ((rd = dmraid_read(lc, di, p, FMT_RAID)))
					list_add_tail(&rd->list, LC_RD(lc));

				add_delimiter(&sep, delim);
			} while (sep);
		}
	}

	if (names)
		dbg_free(names);
}

/*
 * Discover partitions on RAID sets.
 *
 * FIXME: remove partition code in favour of kpartx ?
 */
static void _discover_partitions(struct lib_context *lc,
				 struct list_head *rs_list)
{
	char *path;
	struct dev_info *di;
	struct raid_dev *rd;
	struct raid_set *rs, *r;

	list_for_each_entry(rs, rs_list, list) {
		/*
		 * t_group type RAID sets are never active!
		 * (They are containers for subsets to activate)
		 *
		 * Recurse into them.
		 */
		if (T_GROUP(rs)) {
			_discover_partitions(lc, &rs->sets);
			return;
		}

		/*
		 * Skip all "container" sets, which are not active.
		 */
		if (base_partitioned_set(lc, rs) ||
		    partitioned_set(lc, rs) ||
		    !dm_status(lc, rs))
			continue;

		log_notice(lc, "discovering partitions on \"%s\"", rs->name);
		if (!(path = mkdm_path(lc, rs->name)))
			return;

		/* Allocate a temporary disk info struct for dmraid_read(). */
		di = alloc_dev_info(lc, path);
		dbg_free(path);
		if (!di)
			return;

		di->sectors = total_sectors(lc, rs);
		if (!(rd = dmraid_read(lc, di, NULL, FMT_PARTITION))) {
			free_dev_info(lc, di);
			continue;
		}

		/*
		 * WARNING: partition group function returns
		 * a dummy pointer because of the creation of multiple
		 * RAID sets (one per partition) it does.
		 *
		 * We don't want to access that 'pointer'!
		 */
		if ((r = dmraid_group(lc, rd))) {
			log_notice(lc, "created partitioned RAID set(s) for %s",
				   di->path);
			rs->flags |= f_partitions;
		} else
			log_err(lc, "adding %s to RAID set", di->path);

		/*
		 * Free the RD. We don't need it any more, because we
		 * don't support writing partition tables.
		 */
		free_dev_info(lc, di);
		free_raid_dev(lc, &rd);
	}
}

void discover_partitions(struct lib_context *lc)
{
	_discover_partitions(lc, LC_RS(lc));
}

/*
 * Group RAID set(s)
 *
 *	name = NULL  : build all sets
 *	name = String: build just the one set
 */
static void want_set(struct lib_context *lc, struct raid_set *rs, char *name)
{
	if (name) {
		size_t len1 = strlen(rs->name), len2 = strlen(name);

		if (len2 > len1 ||
		    strncmp(rs->name, name, min(len1, len2))) {
			struct dmraid_format *fmt = get_format(rs);

			log_notice(lc, "dropping unwanted RAID set \"%s\"",
				   rs->name);

			/*
			 * ddf1 carries a private pointer to it's contianing
			 * set which is cleared as part of the check. So we
			 * must call it's check method before freeing the
			 * set. Whats more, it looks like ddf1 check can
			 * only be called once, yoweee !!!!
			 */
			if (fmt)
				fmt->check(lc, rs);

			free_raid_set(lc, rs);
		}
	}
}

/* Get format handler of RAID set. */
struct dmraid_format *get_format(struct raid_set *rs)
{
	/* Decend RAID set hierarchy. */
	while (SETS(rs))
		rs = RS_RS(rs);

	return DEVS(rs) ? (RD_RS(rs))->fmt : NULL;
}

/* Find the set associated with a device */
struct raid_set *get_raid_set(struct lib_context *lc, struct raid_dev *dev)
{
	struct raid_set *rs;
	struct raid_dev *rd;

	list_for_each_entry(rs, LC_RS(lc), list)
		list_for_each_entry(rd, &rs->devs, devs)
			if (dev == rd)
				return rs;

	return NULL;
}

/* Check metadata consistency of raid sets. */
static void check_raid_sets(struct lib_context *lc)
{
	struct list_head *elem, *tmp;
	struct raid_set *rs;
	struct dmraid_format *fmt;

	list_for_each_safe(elem, tmp, LC_RS(lc)) {
		/* Some metadata format handlers may not have a check method. */
		if (!(fmt = get_format((rs = RS(elem)))))
			continue;

		if (!fmt->check(lc, rs)) {
			/*
			 * FIXME: check needed if degraded activation
			 *        is sensible.
			 */
			if (T_RAID1(rs))
				log_err(lc, "keeping degraded mirror "
					"set \"%s\"", rs->name);
			else {
				log_err(lc, "removing inconsistent RAID "
					"set \"%s\"", rs->name);
				free_raid_set(lc, rs);
			}
		}
	}

	return;
}

int group_set(struct lib_context *lc, char *name)
{
	struct raid_dev *rd;
	struct raid_set *rs;
	struct list_head *elem, *tmp;

	if (name && find_set(lc, NULL, name, FIND_TOP))
		LOG_ERR(lc, 0, "RAID set %s already exists", name);

	list_for_each_safe(elem, tmp, LC_RD(lc)) {
		rd = list_entry(elem, struct raid_dev, list);
		/* FIXME: optimize dropping of unwanted RAID sets. */
		if ((rs = dmraid_group(lc, rd))) {
			log_notice(lc, "added %s to RAID set \"%s\"",
				   rd->di->path, rs->name);
			want_set(lc, rs, name);
			continue;
		}

		if (!T_SPARE(rd))
			log_err(lc, "adding %s to RAID set \"%s\"",
				rd->di->path, rd->name);

		/* Need to find the set and remove it. */
		if ((rs = find_set(lc, NULL, rd->name, FIND_ALL))) {
			log_err(lc, "removing RAID set \"%s\"", rs->name);
			free_raid_set(lc, rs);
		}
	}

	/* Check sanity of grouped RAID sets. */
	check_raid_sets(lc);

	return 1;
}

/* Process function on RAID set(s) */
static void process_set(struct lib_context *lc, void *rs,
			int (*func)(struct lib_context *lc, void *rs, int arg),
			int arg)
{
	if (!partitioned_set(lc, rs))
		func(lc, rs, arg);
}

/* FIXME: remove partition code in favour of kpartx ? */
static void
process_partitioned_set(struct lib_context *lc, void *rs,
			int (*func)(struct lib_context *lc, void *rs, int arg),
			int arg)
{
	if (partitioned_set(lc, rs) && !base_partitioned_set(lc, rs))
		func(lc, rs, arg);
}

void process_sets(struct lib_context *lc,
		  int (*func)(struct lib_context *lc, void *rs, int arg),
		  int arg, enum set_type type)
{
	struct raid_set *rs;
	void (*p)(struct lib_context *l, void *r,
		  int (*f)(struct lib_context *lc, void *rs, int arg), int a) =
		(type == PARTITIONS) ? process_partitioned_set : process_set;

	list_for_each_entry(rs, LC_RS(lc), list)
		p(lc, rs, func, arg);
}

/* Write RAID set metadata to devices. */
int write_set(struct lib_context *lc, void *v)
{
	int ret = 1;
	struct raid_set *r, *rs = v;
	struct raid_dev *rd;

	/* Decend hierarchy */
	list_for_each_entry(r, &rs->sets, list) {
		/*
		 * FIXME: does it make sense to try the rest of the subset
		 *	  in case we fail writing one ?
		 */
		if (!write_set(lc, (void*) r))
			log_err(lc, "writing RAID subset \"%s\", continuing",
				r->name);
	}

	/* Write metadata to the RAID devices of a set. */
	list_for_each_entry(rd, &rs->devs, devs) {
		/*
		 * FIXME: does it make sense to try the rest of the
		 *	  devices in case we fail writing one ?
		 */
		if (!write_dev(lc, rd, 0)) {
			log_err(lc, "writing RAID device \"%s\", continuing",
				rd->di->path);
			ret = 0;
		}
	}

	return ret;
}

/* Erase ondisk metadata. */
int erase_metadata(struct lib_context *lc)
{
	int ret = 1;
	struct raid_dev *rd;

	list_for_each_entry(rd, LC_RD(lc), list) {
		if (yes_no_prompt(lc, "Do you really want to erase \"%s\" "
				  "ondisk metadata on %s",
				  rd->fmt->name, rd->di->path) &&
		    !write_dev(lc, rd, 1)) {
			log_err(lc, "erasing ondisk metadata on %s",
				rd->di->path);
			ret = 0;
		}
	}

	return ret;
}

/*
 * Support function for metadata format handlers:
 *
 * Return neutralized RAID type for given mapping array (linear, raid0, ...)
 */
enum type rd_type(struct types *t, unsigned int type)
{
	for (; t->unified_type != t_undef && t->type != type; t++);
	return t->unified_type;
}

/*
 * Support function for metadata format handlers:
 *
 * Return neutralized RAID status for given metadata status
 */
enum status rd_status(struct states *s, unsigned int status, enum compare cmp)
{
	for (; s->status && (cmp == AND ? !(s->status & status) : (s->status != status)); s++);
	return s->unified_status;
}

/*
 * Support function for metadata format handlers.
 *
 * Sort an element into a list by optionally
 * using a metadata format handler helper function.
 */
void list_add_sorted(struct lib_context *lc,
		     struct list_head *to, struct list_head *new,
		     int (*f_sort)(struct list_head *pos,
				   struct list_head *new))
{
	struct list_head *pos;

	list_for_each(pos, to) {
		/*
		 * Add in at the beginning of the list
		 * (ie., after HEAD or the first entry we found),
		 * or where the metadata format handler sort
		 * function tells us to.
		 */
		if (!f_sort || f_sort(pos, new))
			break;
	}

	/*
	 * If we get here we either had an empty list or the sort
	 * function hit or not -> add where pos tells us to.
	 */
	list_add_tail(new, pos);
}

/*
 * Support function for format handlers:
 *
 * File RAID metadata and offset on device for analysis.
 */
/* FIXME: all files into one directory ? */
static size_t __name(struct lib_context *lc, char *str, size_t len,
		     const char *path, const char *suffix)
{
	return snprintf(str, len, "%s.%s",
			get_basename(lc, (char*) path), suffix) + 1;
}

static char *_name(struct lib_context *lc, const char *path, const char *suffix)
{
	size_t len;
	char *ret;

	if ((ret = dbg_malloc((len = __name(lc, NULL, 0, path, suffix)))))
		__name(lc, ret, len, path, suffix);
	else
		log_alloc_err(lc, __func__);

	return ret;
}

static int file_data(struct lib_context *lc, const char *handler,
		     char *path, void *data, size_t size)
{
	int ret = 0;
	char *name;

	if ((name = _name(lc, path, "dat"))) {
		log_notice(lc, "writing metadata file \"%s\"", name);
		ret = write_file(lc, handler, name, data, size, 0);
		dbg_free(name);
	}

	return ret;
}

static void file_number(struct lib_context *lc, const char *handler,
			char *path, uint64_t number, const char *suffix)
{
	char *name, s_number[32];
	
	if ((name = _name(lc, path, suffix))) {
		log_notice(lc, "writing %s to file \"%s\"", suffix, name);
		write_file(lc, handler, name, (void*) s_number,
		           snprintf(s_number, sizeof(s_number),
			   "%" PRIu64 "\n", number), 0);
		dbg_free(name);
	}
}

static int _chdir(struct lib_context *lc, const char *dir)
{
	if (chdir(dir)) {
		log_err(lc, "changing directory to %s", dir);
		return -EFAULT;
	}

	return 0;
}

static char *_dir(struct lib_context *lc, const char *handler)
{
	char *dir = _name(lc, lc->cmd, handler);

	if (!dir) {
		log_err(lc, "allocating directory name for %s", handler);
		return NULL;
	}

	if (!mk_dir(lc, dir))
		goto out;

	if (!_chdir(lc, dir))
		return dir;

   out:
	dbg_free(dir);
	return NULL;
}

/*
 * File vendor RAID metadata.
 */
void file_metadata(struct lib_context *lc, const char *handler,
		   char *path, void *data, size_t size, uint64_t offset)
{
	if (OPT_DUMP(lc)) {
		char *dir = _dir(lc, handler);

		if (dir)
			dbg_free(dir);
		else
			return;

		if (file_data(lc, handler, path, data, size))
			file_number(lc, handler, path, offset, "offset");

		_chdir(lc, "..");
	}
}

/*
 * File RAID device size.
 */
void file_dev_size(struct lib_context *lc, const char *handler,
		   struct dev_info *di)
{
	if (OPT_DUMP(lc)) {
		char *dir = _dir(lc, handler);

		if (dir)
			dbg_free(dir);
		else
			return;

		file_number(lc, handler, di->path, di->sectors, "size");
		_chdir(lc, "..");
	}
}

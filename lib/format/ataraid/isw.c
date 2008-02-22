/*
 * Intel Software RAID metadata format handler.
 *
 * Copyright (C) 2004,2005  Heinz Mauelshagen, Red Hat GmbH.
 *                          All rights reserved.
 *
 * See file LICENSE at the top of this source tree for license information.
 */

/*
 * isw_read() etc. profited from Carl-Daniel Hailfinger's raiddetect code.
 *
 * Profited from the Linux 2.4 iswraid driver by
 * Boji Tony Kannanthanam and Martins Krikis.
 */
#define	HANDLER	"isw"

#include "internal.h"
#define	FORMAT_HANDLER
#include "isw.h"

#if	BYTE_ORDER != LITTLE_ENDIAN
#  define	DM_BYTEORDER_SWAB
#  include	<datastruct/byteorder.h>
#endif

static const char *handler = HANDLER;

/*
 * Make up RAID set name from family_num and volume name.
 */
static size_t _name(struct isw *isw, struct isw_dev *dev,
		     char *str, size_t len)
{
	return snprintf(str, len, dev ? "isw_%u_%s" : "isw_%u",
			isw->family_num, (char*) dev->volume);
}

static char *name(struct lib_context *lc, struct isw *isw, struct isw_dev *dev)
{
        size_t len;
        char *ret;

        if ((ret = dbg_malloc((len = _name(isw, dev, NULL, 0) + 1)))) {
                _name(isw, dev, ret, len);
		mk_alpha(lc, ret + HANDLER_LEN, len - HANDLER_LEN -
			 (dev ? strlen((char*) dev->volume) - 2 : 1));
        } else
		log_alloc_err(lc, handler);

        return ret;
}

/* Find a disk table slot by serial number. */
static struct isw_disk *_get_disk(struct isw *isw, struct dev_info *di)
{
	if (di->serial) {
		struct isw_disk *disk = isw->disk;

		do {
			if (!strncmp(di->serial, (const char*) disk->serial,
				     MAX_RAID_SERIAL_LEN))
				return disk;
		} while (++disk < isw->disk + isw->num_disks);
	}

	return NULL;
}

static struct isw_disk *get_disk(struct lib_context *lc,
				 struct dev_info *di, struct isw *isw)
{
	struct isw_disk *disk;

	if ((disk = _get_disk(isw, di)))
		return disk;

	LOG_ERR(lc, NULL, "%s: Error finding disk table slot for %s",
		handler, di->path);
}

/*
 * Retrieve status of device.
 *
 * FIXME: is this sufficient to cover all state ?
 */
static enum status __status(unsigned int status)
{
	return ((status & (CONFIGURED_DISK|USABLE_DISK)) &&
		!(FAILED_DISK & status)) ?
	       s_ok : s_broken;
}

static enum status status(struct lib_context *lc, struct raid_dev *rd)
{
	struct isw_disk *disk;

	if ((disk = get_disk(lc, rd->di, META(rd, isw))))
		return __status(disk->status);

	return s_undef;
}

/* Neutralize disk type. */
static enum type type(struct raid_dev *rd)
{
	/* Mapping of Intel types to generic types. */
	static struct types types[] = {
	        { ISW_T_RAID0, t_raid0},
	        { ISW_T_RAID1, t_raid1},
	        { ISW_T_RAID5, t_raid5_la},
	        { 0, t_undef},
	};
	struct isw_dev *dev = rd->private.ptr;

	return dev ? rd_type(types, (unsigned int) dev->vol.map.raid_level) :
		     t_group;
}

/*
 * Generate checksum of Raid metadata for mpb_size/sizeof(u32) words
 * (checksum field itself ignored for this calculation).
 */
static uint32_t _checksum(struct isw *isw)
{
	uint32_t end = isw->mpb_size / sizeof(end),
		 *p = (uint32_t*) isw, ret = 0;

	while (end--)
		ret += *p++;

	return ret - isw->check_sum;
}

/* Calculate next isw device offset. */
static struct isw_dev *advance_dev(struct isw_dev *dev,
				   struct isw_map *map, size_t add)
{
	return (struct isw_dev*) ((uint8_t*) dev +
				  (map->num_members - 1) * 
				  sizeof(map->disk_ord_tbl) + add);
}

/* Advance to the next isw_dev from a given one. */
static struct isw_dev *advance_raiddev(struct isw_dev *dev)
{
	struct isw_vol *vol = &dev->vol;
	struct isw_map *map = &vol->map;

	/* Correction: yes, it sits here! */
	dev = advance_dev(dev, map, sizeof(*dev));

	if (vol->migr_state) /* need to add space for another map */
		dev = advance_dev(dev, map, sizeof(*map));

	return dev;
}

/* Return isw_dev by table index. */
static struct isw_dev *raiddev(struct isw *isw, unsigned int i)
{
	struct isw_dev *dev = (struct isw_dev*) (isw->disk + isw->num_disks);

	while (i--)
		dev = advance_raiddev(dev);

	return dev;
}

/*
 * Read an Intel RAID device
 */
/* Endianess conversion. */
enum convert { FULL, FIRST, LAST };
#if	BYTE_ORDER == LITTLE_ENDIAN
#  define	to_cpu(x, y)
#else
/*
 * We can differ from the read_raid_dev template here,
 * because we don't get called from there.
 */
static void to_cpu(struct isw *isw, enum convert cvt)
{
	unsigned int i, j;
	struct isw_disk *dsk;
	struct isw_dev *dev;

	if (cvt == FIRST || cvt == FULL) {
		CVT32(isw->check_sum);
		CVT32(isw->mpb_size);
		CVT32(isw->family_num);
		CVT32(isw->generation_num);
	}

	if (cvt == FIRST)
		return;

	for (dsk = isw->disk; dsk < &isw->disk[isw->num_disks]; dsk++) {
		CVT32(dsk->totalBlocks);
		CVT32(dsk->scsiId);
		CVT32(dsk->status);
	}

	for (i = 0; i < isw->num_raid_devs; i++) {
		dev = raiddev(isw, i);

		/* RAID device. */
		CVT32(dev->SizeLow);
		CVT32(dev->SizeHigh);
		CVT32(dev->status);
		CVT32(dev->reserved_blocks);

		/* RAID volume has 8 bit members only. */
	
		/* RAID map. */
		CVT32(dev->vol.map.pba_of_lba0);
		CVT32(dev->vol.map.blocks_per_member);
		CVT32(dev->vol.map.num_data_stripes);
		CVT16(dev->vol.map.blocks_per_strip);

		for (j = 0; j < dev->vol.map.num_members; j++)
			CVT16(dev->vol.map.disk_ord_tbl[j]);
	}
}
#endif

static int is_isw(struct lib_context *lc, struct dev_info *di, struct isw *isw)
{
	if (strncmp((const char *) isw->sig, MPB_SIGNATURE,
		    sizeof(MPB_SIGNATURE) - 1))
		return 0;

	/* Check version info, older versions supported */
	if (strncmp((const char*) isw->sig + sizeof(MPB_SIGNATURE) - 1,
		    MPB_VERSION_RAID2, sizeof(MPB_VERSION_RAID2) - 1) > 0)
		log_print(lc, "%s: untested metadata version %s found on %s",
			  handler, isw->sig + sizeof(MPB_SIGNATURE) - 1,
			  di->path);

	return 1;
}

static void isw_file_metadata(struct lib_context *lc, struct dev_info *di,
			      void *meta)
{
	struct isw *isw = meta;

	/* Get the rounded up value for the metadata size */
	size_t size = round_up(isw->mpb_size, ISW_DISK_BLOCK_SIZE);

	file_metadata(lc, handler, di->path,
		      meta + (size / ISW_DISK_BLOCK_SIZE > 1 ?
			      ISW_DISK_BLOCK_SIZE : 0),
		      size, (di->sectors - (size / ISW_DISK_BLOCK_SIZE)) << 9);
	file_dev_size(lc, handler, di);
}

static int isw_read_extended(struct lib_context *lc, struct dev_info *di,
			     struct isw **isw,
			     uint64_t *isw_sboffset, size_t *size)
{
	struct isw *isw_tmp;

	/* Get the rounded up value for the metadata blocks */
	size_t blocks = div_up((*isw)->mpb_size, ISW_DISK_BLOCK_SIZE);

	/* No extended metadata to read ? */
	if (blocks < 2)
		return 1;

	/*
	 * Allocate memory for the extended Intel superblock
	 * and read it in. Reserve one more disk block in order
	 * to be able to file the metadata in the proper sequence.
	 * (ie, sectors 1, 2-n, 1 in core so that the filing can start at 2).
	 */
	*size = blocks * ISW_DISK_BLOCK_SIZE;
	*isw_sboffset -= *size - ISW_DISK_BLOCK_SIZE;

	if ((isw_tmp = alloc_private(lc, handler,
				     *size + ISW_DISK_BLOCK_SIZE))) {
		/* Read extended metadata to offset ISW_DISK_BLOCK_SIZE */
		if (read_file(lc, handler, di->path,
			      (void*) isw_tmp + ISW_DISK_BLOCK_SIZE,
			      *size - ISW_DISK_BLOCK_SIZE, *isw_sboffset))
			/* Copy in first metadata sector. */
			memcpy(isw_tmp, *isw, ISW_DISK_BLOCK_SIZE);
		else {
			dbg_free(isw_tmp);
			isw_tmp = NULL;
		}
	}

	dbg_free(*isw);
	*isw = isw_tmp;

	return isw_tmp ? 1 : 0;
}

/* Check for RAID disk ok. */
static int disk_ok(struct lib_context *lc, struct dev_info *di, struct isw *isw)
{
	struct isw_disk *disk = get_disk(lc, di, isw);

	return disk && __status(disk->status) == s_ok;
}

static void *isw_read_metadata(struct lib_context *lc, struct dev_info *di,
			       size_t *sz, uint64_t *offset,
			       union read_info *info)
{
	size_t size = ISW_DISK_BLOCK_SIZE;
	uint64_t isw_sboffset = ISW_CONFIGOFFSET;
	struct isw *isw;

	if (!(isw = alloc_private_and_read(lc, handler, size,
					   di->path, isw_sboffset)))
		goto out;

	/*
	 * Convert start of metadata only, because we might need to
	 * read extended metadata located ahead of it first.
	 */
	to_cpu(isw, FIRST);

	/* Check Signature and read optional extended metadata. */
	if (!is_isw(lc, di, isw) ||
	    !isw_read_extended(lc, di, &isw, &isw_sboffset, &size))
		goto bad;

	/*
	 * Now that we made sure, that we've got all the
	 * metadata, we can convert it completely.
	 */
	to_cpu(isw, LAST);

	if (disk_ok(lc, di, isw)) {
		*sz = size;
		*offset = isw_sboffset;
		info->u64 = isw_sboffset;
		goto out;
	}
	
   bad:
	dbg_free(isw);
	isw = NULL;

   out:
	return (void*) isw;
}

static int setup_rd(struct lib_context *lc, struct raid_dev *rd,
		    struct dev_info *di, void *meta, union read_info *info);
static struct raid_dev *isw_read(struct lib_context *lc, struct dev_info *di)
{
	return read_raid_dev(lc, di, isw_read_metadata, 0, 0, NULL, NULL,
			     isw_file_metadata, setup_rd, handler);
}

/*
 * Write an Intel Software RAID device.
 */
static int isw_write(struct lib_context *lc, struct raid_dev *rd, int erase)
{
	int ret;
	struct isw *isw = META(rd, isw);

	to_disk(isw, FULL);

	/*
	 * Copy 1st metadata sector to after the extended ones
	 * and increment metadata area pointer by one block, so
	 * that the metadata is filed in the proper sequence.
	 */
	memcpy((void*) isw + rd->meta_areas->size, isw, ISW_DISK_BLOCK_SIZE);
	rd->meta_areas->area += ISW_DISK_BLOCK_SIZE;

	ret = write_metadata(lc, handler, rd, -1, erase);

	/* Correct metadata area pointer. */
	rd->meta_areas->area -= ISW_DISK_BLOCK_SIZE;

	to_cpu(isw, FULL);

	return ret;
}

/*
 * Group an Intel SW RAID disk into potentially
 * multiple RAID sets and RAID disks.
 */
/* Check state if isw device map. */
static int _check_map_state(struct lib_context *lc, struct raid_dev *rd,
			    struct isw_dev *dev)
{
	/* FIXME: FAILED_MAP etc. */
	switch (dev->vol.map.map_state) {
	case ISW_T_STATE_NORMAL:
	case ISW_T_STATE_UNINITIALIZED:
		break;

	default:
		LOG_ERR(lc, 0, "%s: unsupported map state 0x%x on %s for %s",
			handler, dev->vol.map.map_state, rd->di->path,
			(char*) dev->volume);
	}

	return 1;
}

/* Create a RAID device to map a volumes segment. */
static struct raid_dev *_create_rd(struct lib_context *lc, struct raid_dev *rd,
				   struct isw *isw, struct isw_dev *dev)
{
	struct raid_dev *r;

	if (!_check_map_state(lc, rd, dev) ||
	    !(r = alloc_raid_dev(lc, handler)))
		return NULL;

	if (!(r->private.ptr = alloc_private(lc, handler, sizeof(*dev))))
		goto free;

	memcpy(r->private.ptr, dev, sizeof(*dev));
	if ((r->type = type(r)) == t_undef) {
		log_err(lc, "%s: RAID type %u not supported",
			handler, (unsigned int) dev->vol.map.raid_level);
		goto free;
	}

        if (!(r->name = name(lc, isw, dev)))
		goto free;

	r->di = rd->di;
	r->fmt = rd->fmt;
	
	r->offset  = dev->vol.map.pba_of_lba0;
	if ((r->sectors = dev->vol.map.blocks_per_member))
		goto out;

	log_zero_sectors(lc, rd->di->path, handler);

   free:
	free_raid_dev(lc, &r);
   out:
	return r;
}

/* Find an Intel RAID set or create it. */
static void create_rs(struct raid_set *rs, void* private)
{
	rs->stride = ((struct isw_dev*) private)->vol.map.blocks_per_strip;
}

/* Decide about ordering sequence of RAID device. */
static int dev_sort(struct list_head *pos, struct list_head *new)
{
	struct isw *isw = RD(new)->private.ptr;
	
	return _get_disk(isw, RD(new)->di) < _get_disk(isw, RD(pos)->di);
}

/*
 * rs_group contains the top-level group RAID set (type: t_group) on entry
 * and shall be returned on success (or NULL on error).
 */
static struct raid_set *group_rd(struct lib_context *lc,
				   struct raid_set *rs_group,
				   struct raid_dev *rd_meta)
{
	unsigned int d;
	void *private;
	struct isw *isw = META(rd_meta, isw);
	struct isw_dev *dev;
	struct raid_dev *rd;
	struct raid_set *rs;

	/* Loop the device/volume table. */
	for (d = 0; d < isw->num_raid_devs; d++) {
		dev = raiddev(isw, d);

		if (!(rd = _create_rd(lc, rd_meta, isw, dev)))
			return NULL;

		if (!(rs = find_or_alloc_raid_set(lc, rd->name, FIND_ALL,
				      		  rd, &rs_group->sets,
						  create_rs, dev))) {
			free_raid_dev(lc, &rd);
			return NULL;
		}

		/* Save and set to enable dev_sort(). */
		private = rd->private.ptr;
		rd->private.ptr = isw;

		list_add_sorted(lc, &rs->devs, &rd->devs, dev_sort);

		/* Restore. */
		rd->private.ptr = private;
	}

	return rs_group;
}

/* Add an Intel SW RAID device to a set */
static struct raid_set *isw_group(struct lib_context *lc,
				    struct raid_dev *rd_meta)
{
	struct raid_set *rs_group;

	if (T_SPARE(rd_meta))
		return NULL;

	/*
	 * Once we get here, an Intel SW RAID disk containing a metadata area
	 * with a volume table has been discovered by isw_read.
	 */
	/* Check if a top level group RAID set already exists. */
	if (!(rs_group = find_or_alloc_raid_set(lc, rd_meta->name, FIND_TOP,
				      		rd_meta, LC_RS(lc),
						NO_CREATE, NO_CREATE_ARG)))
		return NULL;

	/*
	 * Add the whole underlying (meta) RAID device to the group set.
	 * Sorting is no problem here, because RAID sets and devices will
	 * be created for all the Volumes of an ISW set and those need sorting.
	 */
	rd_meta->private.ptr = rd_meta->meta_areas->area;
	list_add_sorted(lc, &rs_group->devs, &rd_meta->devs, dev_sort);
	rd_meta->private.ptr = NULL;

	/*
	 * We need to run through the volume table and create a RAID set and
	 * RAID devices hanging off it for every volume,
	 * so that the activate code is happy.
	 *
	 * A pointer to the top-level group RAID set
	 * gets returned or NULL on error.
	 */
	return group_rd(lc, rs_group, rd_meta);
}

/*
 * Check an Intel SW RAID set.
 *
 * FIXME: more sanity checks.
 */
static unsigned int devices(struct raid_dev *rd, void *context)
{
	return ((struct isw_dev*) rd->private.ptr)->vol.map.num_members;
}

static int check_rd(struct lib_context *lc, struct raid_set *rs,
		    struct raid_dev *rd, void *context)
{
	struct isw_dev *dev = rd->private.ptr;

	/* FIXME: more status checks ? */
	if (dev->status)
		LOG_ERR(lc, 0, "%s device for volume \"%s\" broken on %s "
			"in RAID set \"%s\"",
			handler, dev->volume, rd->di->path, rs->name);

	return 1;
}

static int _isw_check(struct lib_context *lc, struct raid_set *rs)
{
	return check_raid_set(lc, rs, devices, NULL, check_rd, NULL, handler);
}

static int isw_check(struct lib_context *lc, struct raid_set *rs)
{
	return T_GROUP(rs) ? _isw_check(lc, rs) : 0;
}

/*
 * IO error event handler.
 */
static int event_io(struct lib_context *lc, struct event_io *e_io)
{
	struct raid_dev *rd = e_io->rd;
	struct isw *isw = META(rd, isw);
	struct isw_disk *disk;

	if (!(disk = get_disk(lc, rd->di, isw)))
		LOG_ERR(lc, 0, "%s: disk", handler);

	/* Avoid write trashing. */
	if (S_BROKEN(status(lc, rd)))
		return 0;

	disk->status &= ~USABLE_DISK;
	disk->status |= FAILED_DISK;

	return 1;
}

static struct event_handlers isw_event_handlers = {
	.io = event_io,
	.rd = NULL,	/* FIXME: no device add/remove event handler yet. */
};

#ifdef DMRAID_NATIVE_LOG
/*
 * Log native information about an ISW RAID device.
 */
static void isw_log(struct lib_context *lc, struct raid_dev *rd)
{
	unsigned int d, i;
	struct isw *isw = META(rd, isw);
	struct isw_disk *disk;
	struct isw_dev *dev;

	log_print(lc, "%s (%s):", rd->di->path, handler);
	P("sig: \"%*s\"", isw, isw->sig, MAX_SIGNATURE_LENGTH, isw->sig);
	DP("check_sum: %u", isw, isw->check_sum);
	DP("mpb_size: %u", isw, isw->mpb_size);
	DP("family_num: %u", isw, isw->family_num);
	DP("generation_num: %u", isw, isw->generation_num);
	DP("reserved[0]: %u", isw, isw->reserved[0]);
	DP("reserved[1]: %u", isw, isw->reserved[1]);
	DP("num_disks: %u", isw, isw->num_disks);
	DP("num_raid_devs: %u", isw, isw->num_raid_devs);
	DP("fill[0]: %u", isw, isw->fill[0]);
	DP("fill[1]: %u", isw, isw->fill[1]);

	for (i = 0; i < ISW_FILLERS; i++) {
		if (isw->filler[i])
        		P("filler[%i]: %u", isw,
			  isw->filler[i], i, isw->filler[i]);
	}

	/* Disk table. */
	for (d = 0, disk = isw->disk; d < isw->num_disks; d++, disk++) {
		if (!disk->totalBlocks)
			continue;

		P("disk[%u].serial: \"%*s\"", isw,
		  disk->serial, d, MAX_RAID_SERIAL_LEN, disk->serial);
		P("disk[%u].totalBlocks: %u", isw,
		  disk->totalBlocks, d, disk->totalBlocks);
		P("disk[%u].scsiId: 0x%x", isw, disk->scsiId, d, disk->scsiId);
		P("disk[%u].status: 0x%x", isw, disk->status, d, disk->status);
		for (i = 0; i < ISW_DISK_FILLERS; i++) {
			if (disk->filler[i])
                		P("disk[%u].filler[%u]: %u", isw,
				  disk->filler[i], d, i, disk->filler[i]);
		}
	}

	/* RAID device/volume table. */
	for (d = 0; d < isw->num_raid_devs; d++) {
		dev = raiddev(isw, d);

		/* RAID device */
		P("isw_dev[%u].volume: \"%*s\"", isw,
		  dev->volume, d, MAX_RAID_SERIAL_LEN, dev->volume);
		P("isw_dev[%u].SizeHigh: %u", isw,
		  dev->SizeHigh, d, dev->SizeHigh);
		P("isw_dev[%u].SizeLow: %u", isw,
		  dev->SizeLow, d, dev->SizeLow);
		P("isw_dev[%u].status: 0x%x", isw, dev->status, d, dev->status);
		P("isw_dev[%u].reserved_blocks: %u", isw,
		  dev->reserved_blocks, d, dev->reserved_blocks);

		for (i = 0; i < ISW_DEV_FILLERS; i++) {
			if (dev->filler[i])
                		P("isw_dev[%u].filler[%u]: %u", isw,
				  dev->filler[i], d, i, dev->filler[i]);
		}

		/* RAID volume */
		for (i = 0; i < 2; i++) {
			if (dev->vol.reserved[i])
                		P("isw_dev[%u].vol.reserved[%u]: %u", isw,
				  dev->vol.reserved[i], d, i,
				  dev->vol.reserved[i]);
		}

		P("isw_dev[%u].vol.migr_state: %u", isw,
		  dev->vol.migr_state, d, dev->vol.migr_state);
		P("isw_dev[%u].vol.migr_type: %u", isw,
		  dev->vol.migr_type, d, dev->vol.migr_type);
		P("isw_dev[%u].vol.dirty: %u", isw,
		  dev->vol.dirty, d, dev->vol.dirty);
		P("isw_dev[%u].vol.fill[0]: %u", isw,
		  dev->vol.fill[0], d, dev->vol.fill[0]);

		for (i = 0; i < 5; i++) {
			if (dev->vol.filler[i])
                		P("isw_dev[%u].vol.filler[%u]: %u", isw,
				  dev->vol.filler[i], d, i,
				  dev->vol.filler[i]);
		}

		/* RAID map */
		P("isw_dev[%u].vol.map.pba_of_lba0: %u", isw,
		  dev->vol.map.pba_of_lba0, d,
		  dev->vol.map.pba_of_lba0);
		P("isw_dev[%u].vol.map.blocks_per_member: %u", isw,
		  dev->vol.map.blocks_per_member, d,
		  dev->vol.map.blocks_per_member);
		P("isw_dev[%u].vol.map.num_data_stripes: %u", isw,
		  dev->vol.map.num_data_stripes, d,
		  dev->vol.map.num_data_stripes);
		P("isw_dev[%u].vol.map.blocks_per_strip: %u", isw,
		  dev->vol.map.blocks_per_strip, d,
		  dev->vol.map.blocks_per_strip);
		P("isw_dev[%u].vol.map.map_state: %u", isw,
		  dev->vol.map.map_state, d,
		  dev->vol.map.map_state);
		P("isw_dev[%u].vol.map.raid_level: %u", isw,
		  dev->vol.map.raid_level, d,
		  dev->vol.map.raid_level);
		P("isw_dev[%u].vol.map.num_members: %u", isw,
		  dev->vol.map.num_members, d,
		  dev->vol.map.num_members);

		for (i = 0; i < 3; i++) {
			if (dev->vol.map.reserved[i])
                		P("isw_dev[%u].vol.map.reserved[%u]: %u", isw,
				  dev->vol.map.reserved[i], d, i,
				  dev->vol.map.reserved[i]);
		}

		for (i = 0; i < 7; i++) {
			if (dev->vol.map.filler[i])
                		P("isw_dev[%u].vol.map.filler[%u]: %u", isw,
				  dev->vol.map.filler[i], d, i,
				  dev->vol.map.filler[i]);
		}

		for (i = 0; i < isw->num_disks; i++) {
			P("isw_dev[%u].vol.map.disk_ord_tbl[%u]: 0x%x", isw,
			  dev->vol.map.disk_ord_tbl[i], d, i,
			  dev->vol.map.disk_ord_tbl[i]);
		}
	}
}
#endif

static struct dmraid_format isw_format = {
	.name	= HANDLER,
	.descr	= "Intel Software RAID",
	.caps	= "0,1",
	.format = FMT_RAID,
	.read	= isw_read,
	.write	= isw_write,
	.group	= isw_group,
	.check	= isw_check,
	.events	= &isw_event_handlers,
#ifdef DMRAID_NATIVE_LOG
	.log	= isw_log,
#endif
};

/* Register this format handler with the format core. */
int register_isw(struct lib_context *lc)
{
	return register_format_handler(lc, &isw_format);
}

/*
 * Set the RAID device contents up derived from the Intel ones.
 *
 * This is the first one we get with here and we potentially need to
 * create many in isw_group() in case of multiple Intel SW RAID devices
 * on this RAID disk.
 */
static int setup_rd(struct lib_context *lc, struct raid_dev *rd,
		    struct dev_info *di, void *meta, union read_info *info)
{
	struct isw *isw = meta;

	/* Superblock checksum */
	if (isw->check_sum != _checksum(isw))
		LOG_ERR(lc, 0, "%s: extended superblock for %s "
			       "has wrong checksum",
			handler, di->path);

	if (!(rd->meta_areas = alloc_meta_areas(lc, rd, handler, 1)))
		return 0;

	rd->meta_areas->offset = info->u64 >> 9;
	rd->meta_areas->size = round_up(isw->mpb_size, ISW_DISK_BLOCK_SIZE);
	rd->meta_areas->area = (void*) isw;

	rd->di = di;
	rd->fmt = &isw_format;

	rd->offset = ISW_DATAOFFSET;
	if (!(rd->sectors = info->u64 >> 9))
		return log_zero_sectors(lc, di->path, handler);

	rd->status = status(lc, rd);
	rd->type   = t_group;

        return (rd->name = name(lc, isw, NULL)) ? 1 : 0;
}

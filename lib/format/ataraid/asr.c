/*
 * Adaptec HostRAID ASR format interpreter for dmraid.
 * Copyright (C) 2005-2006 IBM, All rights reserved.
 * Written by Darrick Wong <djwong@us.ibm.com>
 *
 * See file LICENSE at the top of this source tree for license information.
 */

#include <errno.h>
#include <netinet/in.h>

#define	HANDLER	"asr"

#include "internal.h"
#define	FORMAT_HANDLER
#include "asr.h"

#if	BYTE_ORDER == LITTLE_ENDIAN
#  define	DM_BYTEORDER_SWAB
#  include	<datastruct/byteorder.h>
#endif

static const char *handler = HANDLER;

#define SPARE_ARRAY	".asr_spares"

static int asr_write(struct lib_context *lc,  struct raid_dev *rd, int erase);

/* Map ASR disk status to dmraid status */
static enum status disk_status(struct asr_raid_configline *disk) {
	if (disk == NULL)
		return s_undef;

	switch (disk->raidstate) {
	case LSU_COMPONENT_STATE_OPTIMAL:
		return s_ok;

	case LSU_COMPONENT_STATE_DEGRADED:
	case LSU_COMPONENT_STATE_FAILED:
		return s_broken;

	case LSU_COMPONENT_STATE_UNINITIALIZED:
	case LSU_COMPONENT_STATE_UNCONFIGURED:
		return s_inconsistent;

	case LSU_COMPONENT_SUBSTATE_BUILDING:
	case LSU_COMPONENT_SUBSTATE_REBUILDING:
	case LSU_COMPONENT_STATE_REPLACED:
		return s_nosync;

	default:
		return s_undef;
	}
}
		
/* Extract config line from metadata */
static struct asr_raid_configline *get_config(struct asr *asr, uint32_t magic)
{
	unsigned int i;
	
	for (i = 0; i < asr->rt->elmcnt; i++) {
		if (asr->rt->ent[i].raidmagic == magic)
			return asr->rt->ent + i;
	}

	return NULL;
}

/* Get this disk's configuration */
static struct asr_raid_configline *this_disk(struct asr *asr)
{
	return get_config(asr, asr->rb.drivemagic);
}

/* Make up RAID device name. */
static size_t _name(struct lib_context *lc, struct asr *asr, char *str,
		    size_t len)
{
	struct asr_raid_configline *cl = this_disk(asr);

	if (cl)
		return snprintf(str, len, "%s_%s", HANDLER, cl->name);

	LOG_ERR(lc, 0, "%s: Could not find device in config table!", handler);
}

/* Figure out a name for the RAID device. */
static char *name(struct lib_context *lc, struct asr *asr)
{
	size_t len;
	char *ret;

	if ((ret = dbg_malloc((len = _name(lc, asr, NULL, 0) + 1)))) {
		_name(lc, asr, ret, len);
		/* Why do we call mk_alpha?  This makes labels like
		 * "OS-u320-15k" become "OS-udca-bek", which is confusing.
		 * mk_alpha(lc, ret + HANDLER_LEN, len - HANDLER_LEN); */
	} else
		log_alloc_err(lc, handler);

	return ret;
}

/* Stride size */
static inline unsigned int stride(struct asr_raid_configline *cl)
{
	return cl ? cl->strpsize: 0;
}

/* Mapping of template types to generic types */
/*
 * FIXME: This needs more examination.  Does HostRAID do linear
 * combination?  The BIOS implies that it only does RAID 0, 1 and 10.
 * The emd driver implied support for RAID3/4/5, but dm doesn't
 * do any of those right now (RAID4 and RAID5 are in the works).
 */
static struct types types[] = {
	{ ASR_RAID0,   t_raid0 },
	{ ASR_RAID1,   t_raid1 },
	{ ASR_RAIDSPR, t_spare },
        { 0, t_undef}
};

/* Map the ASR raid type codes into dmraid type codes. */
static enum type type(struct asr_raid_configline *cl)
{
	return cl ? rd_type(types, (unsigned int) cl->raidtype) : t_undef;
}

/*
 * Read an ASR RAID device.  Fields are big endian, so
 * need to convert them if we're on a LE machine (i386, etc).
 */
#define ASR_BLOCK	0x01
#define ASR_TABLE	0x02
#define ASR_EXTTABLE 	0x04

#if	BYTE_ORDER == LITTLE_ENDIAN
static void cvt_configline(struct asr_raid_configline *cl)
{
	CVT16(cl->raidcnt);
	CVT16(cl->raidseq);
	CVT32(cl->raidmagic);
	CVT32(cl->raidid);
	CVT32(cl->loffset);
	CVT32(cl->lcapcty);
	CVT16(cl->strpsize);
	CVT16(cl->biosInfo);
	CVT32(cl->lsu);
	CVT16(cl->blockStorageTid);
	CVT32(cl->curAppBlock);
	CVT32(cl->appBurstCount);
}

static void to_cpu(void *meta, unsigned int cvt)
{
	int i;
	struct asr *asr = meta;
	int elmcnt = asr->rt->elmcnt;

	int use_old_elmcnt = (asr->rt->ridcode == RVALID2);

	if (cvt & ASR_BLOCK) {
		CVT32(asr->rb.b0idcode);
		CVT16(asr->rb.biosInfo);
		CVT32(asr->rb.fstrsvrb);
		CVT16(asr->rb.svBlockStorageTid);
		CVT16(asr->rb.svtid);
		CVT32(asr->rb.drivemagic);
		CVT32(asr->rb.fwTestMagic);
		CVT32(asr->rb.fwTestSeqNum);
		CVT32(asr->rb.smagic);
		CVT32(asr->rb.raidtbl);
	}

	if (cvt & ASR_TABLE) {
		CVT32(asr->rt->ridcode);
		CVT32(asr->rt->rversion);
		CVT16(asr->rt->maxelm);
		CVT16(asr->rt->elmcnt);
		if (!use_old_elmcnt)
			elmcnt = asr->rt->elmcnt;
		CVT16(asr->rt->elmsize);
		CVT32(asr->rt->raidFlags);
		CVT32(asr->rt->timestamp);
		CVT16(asr->rt->rchksum);
		CVT32(asr->rt->sparedrivemagic);
		CVT32(asr->rt->raidmagic);
		CVT32(asr->rt->verifyDate);
		CVT32(asr->rt->recreateDate);

		/* Convert the first seven config lines */
		for (i = 0; i < (elmcnt < 7 ? elmcnt : 7); i++) 
			cvt_configline(asr->rt->ent + i);
		
	}

	if (cvt & ASR_EXTTABLE) {
		for (i = 7; i < elmcnt; i++) {
			cvt_configline(asr->rt->ent + i);
		}
	}
}

#else
# define to_cpu(x, y)
#endif

/* Compute the checksum of RAID metadata */
static unsigned int compute_checksum(struct asr *asr)
{
	uint8_t *ptr;
	unsigned int i, checksum;

	/* Compute checksum. */
	ptr = (uint8_t*) asr->rt->ent;
	checksum = 0;
	for (i = 0; i < sizeof(*asr->rt->ent) * asr->rt->elmcnt; i++)
		checksum += ptr[i];

	return checksum & 0xFFFF;
}

/* Read extended metadata areas */
static int read_extended(struct lib_context *lc, struct dev_info *di,
			 struct asr *asr)
{
	unsigned int remaining, i, chk;
	int j;

	log_info(lc, "%s: reading extended data", di->path);
	
	/* Read the RAID table. */
	if (!read_file(lc, handler, di->path, asr->rt, ASR_DISK_BLOCK_SIZE,
		       (uint64_t) asr->rb.raidtbl * ASR_DISK_BLOCK_SIZE))
		LOG_ERR(lc, 0, "%s: Could not read metadata.", handler);

	/* Convert it */
	to_cpu(asr, ASR_TABLE);
	
	/* Is this ok? */
	if (asr->rt->ridcode != RVALID2)
		LOG_ERR(lc, 0, "%s: Invalid magic number in RAID table; "
			"saw 0x%X, expected 0x%X.", handler, asr->rt->ridcode,
			RVALID2);

	/* Have we a valid element count? */
	if (asr->rt->elmcnt >= asr->rt->maxelm)
		LOG_ERR(lc, 0, "%s: Invalid RAID config table count.\n",
			handler);

	/* Is each element the right size? */
	if (asr->rt->elmsize != sizeof(struct asr_raid_configline))
		LOG_ERR(lc, 0, "%s: RAID config line is the wrong size.\n",
			handler);

	/* Figure out how much else we need to read. */
	if (asr->rt->elmcnt > 7) {
		remaining = asr->rt->elmsize * (asr->rt->elmcnt - 7);
		if (!read_file(lc, handler, di->path, asr->rt->ent + 7,
			       remaining, (uint64_t)(asr->rb.raidtbl + 1) *
			       ASR_DISK_BLOCK_SIZE))
			return 0;

		to_cpu(asr, ASR_EXTTABLE);
	}

	chk = compute_checksum(asr);
	if (chk != asr->rt->rchksum)
		LOG_ERR(lc, 0,"%s: Invalid RAID config table checksum "
			       "(0x%X vs. 0x%X).",
			handler, chk, asr->rt->rchksum);
	
	/* Process the name of each line of the config line. */
	for (i = 0; i < asr->rt->elmcnt; i++) {
		/* 
		 * Weird quirks of the name field of the config line:
		 *
		 * - SATA HostRAID w/ ICH5 on IBM x226: The name field is null
		 *   in the drive config lines.  The zeroeth item does have a
		 *   name, however.
		 * - Spares on SCSI HostRAID on IBM x226: The name field for
		 *   all config lines is null.
		 * 
		 * So, we'll assume that we can copy the name from the zeroeth
		 * element in the array.  The twisted logic doesn't seem to
		 * have a problem with either of the above cases, though
		 * attaching spares is going to be a tad tricky (primarily
		 * because there doesn't seem to be a way to attach a spare to
		 * a particular array; presumably the binary driver knows how
		 * or just grabs a disk out of the spare pool.
		 *
		 * (Yes, the binary driver _does_ just grab a disk from the
		 * global spare pool.  We must teach dm about this...?)
		 *
		 * This is nuts.
		 */
		if (!asr->rt->ent[i].name)
			memcpy(asr->rt->ent[i].name, asr->rt->ent[0].name, 16);

		/* Now truncate trailing whitespace in the name. */
		for (j = 15; j >= 0; j--) {
			if (asr->rt->ent[i].name[j] != ' ')
				break;
		}
		asr->rt->ent[i].name[j + 1] = 0;
	}

	return 1;
}

static int is_asr(struct lib_context *lc, struct dev_info *di, void *meta)
{
	struct asr *asr = meta;

	/*
	 * Check our magic numbers and that the version == v8.
	 * We don't support anything other than that right now.
	 */
	if (asr->rb.b0idcode == B0RESRVD &&
	    asr->rb.smagic == SVALID) {
		if (asr->rb.resver == RBLOCK_VER)
			return 1;
		
		LOG_ERR(lc, 0,
			"%s: ASR v%d detected, but we only support v8.\n",
			handler, asr->rb.resver);
	}

	return 0;
}

/*
 * Attempt to interpret ASR metadata from a block device.  This function
 * returns either NULL (not an ASR) or a pointer to a descriptor struct.
 * Note that the struct should be fully converted to the correct endianness
 * by the time this function returns.
 *
 * WARNING: If you take disks out of an ASR HostRAID array and plug them in
 * to a normal SCSI controller, the array will still show up!  Even if you
 * scribble over the disks!  I assume that the a320raid binary driver only
 * does its HostRAID magic if your controller is in RAID mode... but dmraid
 * lacks this sort of visibility as to where its block devices come from.
 * This is EXTREMELY DANGEROUS if you aren't careful!
 */
static void *read_metadata_areas(struct lib_context *lc, struct dev_info *di,
				 size_t *sz, uint64_t *offset,
				 union read_info *info)
{
	size_t size = ASR_DISK_BLOCK_SIZE;
	uint64_t asr_sboffset = ASR_CONFIGOFFSET;
	struct asr *asr;
	struct asr_raid_configline *cl;

	/*
	 * Read the ASR reserved block on each disk.  This is the very
	 * last sector of the disk, and we're really only interested in
	 * the two magic numbers, the version, and the pointer to the
	 * RAID table.  Everything else appears to be unused in v8.
	 */
	if (!(asr = alloc_private(lc, handler, sizeof(struct asr))))
		goto bad0;
	
	if (!(asr->rt = alloc_private(lc, handler, sizeof(struct asr_raidtable))))
		goto bad1;

	if (!read_file(lc, handler, di->path, &asr->rb, size, asr_sboffset))
		goto bad2;

	/*
	 * Convert metadata and read in 
	 */
	to_cpu(asr, ASR_BLOCK);

	/* Check Signature and read optional extended metadata. */
	if (!is_asr(lc, di, asr) ||
	    !read_extended(lc, di, asr))
		goto bad2;

	/*
	 * Now that we made sure that we have all the metadata, we exit.
	 */
	cl = this_disk(asr);
	if (cl->raidstate == LSU_COMPONENT_STATE_FAILED)
		goto bad2;

	goto out;

   bad2:
	dbg_free(asr->rt);
   bad1:
	asr->rt = NULL;
	dbg_free(asr);
   bad0:
	asr = NULL;

   out:
	return (void*) asr;
}

/*
 * "File the metadata areas" -- I think this function is supposed to declare
 * which parts of the drive are metadata and thus off-limits to dmraid.
 */
static void file_metadata_areas(struct lib_context *lc, struct dev_info *di,
				void *meta)
{
	struct asr *asr = meta;

	/* Register the raid tables. */
	file_metadata(lc, handler, di->path, asr->rt,
		      ASR_DISK_BLOCK_SIZE * 17,
		      (uint64_t)asr->rb.raidtbl * ASR_DISK_BLOCK_SIZE);

	/* Record the device size if -D was specified. */
	file_dev_size(lc, handler, di);
}

static int setup_rd(struct lib_context *lc, struct raid_dev *rd,
		    struct dev_info *di, void *meta, union read_info *info);

static struct raid_dev *asr_read(struct lib_context *lc,
					struct dev_info *di)
{
	/*
	 * NOTE: Everything called after read_metadata_areas assumes that
	 * the reserved block, raid table and config table have been
	 * converted to the appropriate endianness.
	 */
	return read_raid_dev(lc, di, read_metadata_areas, 0, 0, NULL, NULL,
			     file_metadata_areas, setup_rd, handler);
}

static int set_sort(struct list_head *dont, struct list_head *care)
{
	return 0;
}

/*
 * Compose a 64-bit ID for device sorting.
 * Is hba:ch:lun:id ok?  It seems to be the way the binary driver
 * does it...
 */
static inline uint64_t compose_id(struct asr_raid_configline *cl)
{
	return    ((uint64_t) cl->raidhba  << 48)
		| ((uint64_t) cl->raidchnl << 40)
		| ((uint64_t) cl->raidlun  << 32)
		| (uint64_t) cl->raidid;
}

/* Sort ASR devices by for a RAID set. */
static int dev_sort(struct list_head *pos, struct list_head *new)
{
	return compose_id(this_disk(META(RD(new), asr))) <
	       compose_id(this_disk(META(RD(pos), asr)));
}

/*
 * Find the top-level RAID set for an ASR context.
 */
static int find_toplevel(struct lib_context *lc, struct asr *asr)
{
	int i, toplevel = -1;

	for (i = 0; i < asr->rt->elmcnt; i++) {
		if (asr->rt->ent[i].raidlevel == FWL)
		{
			toplevel = i;
		}
		else if (asr->rt->ent[i].raidlevel == FWL_2)
		{
			toplevel = i;
			break;
		}
	}
	
	return toplevel;
}

/*
 * Find the logical drive configuration that goes with this
 * physical disk configuration.
 */
static struct asr_raid_configline *find_logical(struct asr *asr)
{
	int i, j;

	/* This MUST be done backwards! */
	for (i = asr->rt->elmcnt - 1; i > -1; i--) {
		if (asr->rt->ent[i].raidmagic == asr->rb.drivemagic)
		{
			for (j = i - 1; j > -1; j--) {
				if (asr->rt->ent[j].raidlevel == FWL)
				{
					return &asr->rt->ent[j];
				}
			}
		}
	}

	return NULL;
}

/* Wrapper for name() */
static char *js_name(struct lib_context *lc, struct raid_dev *rd,
		  unsigned int subset)
{
	return name(lc, META(rd, asr));
}

/*
 * IO error event handler.
 */
static int event_io(struct lib_context *lc, struct event_io *e_io)
{
	struct raid_dev *rd = e_io->rd;
	struct asr *asr = META(rd, asr);
	struct asr_raid_configline *cl = this_disk(asr);
	struct asr_raid_configline *fwl = find_logical(asr);

	/* Ignore if we've already marked this disk broken(?) */
	if (rd->status & s_broken)
		return 0;
	
	log_err(lc, "I/O error on device %s at sector %lu.",
		e_io->rd->di->path, e_io->sector);

	/* Mark the array as degraded and the disk as failed. */
	rd->status = s_broken;
	cl->raidstate = LSU_COMPONENT_STATE_FAILED;
	fwl->raidstate = LSU_COMPONENT_STATE_DEGRADED;
	/* FIXME: Do we have to mark a parent too? */

	/* Indicate that this is indeed a failure. */
	return 1;
}

/* 
 * Add an ASR device to a RAID set.  This involves finding the raid set to
 * which this disk belongs, and then attaching it.  Note that there are other
 * complications, such as two-layer arrays (RAID10).
 */
#define BUFSIZE 128
static struct raid_set *asr_group(struct lib_context *lc, struct raid_dev *rd)
{
	int top_idx;
	struct asr *asr = META(rd, asr);
	struct asr_raid_configline *cl = this_disk(asr);
	struct asr_raid_configline *fwl;
	struct raid_set *set, *sset;
	char buf[BUFSIZE];

	if (T_SPARE(rd)) {
		/*
		 * If this drive really _is_ attached to a specific
		 * RAID set, then just attach it.  Really old HostRAID cards
		 * do this... but I don't have any hardware to test this.
		 */
		/*
		 * FIXME: dmraid ignores spares attached to RAID arrays.
		 * For now, we'll let it get sucked into the ASR spare pool. 
		 * If we need it, we'll reconfigure it; if not, nobody touches
		 * it.
		 *
		set = find_set(lc, name(lc, asr), FIND_TOP, rd, LC_RS(lc),
			       NO_CREATE, NO_CREATE_ARG);
		 */

		/* Otherwise, make a global spare pool. */
		set = find_or_alloc_raid_set(lc, (char*)SPARE_ARRAY,
			FIND_TOP, rd, LC_RS(lc), NO_CREATE, NO_CREATE_ARG);

		/*
		 * Setting the type to t_spare guarantees that dmraid won't
		 * try to set up a real device-mapper mapping.
		 */
		set->type = t_spare;

		/* Add the disk to the set. */
		list_add_sorted(lc, &set->devs, &rd->devs, dev_sort);
		return set;
	}

	/* Find the top level FWL/FWL2 for this device. */
	top_idx = find_toplevel(lc, asr);
	if (top_idx < 0) {
		LOG_ERR(lc, NULL, "Can't find a logical array config "
			"for disk %x\n",
			asr->rb.drivemagic);
	}

	/* This is a simple RAID0/1 array.  Find the set. */
	if (asr->rt->ent[top_idx].raidlevel == FWL)
	{
		set = find_or_alloc_raid_set(lc, name(lc, asr),
			FIND_TOP, rd, LC_RS(lc), NO_CREATE, NO_CREATE_ARG);

		set->stride = stride(cl);
		set->status = s_ok;
		set->type = type(find_logical(asr));

		/* Add the disk to the set. */
		list_add_sorted(lc, &set->devs, &rd->devs, dev_sort);
		
		return set;
	}

	/*
	 * This is a two-level RAID array.  Attach the disk to the disk's
	 * parent set; create it if necessary.  Then, find the top-level set
	 * and use join_superset to attach the parent set to the top set.
	 */
	if (asr->rt->ent[top_idx].raidlevel == FWL_2)
	{
		/* First compute the name of the disk's direct parent. */
		fwl = find_logical(asr);
		snprintf(buf, BUFSIZE, ".asr_%s_%x_donotuse",
			 fwl->name, fwl->raidmagic);
		
		/* Now find said parent. */
		set = find_or_alloc_raid_set(lc, buf,
			FIND_ALL, rd, NO_LIST, NO_CREATE, NO_CREATE_ARG);

		if (!set)
			LOG_ERR(lc, NULL, "Error creating RAID set.\n");

		set->stride = stride(cl);
		set->status = s_ok;
		set->type = type(fwl);

		/* Add the disk to the set. */
		list_add_sorted(lc, &set->devs, &rd->devs, dev_sort);
		
		/* Find the top level set. */
		sset = join_superset(lc, js_name, NO_CREATE,
				     set_sort, set, rd);

		if (!sset)
			LOG_ERR(lc, NULL, "Error creating top RAID set.\n");

		sset->stride = stride(cl);
		sset->status = s_ok;
		sset->type = type(&asr->rt->ent[top_idx]);

		return sset;
	}

	/* If we land here, something's seriously wrong. */
	LOG_ERR(lc, NULL, "Top level array config is not FWL/FWL2?\n");
}

/* Write metadata. */
static int asr_write(struct lib_context *lc,  struct raid_dev *rd, int erase)
{
	int ret, i, j;
        struct asr *asr = META(rd, asr);
	int elmcnt = asr->rt->elmcnt;

	/* Untruncate trailing whitespace in the name. */
	for (i = 0; i < elmcnt; i++) {
		for (j = 15; j >= 0; j--) {
			if (asr->rt->ent[i].name[j] == 0)
				break;
		}
		asr->rt->ent[i].name[j] = ' ';
	}

	/* Compute checksum */
	asr->rt->rchksum = compute_checksum(asr);

	/* Convert back to disk format */
        to_disk(asr, ASR_BLOCK | ASR_TABLE | ASR_EXTTABLE);

	/* Write data */
        ret = write_metadata(lc, handler, rd, -1, erase);
	
	/* Go back to CPU format */
        to_cpu(asr, ASR_BLOCK | ASR_TABLE | ASR_EXTTABLE);
 
	/* Truncate trailing whitespace in the name. */
	for (i = 0; i < elmcnt; i++) {
		for (j = 15; j >= 0; j--) {
			if (asr->rt->ent[i].name[j] != ' ')
				break;
		}
		asr->rt->ent[i].name[j + 1] = 0;
	}

        return ret;
}

/*
 * Check integrity of a RAID set.
 */

/* Retrieve the number of devices that should be in this set. */
static unsigned int device_count(struct raid_dev *rd, void *context)
{
	/* Get the logical drive */
	struct asr_raid_configline *cl = find_logical(META(rd, asr));
	return (cl ? cl->raidcnt : 0);
}

/* Check a RAID device */
static int check_rd(struct lib_context *lc, struct raid_set *rs,
		    struct raid_dev *rd, void *context)
{
	/* FIXME: Assume non-broken means ok. */
	return (rd->type != s_broken);
}

/* Start the recursive RAID set check. */
static int asr_check(struct lib_context *lc, struct raid_set *rs)
{
	return check_raid_set(lc, rs, device_count, NULL, check_rd,
			      NULL, handler);
}

static struct event_handlers asr_event_handlers = {
	.io = event_io,
	.rd = NULL,	/* FIXME: no device add/remove event handler yet. */
};

/* Dump a reserved block */
static void dump_rb(struct lib_context *lc, struct asr_reservedblock *rb)
{
	DP("block magic:\t\t0x%X", rb, rb->b0idcode);
	DP("sb0flags:\t\t\t0x%X", rb, rb->sb0flags);
	DP("jbodEnable:\t\t%d", rb, rb->jbodEnable);
	DP("biosInfo:\t\t\t0x%X", rb, rb->biosInfo);
	DP("drivemagic:\t\t0x%X", rb, rb->drivemagic);
	DP("svBlockStorageTid:\t0x%X", rb, rb->svBlockStorageTid);
	DP("svtid:\t\t\t0x%X", rb, rb->svtid);
	DP("resver:\t\t\t%d", rb, rb->resver);
	DP("smagic:\t\t\t0x%X", rb, rb->smagic);
	DP("raidtbl @ sector:\t\t%d", rb, rb->raidtbl);
}

/* Dump a raid config line */
static void dump_cl(struct lib_context *lc, struct asr_raid_configline *cl)
{
	DP("config ID:\t\t0x%X", cl, cl->raidmagic);
	DP("  name:\t\t\t\"%s\"", cl, cl->name);
	DP("  raidcount:\t\t%d", cl, cl->raidcnt);
	DP("  sequence #:\t\t%d", cl, cl->raidseq);
	DP("  level:\t\t\t%d", cl, cl->raidlevel);
	DP("  type:\t\t\t%d", cl, cl->raidtype);
	DP("  state:\t\t\t%d", cl, cl->raidstate);
	DP("  flags:\t\t\t0x%X", cl, cl->flags);
	DP("  refcount:\t\t%d", cl, cl->refcnt);
	DP("  hba:\t\t\t%d", cl, cl->raidhba);
	DP("  channel:\t\t%d", cl, cl->raidchnl);
	DP("  lun:\t\t\t%d", cl, cl->raidlun);
	DP("  id:\t\t\t%d", cl, cl->raidid);
	DP("  offset:\t\t\t%d", cl, cl->loffset);
	DP("  capacity:\t\t%d", cl, cl->lcapcty);
	P("  stripe size:\t\t%d KB",
	  cl, cl->strpsize, cl->strpsize * ASR_DISK_BLOCK_SIZE / 1024);
	DP("  BIOS info:\t\t%d", cl, cl->biosInfo);
	DP("  phys/log lun:\t\t%d", cl, cl->lsu);
	DP("  addedDrives:\t\t%d", cl, cl->addedDrives);
	DP("  appSleepRate:\t\t%d", cl, cl->appSleepRate);
	DP("  blockStorageTid:\t%d", cl, cl->blockStorageTid);
	DP("  curAppBlock:\t\t%d", cl, cl->curAppBlock);
	DP("  appBurstCount:\t\t%d", cl, cl->appBurstCount);
}

/* Dump a raid config table */
static void dump_rt(struct lib_context *lc, struct asr_raidtable *rt)
{
	unsigned int i;

	DP("ridcode:\t\t\t0x%X", rt, rt->ridcode);
	DP("table ver:\t\t%d", rt, rt->rversion);
	DP("max configs:\t\t%d", rt, rt->maxelm);
	DP("configs:\t\t\t%d", rt, rt->elmcnt);
	DP("config sz:\t\t%d", rt, rt->elmsize);
	DP("checksum:\t\t\t0x%X", rt, rt->rchksum);
	DP("raid flags:\t\t0x%X", rt, rt->raidFlags);
	DP("timestamp:\t\t0x%X", rt, rt->timestamp);
	P("irocFlags:\t\t%X%s", rt, rt->irocFlags, rt->irocFlags,
	  rt->irocFlags & ASR_IF_BOOTABLE ? " (bootable)" : "");
	DP("dirt, rty:\t\t%d", rt, rt->dirty);
	DP("action prio:\t\t%d", rt, rt->actionPriority);
	DP("spareid:\t\t\t%d", rt, rt->spareid);
	DP("sparedrivemagic:\t\t0x%X", rt, rt->sparedrivemagic);
	DP("raidmagic:\t\t0x%X", rt, rt->raidmagic);
	DP("verifydate:\t\t0x%X", rt, rt->verifyDate);
	DP("recreatedate:\t\t0x%X", rt, rt->recreateDate);

	log_print(lc, "\nRAID config table:");
	for (i = 0; i < rt->elmcnt; i++)
		dump_cl(lc, &rt->ent[i]);
}

#ifdef DMRAID_NATIVE_LOG
/*
 * Log native information about the RAID device.
 */
static void asr_log(struct lib_context *lc, struct raid_dev *rd)
{
	struct asr *asr = META(rd, asr);

	log_print(lc, "%s (%s):", rd->di->path, handler);
	dump_rb(lc, &asr->rb);
	dump_rt(lc, asr->rt);
}
#endif

static struct dmraid_format asr_format = {
	.name	= HANDLER,
	.descr	= "Adaptec HostRAID ASR",
	.caps	= "0,1,10",
	.format = FMT_RAID,
	.read	= asr_read,
	.write	= asr_write,
	.group	= asr_group,
	.check	= asr_check,
	.events	= &asr_event_handlers,
#ifdef DMRAID_NATIVE_LOG
	.log	= asr_log,
#endif
};

/* Register this format handler with the format core */
int register_asr(struct lib_context *lc)
{
	return register_format_handler(lc, &asr_format);
}

/*
 * Set up a RAID device from what we've assembled out of the metadata.
 */
static int setup_rd(struct lib_context *lc, struct raid_dev *rd,
		    struct dev_info *di, void *meta, union read_info *info)
{
	struct asr *asr = meta;
	struct meta_areas *ma;
	struct asr_raid_configline *cl = this_disk(asr);

	if (!cl)
		LOG_ERR(lc, 0, "%s: Could not find current disk!\n",
			handler);		

	/* We need two metadata areas */
	if (!(rd->meta_areas = alloc_meta_areas(lc, rd, handler, 2)))
		return 0;

	/* First area: raid reserved block. */
	ma = rd->meta_areas;
	ma->offset = ASR_CONFIGOFFSET >> 9;
	ma->size = ASR_DISK_BLOCK_SIZE;
	ma->area = (void*) asr;

	/* Second area: raid table. */
	ma++;
	ma->offset = asr->rb.raidtbl;
	ma->size = ASR_DISK_BLOCK_SIZE * 16;
	ma->area = (void*) asr->rt;

	/* Now set up the rest of the metadata info */
        rd->di = di;
	rd->fmt = &asr_format;

	rd->status = disk_status(cl);
	rd->type   = type(cl);

	rd->offset = ASR_DATAOFFSET;
	rd->sectors = cl->lcapcty;

	return (rd->name = name(lc, asr)) ? 1 : 0;
}

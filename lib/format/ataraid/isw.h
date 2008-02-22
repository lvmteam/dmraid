/*
 * Copyright (C) 2003,2004,2005 Intel Corporation. 
 *
 * dmraid extensions:
 * Copyright (C) 2004,2005 Heinz Mauelshagen, Red Hat GmbH.
 *                          All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2.1, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example /usr/src/linux/COPYING); if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Authors: Boji Tony Kannanthanam 
 *          < boji dot t dot kannanthanam at intel dot com >
 *          Martins Krikis
 *          < martins dot krikis at intel dot com >
 */

/*
 * Intel Software Raid metadata definitions.
 */

#ifndef _ISW_H_
#define _ISW_H_

#ifdef	FORMAT_HANDLER
#undef	FORMAT_HANDLER

/* Intel metadata offset in bytes */
#define	ISW_CONFIGOFFSET	((di->sectors - 2) << 9)
#define	ISW_DATAOFFSET		0	/* Data offset in sectors */

#define MPB_SIGNATURE	     "Intel Raid ISM Cfg Sig. "
#define MPB_VERSION_RAID2                             "1.2.02"
#define MAX_SIGNATURE_LENGTH  32
#define MAX_RAID_SERIAL_LEN   16
#define ISW_DISK_BLOCK_SIZE  512
#define TYPICAL_MPBSIZE 1024

/* Disk configuration info. */
struct isw_disk {
	int8_t serial[MAX_RAID_SERIAL_LEN];/* 0xD8 - 0xE7 ascii serial number */
	uint32_t totalBlocks;	/* 0xE8 - 0xEB total blocks */
	uint32_t scsiId;	/* 0xEC - 0xEF scsi ID */
	uint32_t status;	/* 0xF0 - 0xF3 */
#define SPARE_DISK      0x01  /* Spare */
#define CONFIGURED_DISK 0x02  /* Member of some RaidDev */
#define FAILED_DISK     0x04  /* Permanent failure */
#define USABLE_DISK     0x08  /* Fully usable unless FAILED_DISK is set */

#define	ISW_DISK_FILLERS	5
	uint32_t filler[ISW_DISK_FILLERS]; /* 0xF4 - 0x107 MPB_DISK_FILLERS for future expansion */
};

/* RAID map configuration infos. */
struct isw_map {
	uint32_t pba_of_lba0;		// start address of partition
	uint32_t blocks_per_member;	// blocks per member
	uint32_t num_data_stripes;	// number of data stripes
	uint16_t blocks_per_strip;
	uint8_t  map_state;		// Normal, Uninitialized, Degraded, Failed
#define	ISW_T_STATE_NORMAL		0
#define	ISW_T_STATE_UNINITIALIZED	1
	uint8_t  raid_level;
#define	ISW_T_RAID0	0
#define	ISW_T_RAID1	1
#define	ISW_T_RAID5	5		// since metadata version 1.2.02 ?
	uint8_t  num_members;		// number of member disks
	uint8_t  reserved[3];
	uint32_t filler[7];		// expansion area
	uint32_t disk_ord_tbl[1];	/* disk_ord_tbl[num_members],
					   top byte special */
} __attribute__ ((packed));

struct isw_vol {
	uint32_t reserved[2];
	uint8_t  migr_state;		// Normal or Migrating
	uint8_t  migr_type;		// Initializing, Rebuilding, ...
	uint8_t  dirty;
	uint8_t  fill[1];
	uint32_t filler[5];
	struct isw_map map;
	// here comes another one if migr_state
} __attribute__ ((packed));

struct isw_dev {
	uint8_t	volume[MAX_RAID_SERIAL_LEN];
	uint32_t SizeLow;
	uint32_t SizeHigh;
	uint32_t status;	/* Persistent RaidDev status */
	uint32_t reserved_blocks; /* Reserved blocks at beginning of volume */
#define	ISW_DEV_FILLERS	12
	uint32_t filler[ISW_DEV_FILLERS];
	struct isw_vol vol;
} __attribute__ ((packed));

struct isw {
	int8_t sig[MAX_SIGNATURE_LENGTH];/* 0x0 - 0x1F */
	uint32_t check_sum;		/* 0x20 - 0x23  MPB Checksum */
	uint32_t mpb_size;		/* 0x24 - 0x27 Size of MPB */
	uint32_t family_num;		/* 0x28 - 0x2B Checksum from first time this config was written */
	/* 0x2C - 0x2F  Incremented each time this array's MPB is written */
	uint32_t generation_num;
	uint32_t reserved[2];		/* 0x30 - 0x37 */
	uint8_t num_disks;		/* 0x38 Number of configured disks */
	uint8_t num_raid_devs;		/* 0x39 Number of configured volumes */
	uint8_t fill[2];		/* 0x3A - 0x3B */
#define	ISW_FILLERS	39
	uint32_t filler[ISW_FILLERS];	/* 0x3C - 0xD7 RAID_MPB_FILLERS */
	struct isw_disk disk[1];	/* 0xD8 diskTbl[numDisks] */
	// here comes isw_dev[num_raid_devs]
} __attribute__ ((packed));

#endif

int register_isw(struct lib_context *lc);

#endif

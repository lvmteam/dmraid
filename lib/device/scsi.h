/*
 * Copyright (C) 2004,2005  Heinz Mauelshagen, Red Hat GmbH.
 *                          All rights reserved.
 *
 * See file LICENSE at the top of this source tree for license information.
 */

#ifndef	_SCSI_H_
#define	_SCSI_H_

/* Ioctl types possible (SG = SCSI generic, OLD = old SCSI command ioctl. */
enum ioctl_type {
	SG,
	OLD,
};

int get_scsi_serial(struct lib_context *lc, int fd,
		    struct dev_info *di, enum ioctl_type type);

#endif

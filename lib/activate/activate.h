/*
 * Copyright (C) 2004,2005  Heinz Mauelshagen, Red Hat GmbH.
 *                          All rights reserved.
 *
 * See file LICENSE at the top of this source tree for license information.
 */

#ifndef _ACTIVATE_H_
#define _ACTIVATE_H_

enum activate_type {
	A_ACTIVATE,
	A_DEACTIVATE,
	A_RELOAD,
};

int change_set(struct lib_context *lc, enum activate_type what, void *rs);

#endif

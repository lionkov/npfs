/*
 * Copyright (C) 2006 by Latchesar Ionkov <lucho@ionkov.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <assert.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

Npcfsys *
npc_rdmamount(struct addrinfo *addrlist, int dotu, Npuser *user, u32 msize, int dfltport, 
	int (*auth)(Npcfid *afid, Npuser *user, void *aux), void *aux)
{
	struct rdma_cm_id *cmid;
	Nptrans *trans;
//	pthread_t proc;

        if (!addrlist)
                goto error;

	if (rdma_create_id(NULL, &cmid, NULL, RDMA_PS_TCP) < 0)
		return NULL;

	if (rdma_resolve_addr(cmid, NULL, addrlist->ai_addr, 30000) < 0) {
error:
		np_uerror(errno);
		rdma_destroy_id(cmid);
		return NULL;
	}

	if (rdma_resolve_route(cmid, 30000) < 0)
		goto error;

	trans = np_rdmatrans_create(cmid, 16, msize, 0);
	if (!trans)
		goto error;

	return npc_mount(trans, NULL, dotu, user, msize, auth, aux);
}


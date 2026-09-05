/*
 * Copyright (C) 2025 by Latchesar Ionkov <lucho@ionkov.net>
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
 * LATCHESAR IONKOV AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* Start the 9P2000.L server.
 * If port is positive, it will be a TCP server listening on that port.
 * Otherwise, a pipe server should be added with ufs_add_conn.
 */
Npsrv *ufs_start(char *rootdir, int debuglevel, int nwthreads, int sameuser, int msize);

/* Create a new connection, messages can be written to wfd and read from rfd */
Npconn *ufs_connect(Npsrv *srv, int *rfd, int *wfd);

/* Create a checkpoint of the connection state.
 * Returns the size of the allocated data and a pointer to the buffer
 * that contains it.
 */
int ufs_checkpoint(Npconn *conn, void** buf);

/* Restore the state of the connection from the provided buffer.
 * Returns -1 if there is an error and what errors occured.
 */
int ufs_restore(Npconn *conn, void *buf, int sz, char *err, int errsz);

/* buf.c - Bluetooth buffer management */

/*
 * Copyright (c) 2015 Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1) Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2) Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3) Neither the name of Intel Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <nanokernel.h>
#include <toolchain.h>
#include <errno.h>
#include <stddef.h>

#include <bluetooth/hci.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/buf.h>

#include "hci_core.h"

/* Total number of all types of buffers */
#define NUM_BUFS		20
static struct bt_buf		buffers[NUM_BUFS];

/* Available (free) buffers queues */
static struct nano_fifo		avail_hci;
static struct nano_fifo		avail_acl_in;
static struct nano_fifo		avail_acl_out;

static struct nano_fifo *get_avail(enum bt_buf_type type)
{
	switch (type) {
	case BT_CMD:
	case BT_EVT:
		return &avail_hci;
	case BT_ACL_IN:
		return &avail_acl_in;
	case BT_ACL_OUT:
		return &avail_acl_out;
	default:
		return NULL;
	}
}

struct bt_buf *bt_buf_get(enum bt_buf_type type, size_t reserve_head)
{
	struct nano_fifo *avail = get_avail(type);
	struct bt_buf *buf;

	buf = nano_fifo_get(avail);
	if (!buf) {
		BT_ERR("Failed to get free buffer\n");
		return NULL;
	}

	memset(buf, 0, sizeof(*buf));

	buf->type = type;
	buf->data = buf->buf + reserve_head;

	BT_DBG("buf %p type %d reserve %u\n", buf, buf->type, reserve_head);

	return buf;
}

void bt_buf_put(struct bt_buf *buf)
{
	struct nano_fifo *avail = get_avail(buf->type);

	BT_DBG("buf %p\n", buf);

	nano_fifo_put(avail, buf);
}

uint8_t *bt_buf_add(struct bt_buf *buf, size_t len)
{
	uint8_t *tail = buf->data + buf->len;
	buf->len += len;
	return tail;
}

uint8_t *bt_buf_push(struct bt_buf *buf, size_t len)
{
	buf->data -= len;
	buf->len += len;
	return buf->data;
}

uint8_t *bt_buf_pull(struct bt_buf *buf, size_t len)
{
	buf->len -= len;
	return buf->data += len;
}

size_t bt_buf_headroom(struct bt_buf *buf)
{
	return buf->data - buf->buf;
}

size_t bt_buf_tailroom(struct bt_buf *buf)
{
	return BT_BUF_MAX_DATA - bt_buf_headroom(buf) - buf->len;
}

int bt_buf_init(int acl_in, int acl_out)
{
	int i;

	/* Check that we have enough buffers configured */
	if (acl_out + acl_in >= NUM_BUFS - 2) {
		BT_ERR("Too many ACL buffers requested\n");
		return -EINVAL;
	}

	BT_DBG("Available bufs: ACL in: %d, ACL out: %d, cmds/evts: %d\n",
	       acl_in, acl_out, NUM_BUFS - (acl_in + acl_out));

	nano_fifo_init(&avail_acl_in);
	for (i = 0; acl_in > 0; i++, acl_in--)
		nano_fifo_put(&avail_acl_in, &buffers[i]);

	nano_fifo_init(&avail_acl_out);
	for (; acl_out > 0; i++, acl_out--)
		nano_fifo_put(&avail_acl_out, &buffers[i]);

	nano_fifo_init(&avail_hci);
	for (; i < NUM_BUFS; i++)
		nano_fifo_put(&avail_hci, &buffers[i]);

	return 0;
}

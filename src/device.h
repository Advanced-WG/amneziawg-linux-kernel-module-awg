/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#ifndef _WG_DEVICE_H
#define _WG_DEVICE_H

#include "junk.h"
#include "noise.h"
#include "allowedips.h"
#include "peerlookup.h"
#include "cookie.h"
#include "magic_header.h"

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/ptr_ring.h>

struct wg_device;

#define AWG_ISPEC_COUNT 5
#define AWG_JC_MAX 128
#define AWG_JUNK_SIZE_MAX 1280

struct multicore_worker {
	void *ptr;
	struct work_struct work;
};

struct crypt_queue {
	struct ptr_ring ring;
	struct multicore_worker __percpu *worker;
	int last_cpu;
};

struct prev_queue {
	struct sk_buff *head, *tail, *peeked;
	struct { struct sk_buff *next, *prev; } empty; // Match first 2 members of struct sk_buff.
	atomic_t count;
};

/* The AWG parameters of a device. They are read on every packet, so they are
 * published with RCU: readers see either the old or the new set as a whole,
 * never a mix of the two while netlink changes them.
 */
struct awg_params {
	struct magic_header headers[4];
	u16 junk_size[4];
	u16 jc;
	u16 jmin;
	u16 jmax;
};

struct wg_device {
	struct net_device *dev;
	struct crypt_queue encrypt_queue, decrypt_queue, handshake_queue;
	struct sock __rcu *sock4, *sock6;
	struct net __rcu *creating_net;
	struct noise_static_identity static_identity;
	struct workqueue_struct *packet_crypt_wq,*handshake_receive_wq, *handshake_send_wq;
	struct cookie_checker cookie_checker;
	struct pubkey_hashtable *peer_hashtable;
	struct index_hashtable *index_hashtable;
	struct allowedips peer_allowedips;
	struct mutex device_update_lock, socket_update_lock;
	struct list_head device_list, peer_list;
	atomic_t handshake_queue_len;
	unsigned int num_peers, device_update_gen;
	u32 fwmark;
	u16 incoming_port;

	struct jp_spec ispecs[AWG_ISPEC_COUNT];
	/* awg points at one of awg_buf; the other one is only written by
	 * netlink (under device_update_lock) after a grace period.
	 */
	struct awg_params __rcu *awg;
	struct awg_params awg_buf[2];
	bool advanced_security;
};

int wg_device_init(void);
void wg_device_uninit(void);
void wg_awg_params_get(struct wg_device *wg, struct awg_params *p);
u16 wg_awg_junk_size(struct wg_device *wg, int idx);
int wg_awg_params_check(const struct wg_device *wg, const struct awg_params *p,
			char *const idesc[], struct jp_built built[]);
void wg_awg_params_set(struct wg_device *wg, const struct awg_params *p,
		       char *idesc[], struct jp_built built[]);

#endif /* _WG_DEVICE_H */

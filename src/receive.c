// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include "compat/compat.h"
#include "queueing.h"
#include "device.h"
#include "peer.h"
#include "timers.h"
#include "messages.h"
#include "cookie.h"
#include "socket.h"
#include "magic_header.h"

#ifdef COMPAT_CRYPTO_IS_ZINC
#include <linux/simd.h>
#endif
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <net/ip_tunnels.h>

/* Must be called with bh disabled. */
static void update_rx_stats(struct wg_peer *peer, size_t len)
{
	dev_sw_netstats_rx_add(peer->device->dev, len);
	peer->rx_bytes += len;
}

static const size_t awg_message_sizes[] = {
	[MSGIDX_HANDSHAKE_INIT] = MESSAGE_INITIATION_SIZE,
	[MSGIDX_HANDSHAKE_RESPONSE] = MESSAGE_RESPONSE_SIZE,
	[MSGIDX_HANDSHAKE_COOKIE] = MESSAGE_COOKIE_REPLY_SIZE,
	[MSGIDX_TRANSPORT] = MESSAGE_TRANSPORT_SIZE,
};

/* Does the packet carry the H<idx+1> header after a junk prefix of len junk? */
static bool awg_header_at(struct sk_buff *skb, struct wg_device *wg, int idx,
			  unsigned int junk)
{
	/* data + junk is unaligned for most S values */
	return mh_validate(get_unaligned((__le32 *)(skb->data + junk)),
			   &wg->headers[idx]);
}

/* Is the receiver index of a transport header at data one of our keypairs? */
static bool awg_known_keypair(struct wg_device *wg, const u8 *data)
{
	const struct message_data *msg = (const struct message_data *)data;
	struct index_hashtable_entry *entry;
	struct wg_peer *peer = NULL;

	entry = wg_index_hashtable_lookup(wg->index_hashtable,
					  INDEX_HASHTABLE_KEYPAIR,
					  get_unaligned(&msg->key_idx), &peer);
	wg_peer_put(peer);
	return entry != NULL;
}

static size_t awg_accept(struct sk_buff *skb, unsigned int junk, int idx,
			 u8 flags)
{
	skb_pull(skb, junk);
	PACKET_CB(skb)->msg_type = idx + 1;
	PACKET_CB(skb)->awg_flags = flags;
	return awg_message_sizes[idx];
}

/* Classifies an incoming packet and strips its junk prefix. Tried in order:
 * the device's own framing (S1-S4 prefix + H1-H4 header), then what peers
 * with fewer AWG features send: transport and cookie packets without the
 * S3/S4 prefix, and plain WireGuard. Stores the MESSAGE_* type and the
 * AWG_PACKET_* flags in PACKET_CB and returns the header length (0: drop).
 */
static size_t prepare_awg_message(struct sk_buff *skb, struct wg_device *wg)
{
	unsigned int s4 = wg->junk_size[MSGIDX_TRANSPORT];
	bool with_s4, without_s4, plain;
	u32 type;
	int idx;

	if (skb_is_nonlinear(skb) && unlikely(skb_linearize(skb))) {
		net_dbg_skb_ratelimited("%s: non-linear sk_buff from %pISpfsc could not be linearized, dropping packet\n",
								wg->dev->name, skb);
		return 0;
	}

	/* Every check below reads at least a message type. */
	if (unlikely(skb->len < sizeof(struct message_header)))
		return 0;

	for (idx = MSGIDX_HANDSHAKE_INIT; idx <= MSGIDX_HANDSHAKE_COOKIE; ++idx) {
		unsigned int junk = wg->junk_size[idx];

		if (skb->len == junk + awg_message_sizes[idx] &&
		    awg_header_at(skb, wg, idx, junk))
			return awg_accept(skb, junk, idx, 0);
	}

	/* Exact-size messages from simpler peers come before the transport
	 * check: a transport header is only recognised by H4 at offset S4,
	 * and in a handshake packet those bytes are random key material that
	 * a wide H4 range matches quite often.
	 */
	if (wg->junk_size[MSGIDX_HANDSHAKE_COOKIE] &&
	    skb->len == MESSAGE_COOKIE_REPLY_SIZE &&
	    awg_header_at(skb, wg, MSGIDX_HANDSHAKE_COOKIE, 0))
		return awg_accept(skb, 0, MSGIDX_HANDSHAKE_COOKIE, 0); /* no S3 */

	type = le32_to_cpu(SKB_TYPE_LE32(skb));
	plain = wg->advanced_security && type >= MESSAGE_HANDSHAKE_INITIATION &&
		type <= MESSAGE_DATA;
	if (plain && type != MESSAGE_DATA &&
	    skb->len == awg_message_sizes[type - 1])
		return awg_accept(skb, 0, type - 1, AWG_PACKET_PLAIN);

	with_s4 = skb->len >= s4 + MESSAGE_MINIMUM_LENGTH &&
		  awg_header_at(skb, wg, MSGIDX_TRANSPORT, s4);
	/* Without the S4 prefix: AWG peer without S3/S4, or plain WireGuard. */
	without_s4 = s4 && skb->len >= MESSAGE_MINIMUM_LENGTH &&
		     awg_header_at(skb, wg, MSGIDX_TRANSPORT, 0);
	plain = plain && type == MESSAGE_DATA && skb->len >= MESSAGE_MINIMUM_LENGTH;
	/* The bytes at offset S4 of a packet without the prefix are counter
	 * and ciphertext, which fall into a wide H4 range quite often. When
	 * both readings look valid, the receiver index decides.
	 */
	if (with_s4 && (without_s4 || plain))
		with_s4 = awg_known_keypair(wg, skb->data + s4);
	if (with_s4)
		return awg_accept(skb, s4, MSGIDX_TRANSPORT, s4 ? AWG_PACKET_S4 : 0);
	if (without_s4)
		return awg_accept(skb, 0, MSGIDX_TRANSPORT, 0);
	if (plain)
		return awg_accept(skb, 0, MSGIDX_TRANSPORT, AWG_PACKET_PLAIN);

	net_dbg_skb_ratelimited("%s: Unknown message from %pISpfsc encountered, packet dropped\n",
								wg->dev->name, skb);

	return 0;
}

static int prepare_skb_header(struct sk_buff *skb, struct wg_device *wg)
{
	size_t data_offset, data_len, header_len;
	struct udphdr *udp;

	if (unlikely(!wg_check_packet_protocol(skb) ||
		     skb_transport_header(skb) < skb->head ||
		     (skb_transport_header(skb) + sizeof(struct udphdr)) >
			     skb_tail_pointer(skb)))
		return -EINVAL; /* Bogus IP header */
	udp = udp_hdr(skb);
	data_offset = (u8 *)udp - skb->data;
	if (unlikely(data_offset > U16_MAX ||
		     data_offset + sizeof(struct udphdr) > skb->len))
		/* Packet has offset at impossible location or isn't big enough
		 * to have UDP fields.
		 */
		return -EINVAL;
	data_len = ntohs(udp->len);
	if (unlikely(data_len < sizeof(struct udphdr) ||
		     data_len > skb->len - data_offset))
		/* UDP packet is reporting too small of a size or lying about
		 * its size.
		 */
		return -EINVAL;
	data_len -= sizeof(struct udphdr);
	data_offset = (u8 *)udp + sizeof(struct udphdr) - skb->data;
	if (unlikely(!pskb_may_pull(skb,
				data_offset + sizeof(struct message_header)) ||
		     pskb_trim(skb, data_len + data_offset) < 0))
		return -EINVAL;
	skb_pull(skb, data_offset);
	if (unlikely(skb->len != data_len))
		/* Final len does not agree with calculated len */
		return -EINVAL;
	header_len = prepare_awg_message(skb, wg);
	if (unlikely(!header_len))
		return -EINVAL;
	__skb_push(skb, data_offset);
	if (unlikely(!pskb_may_pull(skb, data_offset + header_len)))
		return -EINVAL;
	__skb_pull(skb, data_offset);
	return 0;
}

static void wg_receive_handshake_packet(struct wg_device *wg,
					struct sk_buff *skb)
{
	enum cookie_mac_state mac_state;
	struct wg_peer *peer = NULL;
	/* This is global, so that our load calculation applies to the whole
	 * system. We don't care about races with it at all.
	 */
	static u64 last_under_load;
	bool packet_needs_cookie;
	bool under_load;

	if (PACKET_CB(skb)->msg_type == MESSAGE_HANDSHAKE_COOKIE) {
		net_dbg_skb_ratelimited("%s: Receiving cookie response from %pISpfsc\n",
					wg->dev->name, skb);
		wg_cookie_message_consume(
			(struct message_handshake_cookie *)skb->data, wg);
		return;
	}

	under_load = atomic_read(&wg->handshake_queue_len) >=
			MAX_QUEUED_INCOMING_HANDSHAKES / 8;
	if (under_load) {
		last_under_load = ktime_get_coarse_boottime_ns();
	} else if (last_under_load) {
		under_load = !wg_birthdate_has_expired(last_under_load, 1);
		if (!under_load)
			last_under_load = 0;
	}
	mac_state = wg_cookie_validate_packet(&wg->cookie_checker, skb,
					      under_load);
	if ((under_load && mac_state == VALID_MAC_WITH_COOKIE) ||
	    (!under_load && mac_state == VALID_MAC_BUT_NO_COOKIE)) {
		packet_needs_cookie = false;
	} else if (under_load && mac_state == VALID_MAC_BUT_NO_COOKIE) {
		packet_needs_cookie = true;
	} else {
		net_dbg_skb_ratelimited("%s: Invalid MAC of handshake, dropping packet from %pISpfsc\n",
					wg->dev->name, skb);
		return;
	}

	if (PACKET_CB(skb)->msg_type == MESSAGE_HANDSHAKE_INITIATION) {
		struct message_handshake_initiation *message =
			(struct message_handshake_initiation *)skb->data;

		if (packet_needs_cookie) {
			wg_packet_send_handshake_cookie(wg, skb,
							message->sender_index);
			return;
		}
		peer = wg_noise_handshake_consume_initiation(message, wg, skb);
		if (unlikely(!peer)) {
			net_dbg_skb_ratelimited("%s: Invalid handshake initiation from %pISpfsc\n",
						wg->dev->name, skb);
			return;
		}
		wg_socket_set_peer_endpoint_from_skb(peer, skb);
		net_dbg_ratelimited("%s: Receiving handshake initiation from peer %llu (%pISpfsc)\n",
				    wg->dev->name, peer->internal_id,
				    &peer->endpoint.addr);
		wg_packet_send_handshake_response(peer);
	}
	if (PACKET_CB(skb)->msg_type == MESSAGE_HANDSHAKE_RESPONSE) {
		struct message_handshake_response *message =
			(struct message_handshake_response *)skb->data;

		if (packet_needs_cookie) {
			wg_packet_send_handshake_cookie(wg, skb,
							message->sender_index);
			return;
		}
		peer = wg_noise_handshake_consume_response(message, wg);
		if (unlikely(!peer)) {
			net_dbg_skb_ratelimited("%s: Invalid handshake response from %pISpfsc\n",
						wg->dev->name, skb);
			return;
		}
		wg_socket_set_peer_endpoint_from_skb(peer, skb);
		net_dbg_ratelimited("%s: Receiving handshake response from peer %llu (%pISpfsc)\n",
				    wg->dev->name, peer->internal_id,
				    &peer->endpoint.addr);
		if (wg_noise_handshake_begin_session(&peer->handshake,
						     &peer->keypairs)) {
			wg_timers_session_derived(peer);
			wg_timers_handshake_complete(peer);
			/* Calling this function will either send any existing
			 * packets in the queue and not send a keepalive, which
			 * is the best case, Or, if there's nothing in the
			 * queue, it will send a keepalive, in order to give
			 * immediate confirmation of the session.
			 */
			wg_packet_send_keepalive(peer);
		}
	}

	if (unlikely(!peer)) {
		WARN(1, "Somehow a wrong type of packet wound up in the handshake queue!\n");
		return;
	}

	local_bh_disable();
	update_rx_stats(peer, skb->len);
	local_bh_enable();

	wg_timers_any_authenticated_packet_received(peer);
	wg_timers_any_authenticated_packet_traversal(peer);
	wg_peer_put(peer);
}

void wg_packet_handshake_receive_worker(struct work_struct *work)
{
	struct crypt_queue *queue = container_of(work, struct multicore_worker, work)->ptr;
	struct wg_device *wg = container_of(queue, struct wg_device, handshake_queue);
	struct sk_buff *skb;

	while ((skb = ptr_ring_consume_bh(&queue->ring)) != NULL) {
		wg_receive_handshake_packet(wg, skb);
		dev_kfree_skb(skb);
		atomic_dec(&wg->handshake_queue_len);
		cond_resched();
	}
}

static void keep_key_fresh(struct wg_peer *peer)
{
	struct noise_keypair *keypair;
	bool send;

	if (peer->sent_lastminute_handshake)
		return;

	rcu_read_lock_bh();
	keypair = rcu_dereference_bh(peer->keypairs.current_keypair);
	send = keypair && READ_ONCE(keypair->sending.is_valid) &&
	       keypair->i_am_the_initiator &&
	       wg_birthdate_has_expired(keypair->sending.birthdate,
			REJECT_AFTER_TIME - KEEPALIVE_TIMEOUT - REKEY_TIMEOUT);
	rcu_read_unlock_bh();

	if (unlikely(send)) {
		peer->sent_lastminute_handshake = true;
		wg_packet_send_queued_handshake_initiation(peer, false);
	}
}

static bool decrypt_packet(struct sk_buff *skb, struct noise_keypair *keypair
			   COMPAT_MAYBE_SIMD_CONTEXT(simd_context_t *simd_context))
{
	struct scatterlist sg[MAX_SKB_FRAGS + 8];
	struct sk_buff *trailer;
	unsigned int offset;
	int num_frags;

	if (unlikely(!keypair))
		return false;

	if (unlikely(!READ_ONCE(keypair->receiving.is_valid) ||
		  wg_birthdate_has_expired(keypair->receiving.birthdate, REJECT_AFTER_TIME) ||
		  READ_ONCE(keypair->receiving_counter.counter) >= REJECT_AFTER_MESSAGES)) {
		WRITE_ONCE(keypair->receiving.is_valid, false);
		return false;
	}

	PACKET_CB(skb)->nonce =
		le64_to_cpu(((struct message_data *)skb->data)->counter);

	/* We ensure that the network header is part of the packet before we
	 * call skb_cow_data, so that there's no chance that data is removed
	 * from the skb, so that later we can extract the original endpoint.
	 */
	offset = -skb_network_offset(skb);
	skb_push(skb, offset);
	num_frags = skb_cow_data(skb, 0, &trailer);
	offset += sizeof(struct message_data);
	skb_pull(skb, offset);
	if (unlikely(num_frags < 0 || num_frags > ARRAY_SIZE(sg)))
		return false;

	sg_init_table(sg, num_frags);
	if (skb_to_sgvec(skb, sg, 0, skb->len) <= 0)
		return false;

	if (!chacha20poly1305_decrypt_sg_inplace(sg, skb->len, NULL, 0,
					         PACKET_CB(skb)->nonce,
						 keypair->receiving.key
					 COMPAT_MAYBE_SIMD_CONTEXT(simd_context)))
		return false;

	/* Another ugly situation of pushing and pulling the header so as to
	 * keep endpoint information intact.
	 */
	skb_push(skb, offset);
	if (pskb_trim(skb, skb->len - noise_encrypted_len(0)))
		return false;
	skb_pull(skb, offset);

	return true;
}

/* This is RFC6479, a replay detection bitmap algorithm that avoids bitshifts */
static bool counter_validate(struct noise_replay_counter *counter, u64 their_counter)
{
	unsigned long index, index_current, top, i;
	bool ret = false;

	spin_lock_bh(&counter->lock);

	if (unlikely(counter->counter >= REJECT_AFTER_MESSAGES + 1 ||
		     their_counter >= REJECT_AFTER_MESSAGES))
		goto out;

	++their_counter;

	if (unlikely((COUNTER_WINDOW_SIZE + their_counter) <
		     counter->counter))
		goto out;

	index = their_counter >> ilog2(BITS_PER_LONG);

	if (likely(their_counter > counter->counter)) {
		index_current = counter->counter >> ilog2(BITS_PER_LONG);
		top = min_t(unsigned long, index - index_current,
			    COUNTER_BITS_TOTAL / BITS_PER_LONG);
		for (i = 1; i <= top; ++i)
			counter->backtrack[(i + index_current) &
				((COUNTER_BITS_TOTAL / BITS_PER_LONG) - 1)] = 0;
		WRITE_ONCE(counter->counter, their_counter);
	}

	index &= (COUNTER_BITS_TOTAL / BITS_PER_LONG) - 1;
	ret = !test_and_set_bit(their_counter & (BITS_PER_LONG - 1),
				&counter->backtrack[index]);

out:
	spin_unlock_bh(&counter->lock);
	return ret;
}

#include "selftest/counter.c"

static void wg_packet_consume_data_done(struct wg_peer *peer,
					struct sk_buff *skb,
					struct endpoint *endpoint)
{
	struct net_device *dev = peer->device->dev;
	unsigned int len, len_before_trim;
	struct wg_peer *routed_peer;

	wg_socket_set_peer_endpoint(peer, endpoint);

	if (unlikely(wg_noise_received_with_keypair(&peer->keypairs,
						    PACKET_CB(skb)->keypair))) {
		wg_timers_handshake_complete(peer);
		wg_packet_send_staged_packets(peer);
	}

	keep_key_fresh(peer);

	wg_timers_any_authenticated_packet_received(peer);
	wg_timers_any_authenticated_packet_traversal(peer);

	/* A packet with length 0 is a keepalive packet */
	if (unlikely(!skb->len)) {
		update_rx_stats(peer, message_data_len(0));
		net_dbg_ratelimited("%s: Receiving keepalive packet from peer %llu (%pISpfsc)\n",
				    dev->name, peer->internal_id,
				    &peer->endpoint.addr);
		goto packet_processed;
	}

	wg_timers_data_received(peer);

	if (unlikely(skb_network_header(skb) < skb->head))
		goto dishonest_packet_size;
	if (unlikely(!(pskb_network_may_pull(skb, sizeof(struct iphdr)) &&
		       (ip_hdr(skb)->version == 4 ||
			(ip_hdr(skb)->version == 6 &&
			 pskb_network_may_pull(skb, sizeof(struct ipv6hdr)))))))
		goto dishonest_packet_type;

	skb->dev = dev;
	/* We've already verified the Poly1305 auth tag, which means this packet
	 * was not modified in transit. We can therefore tell the networking
	 * stack that all checksums of every layer of encapsulation have already
	 * been checked "by the hardware" and therefore is unnecessary to check
	 * again in software.
	 */
	skb->ip_summed = CHECKSUM_UNNECESSARY;
#ifndef COMPAT_CANNOT_USE_CSUM_LEVEL
	skb->csum_level = ~0; /* All levels */
#endif
	skb->protocol = ip_tunnel_parse_protocol(skb);
	if (skb->protocol == htons(ETH_P_IP)) {
		len = ntohs(ip_hdr(skb)->tot_len);
		if (unlikely(len < sizeof(struct iphdr)))
			goto dishonest_packet_size;
		INET_ECN_decapsulate(skb, PACKET_CB(skb)->ds, ip_hdr(skb)->tos);
	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		len = ntohs(ipv6_hdr(skb)->payload_len) +
		      sizeof(struct ipv6hdr);
		INET_ECN_decapsulate(skb, PACKET_CB(skb)->ds, ipv6_get_dsfield(ipv6_hdr(skb)));
	} else {
		goto dishonest_packet_type;
	}

	if (unlikely(len > skb->len))
		goto dishonest_packet_size;
	len_before_trim = skb->len;
	if (unlikely(pskb_trim(skb, len)))
		goto packet_processed;

	routed_peer = wg_allowedips_lookup_src(&peer->device->peer_allowedips,
					       skb);
	wg_peer_put(routed_peer); /* We don't need the extra reference. */

	if (unlikely(routed_peer != peer))
		goto dishonest_packet_peer;

	napi_gro_receive(&peer->napi, skb);
	update_rx_stats(peer, message_data_len(len_before_trim));
	return;

dishonest_packet_peer:
	net_dbg_skb_ratelimited("%s: Packet has unallowed src IP (%pISc) from peer %llu (%pISpfsc)\n",
				dev->name, skb, peer->internal_id,
				&peer->endpoint.addr);
	DEV_STATS_INC(dev, rx_errors);
	DEV_STATS_INC(dev, rx_frame_errors);
	goto packet_processed;
dishonest_packet_type:
	net_dbg_ratelimited("%s: Packet is neither ipv4 nor ipv6 from peer %llu (%pISpfsc)\n",
			    dev->name, peer->internal_id, &peer->endpoint.addr);
	DEV_STATS_INC(dev, rx_errors);
	DEV_STATS_INC(dev, rx_frame_errors);
	goto packet_processed;
dishonest_packet_size:
	net_dbg_ratelimited("%s: Packet has incorrect size from peer %llu (%pISpfsc)\n",
			    dev->name, peer->internal_id, &peer->endpoint.addr);
	DEV_STATS_INC(dev, rx_errors);
	DEV_STATS_INC(dev, rx_length_errors);
	goto packet_processed;
packet_processed:
	dev_kfree_skb(skb);
}

int wg_packet_rx_poll(struct napi_struct *napi, int budget)
{
	struct wg_peer *peer = container_of(napi, struct wg_peer, napi);
	struct noise_keypair *keypair;
	struct endpoint endpoint;
	enum packet_state state;
	struct sk_buff *skb;
	int work_done = 0;
	bool free;

	if (unlikely(budget <= 0))
		return 0;

	while ((skb = wg_prev_queue_peek(&peer->rx_queue)) != NULL &&
	       (state = atomic_read_acquire(&PACKET_CB(skb)->state)) !=
		       PACKET_STATE_UNCRYPTED) {
		wg_prev_queue_drop_peeked(&peer->rx_queue);
		keypair = PACKET_CB(skb)->keypair;
		free = true;

		if (unlikely(state != PACKET_STATE_CRYPTED))
			goto next;

		if (unlikely(!counter_validate(&keypair->receiving_counter,
					       PACKET_CB(skb)->nonce))) {
			net_dbg_ratelimited("%s: Packet has invalid nonce %llu (max %llu)\n",
					    peer->device->dev->name,
					    PACKET_CB(skb)->nonce,
					    READ_ONCE(keypair->receiving_counter.counter));
			goto next;
		}

		/* Learn whether the peer uses the S4 prefix, from authenticated
		 * packets only. Keepalives (empty after decryption) do not
		 * clear it: some implementations send them without the prefix.
		 */
		if (peer->advanced_security) {
			if (PACKET_CB(skb)->awg_flags & AWG_PACKET_S4)
				WRITE_ONCE(peer->no_s4, false);
			else if (skb->len && peer->device->junk_size[MSGIDX_TRANSPORT])
				WRITE_ONCE(peer->no_s4, true);
		}

		if (unlikely(wg_socket_endpoint_from_skb(&endpoint, skb)))
			goto next;

		wg_reset_packet(skb, false);
		wg_packet_consume_data_done(peer, skb, &endpoint);
		free = false;

next:
		wg_noise_keypair_put(keypair, false);
		wg_peer_put(peer);
		if (unlikely(free))
			dev_kfree_skb(skb);

		if (++work_done >= budget)
			break;
	}

	if (work_done < budget)
		napi_complete_done(napi, work_done);

	return work_done;
}

void wg_packet_decrypt_worker(struct work_struct *work)
{
	struct crypt_queue *queue = container_of(work, struct multicore_worker,
						 work)->ptr;
	struct sk_buff *skb;
#ifdef COMPAT_CRYPTO_IS_ZINC
	simd_context_t simd_context;
	simd_get(&simd_context);
#endif
	while ((skb = ptr_ring_consume_bh(&queue->ring)) != NULL) {
		enum packet_state state =
			likely(decrypt_packet(skb, PACKET_CB(skb)->keypair
					      COMPAT_MAYBE_SIMD_CONTEXT(&simd_context))) ?
				PACKET_STATE_CRYPTED : PACKET_STATE_DEAD;
		wg_queue_enqueue_per_peer_rx(skb, state);
#ifdef COMPAT_CRYPTO_IS_ZINC
		simd_relax(&simd_context);
#endif
		if (need_resched())
			cond_resched();
	}

#ifdef COMPAT_CRYPTO_IS_ZINC
	simd_put(&simd_context);
#endif
}

static void wg_packet_consume_data(struct wg_device *wg, struct sk_buff *skb)
{
	__le32 idx = ((struct message_data *)skb->data)->key_idx;
	struct wg_peer *peer = NULL;
	int ret;

	rcu_read_lock_bh();
	PACKET_CB(skb)->keypair =
		(struct noise_keypair *)wg_index_hashtable_lookup(
			wg->index_hashtable, INDEX_HASHTABLE_KEYPAIR, idx,
			&peer);
	if (unlikely(!wg_noise_keypair_get(PACKET_CB(skb)->keypair)))
		goto err_keypair;

	if (unlikely(READ_ONCE(peer->is_dead)))
		goto err;

	ret = wg_queue_enqueue_per_device_and_peer(&wg->decrypt_queue, &peer->rx_queue, skb,
						   wg->packet_crypt_wq);
	if (unlikely(ret == -EPIPE))
		wg_queue_enqueue_per_peer_rx(skb, PACKET_STATE_DEAD);
	if (likely(!ret || ret == -EPIPE)) {
		rcu_read_unlock_bh();
		return;
	}
err:
	wg_noise_keypair_put(PACKET_CB(skb)->keypair, false);
err_keypair:
	rcu_read_unlock_bh();
	wg_peer_put(peer);
	dev_kfree_skb(skb);
}

void wg_packet_receive(struct wg_device *wg, struct sk_buff *skb)
{
	if (unlikely(prepare_skb_header(skb, wg) < 0))
		goto err;

	if (PACKET_CB(skb)->msg_type != MESSAGE_DATA) {
		int cpu, ret = -EBUSY;

		if (unlikely(!rng_is_initialized()))
			goto drop;
		if (atomic_read(&wg->handshake_queue_len) > MAX_QUEUED_INCOMING_HANDSHAKES / 2) {
			if (spin_trylock_bh(&wg->handshake_queue.ring.producer_lock)) {
				ret = __ptr_ring_produce(&wg->handshake_queue.ring, skb);
				spin_unlock_bh(&wg->handshake_queue.ring.producer_lock);
			}
		} else
			ret = ptr_ring_produce_bh(&wg->handshake_queue.ring, skb);
		if (ret) {
	drop:
			net_dbg_skb_ratelimited("%s: Dropping handshake packet from %pISpfsc\n",
						wg->dev->name, skb);
			goto err;
		}
		atomic_inc(&wg->handshake_queue_len);
		cpu = wg_cpumask_next_online(&wg->handshake_queue.last_cpu);
		/* Queues up a call to packet_process_queued_handshake_packets(skb): */
		queue_work_on(cpu, wg->handshake_receive_wq,
			      &per_cpu_ptr(wg->handshake_queue.worker, cpu)->work);
	} else {
		PACKET_CB(skb)->ds = ip_tunnel_get_dsfield(ip_hdr(skb), skb);
		wg_packet_consume_data(wg, skb);
	}
	return;

err:
	dev_kfree_skb(skb);
}

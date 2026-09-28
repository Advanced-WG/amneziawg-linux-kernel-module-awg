# Changelog

All changes relative to upstream [amneziawg-linux-kernel-module](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module).

## Bug fixes

- **noise.c** — a replayed handshake initiation switched how the server frames packets for the peer: `advanced_security` (and `fixed_headers`) were set before the timestamp replay and flood checks. Replaying an old plain-WireGuard initiation of a client that now uses AWG made the server send it plain WireGuard packets, which the client accepts, so obfuscation was silently off until its next handshake. They are now set only for an accepted initiation (`tests/replay-framing.sh`)
- **netlink.c / device.c** — a rejected configuration stayed active: `wg_set_device` stored Jc/Jmin/Jmax/S1–S4/H1–H4/I1–I5 into the device first and validated afterwards, so e.g. `awg set awg0 jmin 2000` (with Jmax 1000) returned EINVAL but left Jmin > Jmax in place and the next handshake overflowed the junk buffer (same crash as upstream [#254](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/254) / [#225](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/225)). AWG parameters are now staged, validated as a whole (including I1–I5 parsing) and only then committed — and validated before the listen port or fwmark change, so a rejected request changes nothing
- **send.c** — junk packet sizes are drawn from locally read, ordered bounds, so a concurrent reconfiguration can never make the draw exceed the buffer; removed a label at the end of a compound statement (rejected by older compilers)
- **junk.c** — integer overflow in the I1–I5 size sum: `<r 2147483647><r 2147483647><b 0x0102>` wrapped to 0, `kzalloc(0)` returned `ZERO_SIZE_PTR` and the copy into it oopsed; every tag is now checked against the space left (≤ 65535 bytes in total), like upstream PR [#247](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/pull/247)
- **receive.c** — cookie reply returned wrong constant (`MESSAGE_HANDSHAKE_COOKIE` = 3 instead of `MESSAGE_COOKIE_REPLY_SIZE` = 64 bytes), causing malformed packets
- **netlink.c** — netlink dump overflow when device has many peers; implemented resumable state machine (upstream PR #152)
- **netlink.c** — race condition: `bogus_endpoints` module params could change via sysfs mid-dump, producing inconsistent output; now snapshotted once per dump in `dump_ctx`
- **netlink.c** — `nla_strdup()` OOM not checked; old descriptors leaked on reconfig
- **netlink.c** — `charp` module_param leaked memory on sysfs write; replaced with `module_param_string`
- **device.c** — dead code: `jc < 0` check on `u16` (always false)
- **device.c / junk.c** — use-after-free: `jp_spec_free` ran before workqueues were drained, causing page fault in `jp_spec_applymods` during teardown (upstream [#139](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/139)); moved after `destroy_workqueue` and added mutex locking in `jp_spec_free`
- **peer.c** — potential deadlock: added `cancel_work_sync()` before `flush_workqueue()` (upstream [#146](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/146))
- **send.c** — missing OOM check on `kzalloc` for junk packet buffer
- **crypto/zinc/chacha20poly1305.c** — upstream declares `simd_context_t ret` instead of `bool ret` (type mismatch)

## Mixed clients on one interface

An obfuscated interface serves, at the same time, plain WireGuard peers (upstream [#162](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/162)), AWG 1.0 peers configured with the H1–H4 range starts ([#163](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/163)) and AWG peers without S3/S4 ([#168](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/168)). Based on the ideas of upstream PRs #164, #165 and #170, with these differences:

- **receive.c** — packets are classified once in `prepare_awg_message()`; the message type and framing flags travel in `PACKET_CB`, so the dispatch no longer re-parses headers (a concurrent H1–H4 change could hit the old `WARN(1, "Non-exhaustive parsing…")`)
- **receive.c** — exact-size messages (plain WireGuard handshakes, cookie replies without S3) are checked before transport packets: in a handshake the bytes at offset S4 are random key material that a wide H4 range often matches, which silently dropped plain WireGuard handshakes
- **receive.c** — when a packet reads as a valid transport header both with and without the S4 prefix, the receiver index decides instead of taking the first match (~H4-range-width chance of a wrong guess per packet)
- **receive.c** — whether a peer uses S4 is learnt only from authenticated, non-keepalive packets (amneziawg-go sends keepalives without S4), so a forged or misparsed packet cannot switch it
- **noise.c / send.c** — plain framing is detected from how the initiation actually arrived, not from its type value, so interfaces with standard H1–H4 and custom S1/S2 also work with plain WireGuard peers; I1–I5, junk packets and S1–S4 are only sent to AWG peers
- **send.c** — AWG 1.0 peers (initiation header equal to the H1 range start) get the range starts for all headers
- **peer.c / device.c** — peers default to the device's obfuscation, also when it is switched on after the peers were added
- Nothing to configure: the per-peer state is detected automatically, so the per-peer netlink settings proposed in #165/#170 (not in upstream, even in 3.x) are not needed
- **tests/mixed-clients.sh** — one server and four kinds of clients in network namespaces
- **receive.c** — headers at the S1–S4 offset and the receiver index are read with `get_unaligned()`; packets shorter than a message header are dropped before any header is read
- **tests/replay-framing.sh** — a replayed old initiation must not change the framing of a peer

Known limit: a cookie reply (only sent under load) always uses S3, since the peer is unknown at that point.

## Userspace API

- **netlink.c / uapi** — `WGPEER_A_AWG_PEER_FLAGS` (NLA_U32, read-only) reports for every peer of an obfuscated device what it was detected to support: `AWG_PEER_F_FIXED_HEADERS` (AWG 1.0), `AWG_PEER_F_NO_S4`; with `WGPEER_A_ADVANCED_SECURITY` absent it marks a plain WireGuard peer. One attribute (number 12) so the divergence from upstream stays small; tools that do not know it ignore it

## Kernel compatibility

- **compat/compat.h** — RHEL 10.2 / 10.3 (CentOS Stream 10) backported `timer_container_of`, `netif_threaded_enable` and (10.3) `sockaddr_inet`; the compat definitions are skipped there (upstream PR [#174](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/pull/174), issue [#173](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/173))
- **compat/compat.h, socket.c** — Linux 7.1: `ipv6_stub` removed, `ip6_dst_lookup_flow()` is called directly
- **compat/compat.h, socket.c** — `setup_udp_tunnel_sock()` / `udp_tunnel_sock_release()` take a `struct sock *` since 7.1.5 and distro kernels backported it under older versions (Proxmox 7.0.14-17-pve, Ubuntu 26.04 7.0.0-38, CachyOS — upstream [#252](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/252), PR [#250](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/pull/250), PR [#218](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/pull/218)); the argument is chosen from the declared prototype at compile time instead of `LINUX_VERSION_CODE`, without function pointer casts
- **netlink.c** — Linux 7.2 removed `strncpy`; replaced with a bounded `memcpy` (upstream's `strscpy` breaks kernels < 4.3, [#251](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/251))
- **device.c** — Linux 7.2: per-CPU workqueues pass `WQ_PERCPU`
- **receive.c / send.c** — SIMD context only used with zinc crypto (upstream b52ea88)
- **compat/compat.h** — blake2s API changes in kernel 6.19+ (upstream issue #158); also fixed regression on kernels 5.4–5.10 with zinc crypto
- **compat/simd** — ARM NEON `kernel_neon_begin/end` signature change in kernel 6.19+
- **compat/Kbuild.include, compat.h** — did not build on current RHEL 8.10 (4.18.0-553), RHEL 9.8 (5.14.0-687) and CentOS Stream 10 (6.12.0-271, the next RHEL 10): those kernels backported `netif_napi_add_weight`, the `headers` struct group in `sk_buff`, `timer_delete`, `timer_container_of`, `netif_set_tso_max_size`, `dev_sw_netstats_rx_add`, `skb_queue_empty_lockless`, `NLA_POLICY_MASK`, `ktime_get_coarse_boottime_ns` and `struct rtnl_newlink_params`, so the compat copies clashed. These are now detected from the kernel headers instead of `LINUX_VERSION_CODE` and RHEL/Ubuntu exceptions
- **main.c** — kernels < 5.10 (zinc crypto) failed with implicit declarations of `chacha20_mod_init()` and the other zinc init functions; `crypto/zinc.h` is now included
- **compat/compat.h** — Ubuntu 18.04 (4.15.0-213) backported the in-kernel blake2s, `rng_is_initialized` and `le32_to_cpu_array`; it now uses the renamed zinc crypto like the 4.19/5.4 stable kernels

## Performance & code quality

- **netlink.c** — `bogus_endpoints` variables made `static`; module_param declarations moved next to definitions
- **netlink.c** — bogus endpoint prefix parsing done once per dump instead of once per peer
- **send.c** — `kzalloc` → `kmalloc` for junk buffer (immediately filled with `get_random_bytes`)
- **junk.c** — idiomatic `kstrtoint() < 0` instead of Yoda conditions
- **junk.c** — comments: `parse_b_tag` buffer mutation explained, `jp_spec_applymods` locking expectation documented
- **send.c** — per-ispec locking rationale documented
- **device.c** — `jmax` auto-increment when `jmin == jmax` documented

## Build & deployment

- **tests/compile-matrix.sh** — builds the module with `-Werror` against the kernel headers of AlmaLinux 8/9/10, CentOS Stream 10, Ubuntu 18.04–26.04 (including HWE kernels) and Debian 10–sid, each in a clean podman container with the distribution's own gcc (18 kernels, 4.15 to 7.2)
- **dkms.conf** — added `MAKE` and `CLEAN` directives (DKMS failed to rebuild on kernel update without them)
- **Makefile** — auto-versioning from git commit timestamp (`1.0.YYYYMMDD-HH.MM-awg`)


## Packaging

- **src/crypto/zinc** — the hand-written assembly files (`blake2s-x86_64.S`, `chacha20-mips.S`, `chacha20-unrolled-arm.S`, `curve25519-arm.S`, `poly1305-mips.S`) were missing from this fork because `.gitignore` covers `*.S`; kernels before 5.10 build the bundled zinc crypto and failed on x86_64, ARM and MIPS
- **debian/, amneziawg-dkms.spec** — packages for this fork: `dkms.conf`, the `/usr/src` directory and `dkms add` now use the same version (the version was not passed to the Makefile and `dh_dkms` read `0.0.0`), debhelper compat 13 with `dh-sequence-dkms`, the spec no longer requires git, rpm-build or python3-devel; `.gitattributes` keeps LF in Windows checkouts, since `dpkg-source` rejects `debian/` files with CRLF
- **src/Makefile** — a checkout of a release tag builds with that version (e.g. `1.0.20260927+awg`) instead of the commit time

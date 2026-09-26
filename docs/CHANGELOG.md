# Changelog

All changes relative to upstream [amneziawg-linux-kernel-module](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module).

## Bug fixes

- **netlink.c / device.c** — a rejected configuration stayed active: `wg_set_device` stored Jc/Jmin/Jmax/S1–S4/H1–H4/I1–I5 into the device first and validated afterwards, so e.g. `awg set awg0 jmin 2000` (with Jmax 1000) returned EINVAL but left Jmin > Jmax in place and the next handshake overflowed the junk buffer (same crash as upstream [#254](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/254) / [#225](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/225)). AWG parameters are now staged, validated as a whole (including I1–I5 parsing) and only then committed
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

## Kernel compatibility

- **compat/compat.h, socket.c** — Linux 7.1: `ipv6_stub` removed, `ip6_dst_lookup_flow()` is called directly
- **compat/compat.h, socket.c** — `setup_udp_tunnel_sock()` / `udp_tunnel_sock_release()` take a `struct sock *` since 7.1.5 and distro kernels backported it under older versions (Proxmox 7.0.14-17-pve, Ubuntu 26.04 7.0.0-38, CachyOS — upstream [#252](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/252), PR [#250](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/pull/250), PR [#218](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/pull/218)); the argument is chosen from the declared prototype at compile time instead of `LINUX_VERSION_CODE`, without function pointer casts
- **netlink.c** — Linux 7.2 removed `strncpy`; replaced with a bounded `memcpy` (upstream's `strscpy` breaks kernels < 4.3, [#251](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/251))
- **device.c** — Linux 7.2: per-CPU workqueues pass `WQ_PERCPU`
- **receive.c / send.c** — SIMD context only used with zinc crypto (upstream b52ea88)
- **compat/compat.h** — blake2s API changes in kernel 6.19+ (upstream issue #158); also fixed regression on kernels 5.4–5.10 with zinc crypto
- **compat/simd** — ARM NEON `kernel_neon_begin/end` signature change in kernel 6.19+

## Performance & code quality

- **netlink.c** — `bogus_endpoints` variables made `static`; module_param declarations moved next to definitions
- **netlink.c** — bogus endpoint prefix parsing done once per dump instead of once per peer
- **send.c** — `kzalloc` → `kmalloc` for junk buffer (immediately filled with `get_random_bytes`)
- **junk.c** — idiomatic `kstrtoint() < 0` instead of Yoda conditions
- **junk.c** — comments: `parse_b_tag` buffer mutation explained, `jp_spec_applymods` locking expectation documented
- **send.c** — per-ispec locking rationale documented
- **device.c** — `jmax` auto-increment when `jmin == jmax` documented

## Build & deployment

- **dkms.conf** — added `MAKE` and `CLEAN` directives (DKMS failed to rebuild on kernel update without them)
- **Makefile** — auto-versioning from git commit timestamp (`1.0.YYYYMMDD-HH.MM-awg`)

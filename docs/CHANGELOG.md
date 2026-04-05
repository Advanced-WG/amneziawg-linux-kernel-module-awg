# Changelog

All changes relative to upstream [amneziawg-linux-kernel-module](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module).

## Bug fixes

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

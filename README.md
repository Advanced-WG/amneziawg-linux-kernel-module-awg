# AmneziaWG kernel module (patched fork)

A patched fork of [amneziawg-linux-kernel-module](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module) (AmneziaWG 2.0) with crash and validation fixes, one interface for plain WireGuard, AWG 1.0 and AWG 2.0 clients, and kernel compatibility from Linux 5.4 up to 7.2 and RHEL 10.

## What is different from upstream

- **No crashes from bad parameters.** A rejected `awg set` used to leave the invalid values active, and the next handshake could overflow the junk buffer (upstream [#254](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/254), [#225](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/225)). Parameters are now validated as a whole and only then applied. The I1–I5 size overflow and several use-after-free, leak and race bugs are fixed too.
- **Mixed clients on one interface.** An obfuscated interface also serves plain WireGuard peers, AWG 1.0 peers and AWG peers without S3/S4 ([#162](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/162), [#163](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/163), [#168](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module/issues/168)). This is detected per peer, with no client settings needed. What was detected is reported over netlink (`WGPEER_A_AWG_PEER_FLAGS`), and tools that do not know the attribute skip it.
- **Newer kernels.** Linux 6.19–7.2 API changes, the `udp_tunnel` prototype backported into Ubuntu, Proxmox and CachyOS kernels, and RHEL / AlmaLinux / Rocky 10.2–10.3.
- **Netlink with many peers.** Dumps no longer overflow, and the cookie reply size is correct.

The full list with upstream references is in the [changelog](docs/CHANGELOG.md).

## Compatibility

The module speaks the **AmneziaWG 2.0** protocol, the same as upstream before 3.0. Clients and servers running AmneziaWG 2.0, 1.0 or plain WireGuard can connect. AmneziaWG 3.x is not supported yet.

Use `amneziawg-tools` up to [v1.0.20260618-2](https://github.com/amnezia-vpn/amneziawg-tools/releases/tag/v1.0.20260618-2) (`awg`, `awg-quick`); the 3.x tools use the new protocol.

Tested on:

| Distribution | Kernels | Notes |
|---|---|---|
| Debian 13 | 6.12, 7.2 | |
| Ubuntu 26.04 | 7.0 | |
| AlmaLinux 10.2 | 6.12.0 (el10_2) | Secure Boot, SELinux enforcing |

## Quick start

Debian / Ubuntu (other distributions: [Installation](docs/INSTALL.md)):

```shell
sudo apt install -y build-essential dkms linux-headers-$(uname -r)

git clone https://github.com/Advanced-WG/amneziawg-linux-kernel-module-awg.git
cd amneziawg-linux-kernel-module-awg/src
sudo make dkms-install
sudo dkms install "amneziawg/$(make print-version)"

sudo modprobe amneziawg
```

To have DKMS rebuild the module after kernel updates, also install the headers meta-package (`linux-headers-amd64` on Debian, `linux-headers-generic` on Ubuntu) — see [Installation](docs/INSTALL.md#1-install-prerequisites).

## Documentation

| Document | Description |
|---|---|
| **[Installation](docs/INSTALL.md)** | Prerequisites and build instructions for all supported distros, Secure Boot |
| **[Configuration](docs/CONFIGURATION.md)** | AWG parameters (Jc, Jmin, Jmax, S1–S4, H1–H4, I1–I5), limits, mixed clients |
| **[Troubleshooting](docs/TROUBLESHOOTING.md)** | Debug logging, rejected parameters, DKMS rebuild, updating |
| **[Changelog](docs/CHANGELOG.md)** | All changes from upstream |

## Companion library

[**awgctrl-go**](https://github.com/Advanced-WG/awgctrl-go) is a Go library for WireGuard and AmneziaWG devices, and works with this module:

| | wgctrl-go | awgctrl-go |
|---|---|---|
| Standard WireGuard | ✅ | ✅ |
| AWG write params | ❌ | ✅ |
| AWG **read** params | ❌ | ✅ |
| Per-peer AdvancedSecurity | ❌ | ✅ |
| Detected peer type (plain WG / AWG 1.0 / no S4) | ❌ | ✅ (v1.2.0+) |
| Auto-generate AWG params | ❌ | ✅ |
| Validate AWG params (kernel limits) | ❌ | ✅ |
| Userspace AWG daemon | ❌ | ✅ |
| `context.Context` API | ❌ | ✅ |
| Single netlink round-trip | ❌ | ✅ |

```go
// Example: enable AWG obfuscation with auto-generated parameters
client, _ := wgctrl.New()
defer client.Close()

cfg := &wgtypes.Config{}
cfg.GenerateAmneziaParams()
if err := cfg.Validate(); err != nil {
    log.Fatal(err)
}
client.ConfigureDevice(context.Background(), "awg0", *cfg)
```

## License

GPL-2.0 — see [COPYING](COPYING).

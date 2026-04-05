# AmneziaWG kernel module (patched fork)

A patched fork of [amneziawg-linux-kernel-module](https://github.com/amnezia-vpn/amneziawg-linux-kernel-module) with bug fixes, performance improvements, and kernel compatibility patches for Linux 5.4 through 6.19+.

Fully compatible with [awgctrl-go](https://github.com/Advanced-WG/awgctrl-go) — a Go library for controlling AmneziaWG devices that adds read/write of all obfuscation parameters, parameter validation and auto-generation, per-peer advanced security, `context.Context` API, and single netlink round-trip configuration.

## Quick start

```shell
# Install prerequisites (Debian/Ubuntu example, see docs/INSTALL.md for other distros)
sudo apt install -y build-essential dkms linux-headers-$(uname -r) linux-headers-generic

# Clone and install
git clone https://github.com/Advanced-WG/amneziawg-linux-kernel-module-awg.git
cd amneziawg-linux-kernel-module-awg/src
sudo make dkms-install
sudo dkms install "amneziawg/$(make print-version)"

# Load
sudo modprobe amneziawg
```

## Documentation

| Document | Description |
|---|---|
| **[Installation](docs/INSTALL.md)** | Prerequisites and build instructions for all supported distros |
| **[Configuration](docs/CONFIGURATION.md)** | AWG parameter reference (Jc, Jmin, Jmax, S1–S4, H1–H4) |
| **[Troubleshooting](docs/TROUBLESHOOTING.md)** | Debug logging, DKMS rebuild, updating, common issues |
| **[Changelog](docs/CHANGELOG.md)** | All changes from upstream |

## Companion library

[**awgctrl-go**](https://github.com/Advanced-WG/awgctrl-go) is designed to work with this kernel module and provides:

| | wgctrl-go | awgctrl-go |
|---|---|---|
| Standard WireGuard | ✅ | ✅ |
| AWG write params | ❌ | ✅ |
| AWG **read** params | ❌ | ✅ |
| Per-peer AdvancedSecurity | ❌ | ✅ |
| Auto-generate AWG params | ❌ | ✅ |
| Validate AWG params | ❌ | ✅ |
| Userspace AWG daemon | ❌ | ✅ |
| `context.Context` API | ❌ | ✅ |
| Single netlink round-trip | ❌ | ✅ |

```go
// Example: enable AWG obfuscation with auto-generated parameters
client, _ := wgctrl.New()
defer client.Close()

cfg := &wgtypes.Config{}
cfg.GenerateAmneziaParams()
client.ConfigureDevice(context.Background(), "awg0", *cfg)
```

## License

GPL-2.0 — see [COPYING](COPYING).

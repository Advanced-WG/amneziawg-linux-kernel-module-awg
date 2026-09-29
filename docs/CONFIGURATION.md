# Configuration

## AWG parameters

| Parameter | Kernel limit | Recommended | Description |
|---|---|---|---|
| **Jc** | 0 – 128 | 0 – 10 | Junk packets sent before each handshake initiation (0 = none) |
| **Jmin** | 0 – 1280, ≤ Jmax | 64 – 1024 | Minimum junk packet size (bytes) |
| **Jmax** | 0 – 1280, ≥ Jmin | 64 – 1024 | Maximum junk packet size (bytes); 0 turns junk packets off |
| **S1** | 0 – 65387 | 0 – 64 | Random prefix of handshake initiations (148 bytes) |
| **S2** | 0 – 65443 | 0 – 64 | Random prefix of handshake responses (92 bytes) |
| **S3** | 0 – 65471 | 0 – 64 | Random prefix of cookie replies (64 bytes) |
| **S4** | 0 – 65503 | 0 – 32 | Random prefix of every transport packet — costs MTU |
| **H1** | `N` or `N-M` | range, ≥ 5 | Header of handshake initiations |
| **H2** | `N` or `N-M` | range, ≥ 5 | Header of handshake responses |
| **H3** | `N` or `N-M` | range, ≥ 5 | Header of cookie replies |
| **H4** | `N` or `N-M` | range, ≥ 5 | Header of transport packets |
| **I1–I5** | tags, ≤ 65535 bytes | I1 only | Packets sent before a handshake initiation (see below) |

The kernel limits are what the module accepts: at most 128 junk packets of at most 1280 bytes, and S1–S4 plus the size of their message must fit in 65535 bytes. Values beyond the recommended ranges work but make packets large enough to be fragmented. Anything above about 1280 bytes in total may not pass every path.

**Checked by the module** (a violation fails `awg set` with `Invalid argument`, and the device keeps its previous values):
- Jc ≤ 128, Jmin and Jmax ≤ 1280.
- Jmin ≤ Jmax unless Jmax is 0. With Jmin = Jmax every junk packet has the same size.
- H1–H4 must not overlap. They are decimal 32-bit numbers, and a single value `N` means the range `N-N`.
- I1–I5 must parse (see below).

**Not checked, but keep them apart** (otherwise packet types can be confused by size):
- S1 + 56 ≠ S2 (initiation 148 + S1 vs. response 92 + S2)
- S3 + 64 differs from both of these (S3 ≠ S1 + 84, S3 ≠ S2 + 28)

H1–H4 values 1–4 are WireGuard's own message types. With H1–H4 = 1, 2, 3, 4 and all S values 0, the interface is plain WireGuard.

### Which values must match

- **S1–S4 and H1–H4** must be the same on the client and the server, except for the mixed clients described below.
- **Jc, Jmin, Jmax and I1–I5** only affect what a side sends. The receiver drops these packets, so they may differ between the two sides.

### I1–I5

Each is a chain of tags, sent as one UDP packet before every handshake initiation:

| Tag | Content |
|---|---|
| `<b 0xHEX>` | fixed bytes (an even number of hex digits) |
| `<r N>` | N random bytes |
| `<rc N>` | N random letters (a–z, A–Z) |
| `<rd N>` | N random digits |
| `<c>` | packet counter, 4 bytes |
| `<t>` | Unix time, 4 bytes |

Example: `I1 = <b 0xc0ff><r 32><t>`. Text outside `<…>` is ignored. An unknown tag or a size of 0 is rejected, and the whole packet may be at most 65535 bytes.

## Mixed clients

An interface with obfuscation also accepts clients that use less of it, detected per peer from the packets they send:

| Client | Configuration of the client |
|---|---|
| Full AWG 2.0 | the server's S1–S4 and H1–H4 |
| AWG 2.0 without S3/S4 | S3 = S4 = 0, the rest as the server |
| AWG 1.0 | single H values = the **start** of each server range, no S3/S4 |
| Plain WireGuard | no AWG settings |

The server answers every peer in the form it was contacted with. A cookie reply, which is only sent under load before the peer is known, always uses S3.

What was detected is reported per peer over netlink in `WGPEER_A_AWG_PEER_FLAGS`: `AWG_PEER_F_FIXED_HEADERS` for AWG 1.0 and `AWG_PEER_F_NO_S4`. When `WGPEER_A_ADVANCED_SECURITY` is absent, the peer is plain WireGuard. `awg show` does not display this; awgctrl-go v1.2.0+ exposes it as `Peer.AWGPeerFlagsKnown`, `FixedHeaders` and `NoS4`.

## Using awgctrl-go

The [awgctrl-go](https://github.com/Advanced-WG/awgctrl-go) library generates parameters within the recommended ranges, and checks them against the kernel limits before sending them:

```go
cfg := &wgtypes.Config{}
cfg.GenerateAmneziaParams()  // Jc, Jmin, Jmax, S1–S4, H1–H4 within the recommended ranges
if err := cfg.Validate(); err != nil {
    log.Fatal(err)           // what the kernel would reject, with the reason
}
```

See the [AWG Parameter Reference](https://github.com/Advanced-WG/awgctrl-go/blob/main/docs/AWG_PARAMETERS.md) in awgctrl-go for more.

# Configuration

## AWG parameter reference

> [!IMPORTANT]
> All parameters must match between client and server, **except** Jc, Jmin, and Jmax — these may differ per side.

| Parameter | Range | Recommended | Description |
|---|---|---|---|
| **Jc** | 1 – 128 | 4 – 12 | Number of junk packets sent before handshake initiation |
| **Jmin** | < Jmax, < 1280* | 8 | Minimum junk packet size (bytes) |
| **Jmax** | > Jmin, ≤ 1280* | 80 | Maximum junk packet size (bytes) |
| **S1** | ≤ 1132* | 15 – 150 | Junk prefix size for handshake initiation packets |
| **S2** | ≤ 1188* | 15 – 150 | Junk prefix size for handshake response packets |
| **S3** | ≤ 1216* | 15 – 150 | Junk prefix size for cookie reply packets |
| **S4** | ≤ 1200* | 15 – 150 | Junk prefix size for transport data packets |
| **H1** | unique | 5 – 2147483647 | Magic header for handshake initiation |
| **H2** | unique | 5 – 2147483647 | Magic header for handshake response |
| **H3** | unique | 5 – 2147483647 | Magic header for cookie reply |
| **H4** | unique | 5 – 2147483647 | Magic header for transport data |

`* Assuming a basic internet connection with an MTU of 1280.`

**Constraints:**
- S1 + 56 ≠ S2 (initiation and response must not be confused)
- H1, H2, H3, H4 must be unique among each other (ranges must not overlap)

## Using awgctrl-go

The [awgctrl-go](https://github.com/Advanced-WG/awgctrl-go) library can auto-generate valid parameters:

```go
cfg := &wgtypes.Config{}
cfg.GenerateAmneziaParams()  // fills Jc, Jmin, Jmax, S1-S4, H1-H4 with safe values
cfg.Validate()               // returns error if any constraint is violated
```

See the [AWG Parameter Reference](https://github.com/Advanced-WG/awgctrl-go/blob/main/docs/AWG_PARAMETERS.md) in awgctrl-go for full details.

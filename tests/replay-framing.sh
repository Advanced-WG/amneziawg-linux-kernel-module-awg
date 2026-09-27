#!/bin/bash
# A replayed handshake initiation must not change how the server frames
# packets for the peer.
#
# c1 first connects as plain WireGuard, then with the same key as AWG 2.0.
# Its old plain initiation, sent again afterwards, carries an old timestamp
# and is rejected. It must not switch the server back to plain framing for
# the peer: the client would still accept those packets, so obfuscation
# would silently be off.
umask 077
S4=8
H="h1 1000-2000 h2 3000-4000 h3 5000-6000 h4 100000000-2000000000"
J="jc 3 jmin 40 jmax 90 s1 30 s2 45"
cleanup() { ip netns del srv 2>/dev/null; ip netns del c1 2>/dev/null; rm -f /tmp/rp-*; }
cleanup

ip netns add srv; ip netns add c1
ip link add s1 netns srv type veth peer name v1 netns c1
ip -n srv addr add 192.168.101.1/24 dev s1; ip -n srv link set s1 up
ip -n c1 addr add 192.168.101.2/24 dev v1; ip -n c1 link set v1 up
awg genkey > /tmp/rp-ck; awg pubkey < /tmp/rp-ck > /tmp/rp-cp
awg genkey > /tmp/rp-sk; awg pubkey < /tmp/rp-sk > /tmp/rp-sp

ip -n srv link add awgs type amneziawg
ip netns exec srv awg set awgs private-key <(cat /tmp/rp-sk) listen-port 51000 $J s3 12 s4 $S4 $H \
	peer $(cat /tmp/rp-cp) allowed-ips 10.50.0.11/32
ip -n srv addr add 10.50.0.1/24 dev awgs; ip -n srv link set awgs up

client() { # type tool params...
	local type=$1 tool=$2; shift 2
	ip -n c1 link del t1 2>/dev/null
	ip -n c1 link add t1 type $type
	ip netns exec c1 $tool set t1 private-key <(cat /tmp/rp-ck) "$@" \
		peer $(cat /tmp/rp-sp) allowed-ips 10.50.0.0/24 endpoint 192.168.101.1:51000
	ip -n c1 addr add 10.50.0.11/24 dev t1; ip -n c1 link set t1 up
}

# UDP payloads of a pcap (Ethernet/IPv4), one hex string per line.
payloads() {
	python3 - "$1" <<'EOF'
import struct, sys
d = open(sys.argv[1], 'rb').read()
off = 24
while off + 16 <= len(d):
    incl = struct.unpack('<I', d[off + 8:off + 12])[0]
    pkt = d[off + 16:off + 16 + incl]
    off += 16 + incl
    ihl = (pkt[14] & 15) * 4
    print(pkt[14 + ihl + 8:].hex())
EOF
}

capture() { # file filter -- command...
	local f=$1 filter=$2; shift 3
	ip netns exec srv tcpdump -i s1 -U -w "$f" "$filter" 2>/dev/null & local p=$!
	sleep 1; "$@" >/dev/null; sleep 0.5; kill $p; wait $p 2>/dev/null
}

# 1. Plain WireGuard handshake; keep the initiation (148 bytes, type 1).
client wireguard wg
capture /tmp/rp-plain.pcap "udp dst port 51000" -- ip netns exec c1 ping -c 3 -W 1 10.50.0.1
init=$(payloads /tmp/rp-plain.pcap | grep -m1 -x '01000000[0-9a-f]\{288\}')
[ "$init" ] || { echo "FAIL: no plain initiation captured"; cleanup; exit 1; }

# 2. Same key as AWG 2.0: the server now uses AWG framing for the peer.
client amneziawg awg $J s3 12 s4 $S4 $H
ip netns exec c1 ping -q -c 3 -W 1 10.50.0.1 >/dev/null

# 3. Replay the old initiation, then look at what the server sends.
sleep 1
ip netns exec c1 python3 -c "import socket,sys; socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(bytes.fromhex(sys.argv[1]), ('192.168.101.1', 51000))" "$init"
sleep 0.5
capture /tmp/rp-after.pcap "udp src port 51000" -- ip netns exec srv ping -c 20 -i 0.05 -W 1 10.50.0.11
total=$(payloads /tmp/rp-after.pcap | wc -l)
plain=$(payloads /tmp/rp-after.pcap | grep -c '^04000000')
if [ "$total" -gt 0 ] && [ "$plain" -eq 0 ]; then
	echo "replayed plain initiation: server kept AWG framing ($total packets)"
else
	echo "FAIL: $plain of $total packets to the client in plain WireGuard framing"
fi
dmesg | tail -50 | grep -iE "bug:|oops|call trace|general protection|WARNING:" || echo "dmesg clean"
[ "$KEEP" ] || cleanup

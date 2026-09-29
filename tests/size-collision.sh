#!/bin/bash
# Transport packets whose length equals a handshake length (S + message size)
# must not be taken for handshakes. With S1 = 28 an initiation is 176 bytes,
# like a transport packet with 144 bytes of padded payload; a wide H1 range
# then matched the ciphertext at offset 28 about half of the time and the
# packet was dropped. Same for S2 = 52 (response, 144 bytes = 112 of payload).
# Second round with S4 = 8 and S1 = 36: 8 + 32 + 144 = 36 + 148.
# Keys go through <(cat ...): the AppArmor profile of wg on Ubuntu blocks /tmp.
umask 077
PINGS=${PINGS:-300}
cleanup() { for n in srv cli; do ip netns del $n 2>/dev/null; done; }
cleanup
ip netns add srv; ip netns add cli
ip link add s0 netns srv type veth peer name c0 netns cli
ip -n srv addr add 192.168.200.1/24 dev s0; ip -n srv link set s0 up
ip -n cli addr add 192.168.200.2/24 dev c0; ip -n cli link set c0 up
for k in sk ck; do awg genkey > /tmp/$k; awg pubkey < /tmp/$k > /tmp/${k}.pub; done

fail=0
round() { # S4 S1 S2 pings...
	local P="jc 0 s4 $1 s1 $2 s2 $3 h1 5-2000000000 h2 2000000001-2100000000 h3 2100000001-2200000000 h4 2200000001-4294967295"
	local s4=$1; shift 3
	ip -n srv link del awg0 2>/dev/null; ip -n cli link del awg0 2>/dev/null
	ip -n srv link add awg0 type amneziawg
	ip netns exec srv awg set awg0 private-key <(cat /tmp/sk) listen-port 51000 $P \
		peer $(cat /tmp/ck.pub) allowed-ips 10.60.0.2/32
	ip -n cli link add awg0 type amneziawg
	ip netns exec cli awg set awg0 private-key <(cat /tmp/ck) $P \
		peer $(cat /tmp/sk.pub) allowed-ips 10.60.0.0/24 endpoint 192.168.200.1:51000
	for n in srv cli; do ip -n $n link set awg0 mtu 1400 up; done
	ip -n srv addr add 10.60.0.1/24 dev awg0; ip -n cli addr add 10.60.0.2/24 dev awg0
	ip netns exec cli ping -q -c 3 -W 2 10.60.0.1 >/dev/null # handshake
	# payload -> IPv4 packet 28 + payload, padded to 16, + 32 bytes + S4
	for s in "$@"; do
		loss=$(ip netns exec cli ping -q -c $PINGS -i 0.005 -W 1 -s $s 10.60.0.1 | grep -o "[0-9.]*% packet loss")
		echo "S4 $s4, ping -s $s (transport $(( ( (28 + s + 15) / 16 ) * 16 + 32 + s4 )) bytes): $loss"
		[ "${loss%%%*}" = 0 ] || fail=1
	done
}
round 0 28 52 60 84 90 110
round 8 36 60 60 84 90 110
[ $fail = 0 ] && echo "PASS" || echo "FAIL: transport packets taken for handshakes"
dmesg | tail -50 | grep -iE "bug:|oops|call trace|general protection|WARNING:" || echo "dmesg clean"
rm -f /tmp/sk /tmp/ck /tmp/sk.pub /tmp/ck.pub
[ "$KEEP" ] || cleanup

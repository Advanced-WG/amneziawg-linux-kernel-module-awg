#!/bin/bash
# One obfuscated AWG server, four kinds of clients on the same interface:
#   c1 plain WireGuard (kernel wireguard module)
#   c2 AWG 1.0: single H values = server range starts, no S3/S4
#   c3 AWG 2.0 with ranges but without S3/S4
#   c4 AWG 2.0 identical to the server (S3/S4 on)
# H4 is huge on purpose so random bytes often look like a valid H4.
umask 077
PINGS=${PINGS:-200}
cleanup() { for n in srv c1 c2 c3 c4; do ip netns del $n 2>/dev/null; done; }
cleanup
ip netns add srv
for i in 1 2 3 4; do
	ip netns add c$i
	ip link add s$i netns srv type veth peer name v$i netns c$i
	ip -n srv addr add 192.168.10$i.1/24 dev s$i; ip -n srv link set s$i up
	ip -n c$i addr add 192.168.10$i.2/24 dev v$i; ip -n c$i link set v$i up
	awg genkey > /tmp/ck$i; awg pubkey < /tmp/ck$i > /tmp/cp$i
done
awg genkey > /tmp/sk; awg pubkey < /tmp/sk > /tmp/sp

H="h1 1000-2000 h2 3000-4000 h3 5000-6000 h4 100000000-2000000000"
J="jc 3 jmin 40 jmax 90 s1 30 s2 45"
ip -n srv link add awgs type amneziawg
ip netns exec srv awg set awgs private-key /tmp/sk listen-port 51000 $J s3 12 s4 8 $H \
	peer $(cat /tmp/cp1) allowed-ips 10.50.0.11/32 \
	peer $(cat /tmp/cp2) allowed-ips 10.50.0.12/32 \
	peer $(cat /tmp/cp3) allowed-ips 10.50.0.13/32 \
	peer $(cat /tmp/cp4) allowed-ips 10.50.0.14/32
ip -n srv addr add 10.50.0.1/24 dev awgs; ip -n srv link set awgs up

client() { # n type params...
	local n=$1 type=$2 tool=awg; shift 2
	[ $type = wireguard ] && tool=wg
	ip -n c$n link add t$n type $type
	ip netns exec c$n $tool set t$n private-key /tmp/ck$n "$@" \
		peer $(cat /tmp/sp) allowed-ips 10.50.0.0/24 endpoint 192.168.10$n.1:51000
	ip -n c$n addr add 10.50.0.1$n/24 dev t$n; ip -n c$n link set t$n up
}
client 1 wireguard
client 2 amneziawg $J h1 1000 h2 3000 h3 5000 h4 100000000
client 3 amneziawg $J $H
client 4 amneziawg $J s3 12 s4 8 $H
[ "$(ip -n c1 -d link show t1 | grep -o wireguard)" ] || echo "c1 is not a wireguard link!"

names=("" "plain WG " "AWG 1.0  " "AWG no S4" "AWG full ")
echo "client       client->server           server->client"
for i in 1 2 3 4; do
	up=$(ip netns exec c$i ping -q -c $PINGS -i 0.01 -W 1 10.50.0.1 | grep -o "[0-9.]*% packet loss")
	down=$(ip netns exec srv ping -q -c $PINGS -i 0.01 -W 1 10.50.0.1$i | grep -o "[0-9.]*% packet loss")
	echo "${names[$i]}    $up       $down"
done
for i in 1 2 3 4; do
	hs=$(ip netns exec srv awg show awgs latest-handshakes | grep "$(cat /tmp/cp$i)" | awk '{print $2}')
	echo "${names[$i]}    server saw handshake: $([ "${hs:-0}" != 0 ] && echo yes || echo no)"
done
dmesg | tail -50 | grep -iE "bug:|oops|call trace|general protection|WARNING:" || echo "dmesg clean"
[ "$KEEP" ] || cleanup

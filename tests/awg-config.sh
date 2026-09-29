#!/bin/bash
# AWG configuration checks:
#  - kernel limits (Jc <= 128, Jmin/Jmax <= 1280) and I1-I5 syntax; a rejected
#    request leaves the device unchanged
#  - what a handshake puts on the wire: I1, Jc junk packets, the initiation
#  - reconfiguring under traffic (params are swapped with RCU) loses nothing
#  - unknown-peer notifications are off by default
# Keys go through <(cat ...): the AppArmor profile of wg on Ubuntu blocks /tmp.
umask 077
PINGS=${PINGS:-2000}
fail=0
ok() { echo "ok   $*"; }
bad() { echo "FAIL $*"; fail=1; }
cleanup() { for n in srv cli; do ip netns del $n 2>/dev/null; done; rm -f /tmp/awgcfg.*; }
cleanup
ip netns add srv; ip netns add cli
ip link add s0 netns srv type veth peer name c0 netns cli
ip -n srv addr add 192.168.201.1/24 dev s0; ip -n srv link set s0 up
ip -n cli addr add 192.168.201.2/24 dev c0; ip -n cli link set c0 up
for k in sk ck; do awg genkey > /tmp/awgcfg.$k; awg pubkey < /tmp/awgcfg.$k > /tmp/awgcfg.$k.pub; done

H="h1 100-200 h2 300-400 h3 500-600 h4 700-800000"
S="s1 20 s2 30 s3 10 s4 8"
ip -n srv link add awg0 type amneziawg
ip netns exec srv awg set awg0 private-key <(cat /tmp/awgcfg.sk) listen-port 51000 $H $S \
	peer $(cat /tmp/awgcfg.ck.pub) allowed-ips 10.61.0.2/32
ip -n cli link add awg0 type amneziawg
ip netns exec cli awg set awg0 private-key <(cat /tmp/awgcfg.ck) $H $S \
	jc 5 jmin 100 jmax 200 i1 "<b 0xdeadbeef> <r 10>" \
	peer $(cat /tmp/awgcfg.sk.pub) allowed-ips 10.61.0.0/24 endpoint 192.168.201.1:51000
for n in srv cli; do ip -n $n link set awg0 up; done
ip -n srv addr add 10.61.0.1/24 dev awg0; ip -n cli addr add 10.61.0.2/24 dev awg0

cli() { ip netns exec cli awg "$@" 2>/dev/null; }
conf() { cli showconf awg0 | grep -E "^(Jc|Jmin|Jmax|S[1-4]|H[1-4]|I[1-5]) "; }

# --- limits and syntax ---
before=$(conf)
cli set awg0 jc 129 && bad "jc 129 accepted" || ok "jc 129 rejected"
cli set awg0 jmax 1281 && bad "jmax 1281 accepted" || ok "jmax 1281 rejected"
cli set awg0 jmin 300 jmax 250 && bad "jmin > jmax accepted" || ok "jmin > jmax rejected"
cli set awg0 i2 "abc<b 0x01>" && bad "text outside tags accepted" || ok "text outside tags rejected"
cli set awg0 i2 "<b 0x01" && bad "unclosed tag accepted" || ok "unclosed tag rejected"
cli set awg0 jc 7 i2 "<r 70000>" && bad "i2 over 65535 bytes accepted" || ok "i2 over 65535 bytes rejected"
[ "$(conf)" = "$before" ] && ok "rejected requests changed nothing" || bad "a rejected request changed the device"
cli set awg0 jc 128 jmin 1280 jmax 1280 && ok "jc 128, jmin = jmax = 1280 accepted" || bad "jc 128 / jmax 1280 rejected"
[ "$(cli showconf awg0 | awk '$1=="Jmax"{print $3}')" = 1280 ] && ok "jmax reads back as set" || bad "jmax changed by the module"
cli set awg0 jc 5 jmin 100 jmax 200

# --- a handshake on the wire ---
ip netns exec srv tcpdump -l -nn -i s0 -Q in udp dst port 51000 2>/dev/null > /tmp/awgcfg.dump &
tp=$!; sleep 1
ip netns exec cli ping -q -c 2 -W 2 10.61.0.1 >/dev/null
sleep 1; kill $tp; wait $tp 2>/dev/null
lens=$(grep -o "length [0-9]*" /tmp/awgcfg.dump | awk '{print $2}' | tr '\n' ' ')
set -- $lens
[ "$1" = 14 ] && ok "I1 sent first (14 bytes)" || bad "expected I1 of 14 bytes first, got: $lens"
junk=0; for l in $2 $3 $4 $5 $6; do [ "$l" -ge 100 ] && [ "$l" -le 200 ] && junk=$((junk+1)); done
[ $junk = 5 ] && ok "5 junk packets of 100-200 bytes" || bad "expected 5 junk packets, got: $lens"
[ "$7" = 168 ] && ok "initiation with S1 prefix (168 bytes)" || bad "expected a 168-byte initiation, got: $lens"

# --- reconfiguration under traffic ---
ip netns exec cli ping -q -c $PINGS -i 0.002 -W 1 10.61.0.1 > /tmp/awgcfg.ping &
pp=$!
for i in $(seq 1 40); do
	cli set awg0 jc $((i % 6)) i1 "<b 0x0$((i % 10))><r $i>"
	ip netns exec srv awg set awg0 $H $S # same values: still swaps the parameter set
	ip netns exec srv awg show awg0 >/dev/null
done
wait $pp
loss=$(grep -o "[0-9.]*% packet loss" /tmp/awgcfg.ping)
[ "${loss%%%*}" = 0 ] && ok "80 reconfigurations under traffic: $loss" || bad "reconfiguration under traffic: $loss"

# --- unknown-peer notifications ---
[ "$(cat /sys/module/amneziawg/parameters/unknown_peer_notify)" = N ] && ok "unknown_peer_notify off by default" || bad "unknown_peer_notify is on"

[ $fail = 0 ] && echo "PASS" || echo "FAIL"
dmesg | tail -50 | grep -iE "bug:|oops|call trace|general protection|WARNING:|suspicious rcu" || echo "dmesg clean"
[ "$KEEP" ] || cleanup

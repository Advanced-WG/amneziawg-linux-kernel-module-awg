#!/bin/bash
# Build the module against the kernel headers of many distributions, each in
# a clean container with that distribution's own compiler. Only compiles:
# nothing is loaded, so runtime tests still need a real machine.
#
#   tests/compile-matrix.sh              all targets
#   tests/compile-matrix.sh alma8 deb12  only these
#
# Needs podman (or docker, via ENGINE=docker). Containers use the host
# network, since a host firewall (ufw) often drops the container bridge.
# Warnings are errors (KCFLAGS=-Werror). Logs go to $OUT
# (default ./compile-matrix-out).
set -u
cd "$(dirname "$0")/.."
ENGINE=${ENGINE:-podman}
OUT=${OUT:-$PWD/compile-matrix-out}
JOBS=${JOBS:-$(nproc)}
mkdir -p "$OUT"

# name  image  package-manager  header packages
TARGETS=(
	"alma8     docker.io/library/almalinux:8     dnf kernel-devel"
	"alma9     docker.io/library/almalinux:9     dnf kernel-devel"
	"alma10    docker.io/library/almalinux:10    dnf kernel-devel"
	"stream10  quay.io/centos/centos:stream10    dnf kernel-devel"
	"bionic    docker.io/library/ubuntu:18.04    apt linux-headers-generic"
	"focal     docker.io/library/ubuntu:20.04    apt linux-headers-generic linux-headers-generic-hwe-20.04"
	"jammy     docker.io/library/ubuntu:22.04    apt linux-headers-generic linux-headers-generic-hwe-22.04"
	"noble     docker.io/library/ubuntu:24.04    apt linux-headers-generic linux-headers-generic-hwe-24.04"
	"resolute  docker.io/library/ubuntu:26.04    apt linux-headers-generic"
	"deb10     docker.io/library/debian:10       apt linux-headers-amd64"
	"deb11     docker.io/library/debian:11       apt linux-headers-amd64"
	"deb12     docker.io/library/debian:12       apt linux-headers-amd64"
	"deb13     docker.io/library/debian:13       apt linux-headers-amd64"
	"sid       docker.io/library/debian:sid      apt linux-headers-amd64"
)

# Runs inside the container: install headers, then build against every
# kernel whose headers ended up installed.
read -r -d '' INNER <<'EOF'
set -u
pm=$1; shift
if [ "$pm" = dnf ]; then
	dnf -y -q install gcc make elfutils-libelf-devel diffutils "$@" >/dev/null 2>&1 \
		|| { echo "SETUP-FAILED"; dnf -y install "$@" 2>&1 | tail -5; exit 2; }
	dirs=$(ls -d /usr/src/kernels/*/ 2>/dev/null)
else
	export DEBIAN_FRONTEND=noninteractive
	# Releases past their LTS live on archive.debian.org only.
	. /etc/os-release
	# buster is on archive.debian.org only; bullseye's security pool no
	# longer holds the packages its index lists.
	case $VERSION_CODENAME in
	buster)
		rm -f /etc/apt/sources.list.d/*
		echo "deb http://archive.debian.org/debian buster main" > /etc/apt/sources.list
		echo "deb http://archive.debian.org/debian-security buster/updates main" >> /etc/apt/sources.list ;;
	bullseye)
		sed -i '/security/d' /etc/apt/sources.list ;;
	esac
	{ apt-get -qq -o Acquire::Check-Valid-Until=false update; apt-get -y -qq install --no-install-recommends gcc make "$@"; } >/dev/null 2>&1 \
		|| { echo "SETUP-FAILED"; apt-get -y install --no-install-recommends "$@" 2>&1 | tail -5; exit 2; }
	dirs=$(ls -d /usr/src/linux-headers-*/ | grep -Ev -- '-common/$|-common-rt/$|^/usr/src/linux-headers-[0-9.-]+/$')
fi
echo "gcc: $(gcc -dumpfullversion)"
rc=0
for d in $dirs; do
	kr=$(basename "$d")
	# Ubuntu HWE kernels are built with a newer gcc than the release default.
	cc=$(grep -so 'CONFIG_CC_VERSION_TEXT="[^ ]*gcc-[0-9]*' "$d/.config" | grep -o 'gcc-[0-9]*$')
	if [ -n "$cc" ] && [ "$pm" = apt ] && ! command -v "$cc" >/dev/null; then
		apt-get -y -qq install --no-install-recommends "$cc" >/dev/null 2>&1
	fi
	rm -rf /b && cp -r /src /b
	if make -C /b KERNELDIR="$d" KCFLAGS=-Werror -j"$JOBS" > "/out/$kr.log" 2>&1; then
		echo "OK   $kr"
	else
		echo "FAIL $kr"; rc=1
		grep -E 'error|warning' "/out/$kr.log" | head -5 | sed 's/^/     /'
	fi
done
[ -n "$dirs" ] || { echo "NO-HEADERS"; rc=2; }
exit $rc
EOF

want=" $* "
fail=0
for t in "${TARGETS[@]}"; do
	read -r name image pm pkgs <<< "$t"
	[ $# -eq 0 ] || [[ $want == *" $name "* ]] || continue
	mkdir -p "$OUT/$name"
	echo "=== $name ($image)"
	$ENGINE run --rm --network=host -e JOBS="$JOBS" \
		-v "$PWD/src:/src:ro,Z" -v "$OUT/$name:/out:Z" \
		"$image" bash -c "$INNER" inner "$pm" $pkgs \
		| tee "$OUT/$name/summary.txt"
	[ "${PIPESTATUS[0]}" -eq 0 ] || fail=1
done
exit $fail

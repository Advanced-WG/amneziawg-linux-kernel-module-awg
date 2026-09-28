#!/bin/bash
# Build the module against the kernel headers of many distributions, each in
# a clean container with that distribution's own compiler. Only compiles:
# nothing is loaded, so runtime tests still need a real machine.
#
#   tests/compile-matrix.sh              all targets
#   tests/compile-matrix.sh alma8 deb12  only these
#   PACKAGES=1 tests/compile-matrix.sh   also build the .deb/.rpm from
#                                        HEAD, install them on every
#                                        target and run "dkms build"
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
	"deb11     docker.io/debian/eol:bullseye     apt linux-headers-amd64"
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
	. /etc/os-release
	# buster is on archive.debian.org only. bullseye's security pool lists
	# kernel packages it no longer holds: take the kernel from main.
	case $VERSION_CODENAME in
	buster)
		rm -f /etc/apt/sources.list.d/*
		echo "deb http://archive.debian.org/debian buster main" > /etc/apt/sources.list
		echo "deb http://archive.debian.org/debian-security buster/updates main" >> /etc/apt/sources.list ;;
	bullseye)
		printf 'Package: linux-*\nPin: release l=Debian-Security\nPin-Priority: -1\n' \
			> /etc/apt/preferences.d/no-security-kernel ;;
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
[ -n "$dirs" ] || { echo "NO-HEADERS"; exit 2; }
[ "${PACKAGES:-0}" = 1 ] || exit $rc
if [ "${VERSION_CODENAME:-}" = bullseye ]; then
	echo "SKIP package: the bullseye security pool is missing packages its index lists"
	exit $rc
fi

# Install the release package the way a user does, then let DKMS build it.
if [ "$pm" = dnf ]; then
	{ dnf -y install epel-release && dnf -y install /pkg/amneziawg-dkms-*.rpm; } > /out/package.log 2>&1
else
	apt-get -y install --no-install-recommends /pkg/amneziawg-dkms_*_all.deb > /out/package.log 2>&1
fi
if [ $? -ne 0 ]; then
	echo "FAIL package install"; tail -5 /out/package.log | sed 's/^/     /'
	exit 1
fi
V=$(ls /usr/src | sed -n 's/^amneziawg-//p' | head -1)
echo "OK   package $V (dkms $(dkms --version | grep -o '[0-9][0-9.]*' | head -1))"
for d in $dirs; do
	kr=$(basename "$d"); kr=${kr#linux-headers-}
	if dkms build -m amneziawg -v "$V" -k "$kr" --kernelsourcedir "$d" > "/out/dkms-$kr.log" 2>&1; then
		echo "OK   dkms $kr"
	else
		echo "FAIL dkms $kr"; rc=1
		tail -5 "/out/dkms-$kr.log" | sed 's/^/     /'
	fi
done
exit $rc
EOF

# The .deb is built on Debian 13 and the .rpm on AlmaLinux 10, as for a
# release; every target then installs one of them.
PKG=()
if [ "${PACKAGES:-0}" = 1 ]; then
	V=$(sed -n 's/^Version: *//p' amneziawg-dkms.spec)
	rm -rf "$OUT/pkg" && mkdir -p "$OUT/pkg"
	git archive --prefix="amneziawg-linux-kernel-module-awg-$V/" 		-o "$OUT/pkg/amneziawg-linux-kernel-module-awg-$V.tar.gz" HEAD || exit 1
	echo "=== building packages $V"
	$ENGINE run --rm --network=host -v "$OUT/pkg:/pkg:Z" docker.io/library/debian:13 bash -c '
		export DEBIAN_FRONTEND=noninteractive
		apt-get -qq update && apt-get -y -qq install --no-install-recommends 			build-essential debhelper dh-dkms && cd /tmp && tar xzf /pkg/*.tar.gz &&
		cd amneziawg-* && dpkg-buildpackage -us -uc -b && cp ../*.deb /pkg/' 		> "$OUT/pkg/deb.log" 2>&1 || { echo "FAIL .deb build"; tail -5 "$OUT/pkg/deb.log"; exit 1; }
	$ENGINE run --rm --network=host -v "$OUT/pkg:/pkg:Z" docker.io/library/almalinux:10 bash -c '
		dnf -y install rpm-build make tar gzip && mkdir -p ~/rpmbuild/SOURCES &&
		cp /pkg/*.tar.gz ~/rpmbuild/SOURCES/ && cd /tmp && tar xzf /pkg/*.tar.gz &&
		rpmbuild -bb amneziawg-*/amneziawg-dkms.spec && cp ~/rpmbuild/RPMS/noarch/*.rpm /pkg/' 		> "$OUT/pkg/rpm.log" 2>&1 || { echo "FAIL .rpm build"; tail -5 "$OUT/pkg/rpm.log"; exit 1; }
	ls "$OUT/pkg" | grep -E '\.(deb|rpm)$'
	PKG=(-v "$OUT/pkg:/pkg:ro,Z")
fi

want=" $* "
fail=0
for t in "${TARGETS[@]}"; do
	read -r name image pm pkgs <<< "$t"
	[ $# -eq 0 ] || [[ $want == *" $name "* ]] || continue
	mkdir -p "$OUT/$name"
	echo "=== $name ($image)"
	$ENGINE run --rm --network=host -e JOBS="$JOBS" -e PACKAGES="${PACKAGES:-0}" \
		-v "$PWD/src:/src:ro,Z" -v "$OUT/$name:/out:Z" "${PKG[@]}" \
		"$image" bash -c "$INNER" inner "$pm" $pkgs \
		| tee "$OUT/$name/summary.txt"
	[ "${PIPESTATUS[0]}" -eq 0 ] || fail=1
done
exit $fail

# Installation

## 1. Install prerequisites

Install build tools, DKMS, and kernel headers for your distribution.

### Ubuntu / Linux Mint

```shell
sudo apt update
sudo apt install -y build-essential dkms linux-headers-$(uname -r) linux-headers-generic
```

If you are running the **HWE kernel** (check with `uname -r` — it contains `hwe`):

```shell
sudo apt update
sudo apt install -y build-essential dkms linux-headers-$(uname -r) linux-headers-generic-hwe-$(lsb_release -rs)
```

### Debian

```shell
sudo apt update
sudo apt install -y build-essential dkms linux-headers-$(uname -r) linux-headers-amd64
```

For ARM64 systems use `linux-headers-arm64` instead of `linux-headers-amd64`.

> [!IMPORTANT]
> The `linux-headers-generic` / `linux-headers-amd64` meta-package ensures that kernel headers are **automatically installed** on kernel upgrades. Without it, DKMS will fail to rebuild the module after an update.

### Fedora

```shell
sudo dnf install -y gcc make dkms kernel-devel kernel-headers
```

### RHEL / CentOS / AlmaLinux / Rocky Linux

```shell
sudo dnf install -y epel-release
sudo dnf install -y gcc make dkms "kernel-devel-$(uname -r)" kernel-devel kernel-headers
```

Plain `kernel-devel` installs the headers of the newest kernel in the
repositories, which may not be the running one; `kernel-devel-$(uname -r)`
covers the running kernel, `kernel-devel` the one you will boot next.
Tested on AlmaLinux 10.2.

### openSUSE / SLES

```shell
sudo zypper install -y gcc make dkms kernel-devel
```

### Arch Linux / Manjaro

```shell
sudo pacman -S --needed base-devel dkms linux-headers
```

### Alpine Linux

```shell
sudo apk add gcc make linux-headers
```

> [!NOTE]
> Alpine does not ship DKMS. You will need to rebuild the module manually after each kernel update (`make clean && make && sudo make install`).

## 2. Build and install

```shell
git clone https://github.com/Advanced-WG/amneziawg-linux-kernel-module-awg.git
cd amneziawg-linux-kernel-module-awg/src
```

### Option A — DKMS (recommended)

DKMS automatically rebuilds the module whenever the kernel is updated.

```shell
sudo make dkms-install
sudo dkms install "amneziawg/$(make print-version)"
```

### Option B — direct install

Must be repeated manually after each kernel update.

```shell
make
sudo make install
```

### Option C — release packages

The [releases](https://github.com/Advanced-WG/amneziawg-linux-kernel-module-awg/releases) have `amneziawg-dkms` packages. They contain the sources, and DKMS builds the module for every installed kernel, so the prerequisites of step 1 are still needed.

```shell
# Debian / Ubuntu
sudo apt install ./amneziawg-dkms_1.0.20260927+awg-1_all.deb

# RHEL / AlmaLinux / Rocky / Fedora
sudo dnf install ./amneziawg-dkms-1.0.20260927+awg-1.el10.noarch.rpm
```

Removing the package removes the module from DKMS again. They replace the upstream `amneziawg-dkms` package, so do not install both.

## 3. Load the module

```shell
sudo modprobe amneziawg
```

If this fails with `Key was rejected by service`, Secure Boot is on and the
key DKMS signs the module with is not trusted yet. Enroll it once:

```shell
sudo mokutil --import /var/lib/dkms/mok.pub   # asks for a one-time password
sudo reboot
```

On the next boot the blue **MOK Manager** screen appears on the console:
choose **Enroll MOK → Continue → Yes**, enter the password and reboot. Check
with `mokutil --test-key /var/lib/dkms/mok.pub` ("is already enrolled").
Later DKMS rebuilds are signed with the same key. (On Ubuntu the key is
`/var/lib/shim-signed/mok/MOK.der` and is usually enrolled at install time.)

To load automatically on boot:

```shell
echo "amneziawg" | sudo tee /etc/modules-load.d/amneziawg.conf
```

## 4. Verify

```shell
sudo dkms status          # should show: amneziawg/<version>, ... installed
lsmod | grep amneziawg    # should show the loaded module
```

## Building the packages

From a checkout of the release tag (`git checkout v1.0.20260927+awg`):

```shell
# .deb (Debian/Ubuntu: apt install dpkg-dev debhelper dh-dkms)
dpkg-buildpackage -us -uc -b          # -> ../amneziawg-dkms_<version>-1_all.deb

# .rpm (RHEL-like: dnf install rpm-build)
V=1.0.20260927+awg
mkdir -p ~/rpmbuild/SOURCES
git archive --prefix=amneziawg-linux-kernel-module-awg-$V/ \
    -o ~/rpmbuild/SOURCES/amneziawg-linux-kernel-module-awg-$V.tar.gz v$V
rpmbuild -ba amneziawg-dkms.spec      # -> ~/rpmbuild/RPMS/noarch/
```

The version comes from `debian/changelog` and the `Version:` of the spec; keep them equal to the tag. A plain `make` in a checkout of the tag uses the same version.

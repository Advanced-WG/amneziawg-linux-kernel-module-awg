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
sudo dnf install -y gcc make dkms kernel-devel kernel-headers
```

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

## 3. Load the module

```shell
sudo modprobe amneziawg
```

To load automatically on boot:

```shell
echo "amneziawg" | sudo tee /etc/modules-load.d/amneziawg.conf
```

## 4. Verify

```shell
sudo dkms status          # should show: amneziawg/<version>, ... installed
lsmod | grep amneziawg    # should show the loaded module
```

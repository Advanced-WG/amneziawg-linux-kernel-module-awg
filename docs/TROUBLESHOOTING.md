# Troubleshooting

## Enable debug logging

```shell
echo "module amneziawg +p" | sudo tee /sys/kernel/debug/dynamic_debug/control
dmesg -wT
```

## DKMS build fails after kernel update

Make sure headers for the new kernel are installed:

```shell
# Debian/Ubuntu:
sudo apt install -y linux-headers-$(uname -r)

# Fedora/RHEL:
sudo dnf install -y kernel-devel-$(uname -r)
```

Then rebuild:

```shell
sudo dkms install "amneziawg/$(dkms status | grep amneziawg | awk -F'[/, ]+' '{print $2}' | head -1)" -k $(uname -r)
```

To prevent this in the future, install the headers meta-package — see [Installation](INSTALL.md#1-install-prerequisites).

## Updating to a new version

```shell
cd amneziawg-linux-kernel-module-awg/src
git pull

# Remove old DKMS registration:
sudo dkms remove "amneziawg/$(dkms status | grep amneziawg | awk -F'[/, ]+' '{print $2}' | head -1)" --all

# Install new version:
sudo make dkms-install
sudo dkms install "amneziawg/$(make print-version)"

# Reload:
sudo modprobe -r amneziawg && sudo modprobe amneziawg
```

## Build fails with "No space left on device"

Override the temporary directory:

```shell
export TMPDIR="/home/$USER/tmp"
mkdir -p "$TMPDIR"
```

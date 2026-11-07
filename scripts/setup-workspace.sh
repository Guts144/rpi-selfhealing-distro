#!/usr/bin/env bash
# Complete workspace initialization script for Ubuntu ThinkPad
# Clones essential layers (Scarthgap), attaches meta-selfhealing, configures local.conf

set -euo pipefail

WORKSPACE_ROOT="${HOME}/rpi-selfhealing-distro"
LAYERS_DIR="${WORKSPACE_ROOT}/layers"

echo "============================================================"
echo " Setting up Raspberry Pi 4 Self-Healing Yocto Environment   "
echo " Target Distro: Scarthgap (5.0 LTS) | Machine: RPi4-64     "
echo "============================================================"

# Install essential host build dependencies on Ubuntu
echo "[*] Checking host packages..."
sudo apt-get update
sudo apt-get install -y gawk wget git diffstat unzip texinfo gcc build-essential \
     chrpath socat cpio python3 python3-pip python3-pexpect xz-utils debianutils \
     iputils-ping python3-git python3-jinja2 libegl1-mesa libsdl1.2-dev \
     pylint xterm zstd liblz4-tool libssl-dev u-boot-tools openssl

mkdir -p "${LAYERS_DIR}"
cd "${LAYERS_DIR}"

# Clone only the essential layers locked to Scarthgap LTS
clone_layer() {
    local name="$1"
    local url="$2"
    if [ ! -d "${name}" ]; then
        echo "[*] Cloning ${name} (scarthgap)..."
        git clone -b scarthgap "${url}" "${name}"
    else
        echo "[✓] Layer ${name} already present."
    fi
}

clone_layer "poky" "git://git.yoctoproject.org/poky.git"
clone_layer "meta-raspberrypi" "git://git.yoctoproject.org/meta-raspberrypi"
clone_layer "meta-openembedded" "git://git.openembedded.org/meta-openembedded"
clone_layer "meta-rauc" "https://github.com/rauc/meta-rauc.git"

# Generate development RAUC signing keys
echo "[*] Initializing RAUC cryptographic keys..."
bash "${WORKSPACE_ROOT}/scripts/generate-rauc-keys.sh"

# Source environment
cd "${WORKSPACE_ROOT}"
source layers/poky/oe-init-build-env build

echo "[*] Adding required layers to bblayers.conf..."
bitbake-layers add-layer "${LAYERS_DIR}/meta-openembedded/meta-oe" || true
bitbake-layers add-layer "${LAYERS_DIR}/meta-openembedded/meta-python" || true
bitbake-layers add-layer "${LAYERS_DIR}/meta-openembedded/meta-networking" || true
bitbake-layers add-layer "${LAYERS_DIR}/meta-raspberrypi" || true
bitbake-layers add-layer "${LAYERS_DIR}/meta-rauc" || true
bitbake-layers add-layer "${LAYERS_DIR}/meta-selfhealing" || true

echo "[*] Injecting verified settings into conf/local.conf..."
cat << 'EOF' > conf/local.conf
# Target hardware
MACHINE = "raspberrypi4-64"
DISTRO = "poky"

# Systemd & Industrial DBus init
INIT_MANAGER = "systemd"

# Hardware configurations
ENABLE_UART = "1"
GPU_MEM = "64"
DISABLE_RPI_BOOT_LOGO = "1"
BOOT_DELAY = "0"

# RAUC A/B U-Boot configuration
RPI_USE_U_BOOT = "1"
PREFERRED_PROVIDER_virtual/bootloader = "u-boot"
PREFERRED_PROVIDER_virtual/kernel = "linux-raspberrypi"
KERNEL_IMAGETYPE = "Image"

# Dual-Bank WIC disk image + RAUC ext4.gz payload
IMAGE_FSTYPES = "ext4.gz wic.bz2 wic.bmap"
WKS_FILE = "sdimage-dual-raspberrypi.wks"

# Enable SSH and development tools
EXTRA_IMAGE_FEATURES = "debug-tweaks ssh-server-openssh"

# Shared state and download cache
DL_DIR ?= "${TOPDIR}/../downloads"
SSTATE_DIR ?= "${TOPDIR}/../sstate-cache"

# Save disk space by cleaning intermediate work
INHERIT += "rm_work"
CONF_VERSION = "2"
EOF

echo ""
echo "============================================================"
echo " Workspace ready! To build the image and RAUC bundle:       "
echo "   cd ~/rpi-selfhealing-distro/build                        "
echo "   source ../layers/poky/oe-init-build-env .                "
echo "   bitbake selfhealing-image                                "
echo "   bitbake selfhealing-bundle                               "
echo "============================================================"


SUMMARY = "RAUC OTA Update Bundle for Raspberry Pi 4 Self-Healing Distro"
DESCRIPTION = "Generates a signed .raucb update bundle containing the target root filesystem"
LICENSE = "MIT"

inherit bundle

RAUC_BUNDLE_COMPATIBLE = "raspberrypi4-64"
RAUC_BUNDLE_VERSION = "1.0"
RAUC_BUNDLE_DESCRIPTION = "Raspberry Pi 4 Self-Healing Firmware Update"
RAUC_BUNDLE_FORMAT = "verity"

# Update target slot: rootfs
RAUC_BUNDLE_SLOTS = "rootfs"
RAUC_SLOT_rootfs = "selfhealing-image"
RAUC_SLOT_rootfs[type] = "image"
RAUC_SLOT_rootfs[fstype] = "ext4"

# Cryptographic signing certificate & key
RAUC_KEY_FILE ?= "${THISDIR}/../rauc/files/development-1.key.pem"
RAUC_CERT_FILE ?= "${THISDIR}/../rauc/files/development-1.cert.pem"

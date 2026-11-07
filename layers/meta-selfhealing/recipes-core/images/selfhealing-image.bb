SUMMARY = "Raspberry Pi 4 Self-Healing & RAUC Dual-Bank Image"
DESCRIPTION = "Production-grade embedded image with native C++ self-healing agent, \
HiveMQ MQTT telemetry, and atomic RAUC A/B OTA update support."
LICENSE = "MIT"

inherit core-image

# Enforce WIC dual-partition layout
WKS_FILE = "sdimage-dual-raspberrypi.wks"
WKS_FILE_DEPENDS:append = " rpi-u-boot-scr"

# Ensure boot.scr is copied to FAT /boot partition
IMAGE_BOOT_FILES:append = " boot.scr"

IMAGE_FEATURES += "ssh-server-openssh"

# Core packages & industrial dependencies
IMAGE_INSTALL:append = " \
    packagegroup-core-boot \
    rauc \
    u-boot-tools \
    libubootenv \
    local-agent \
    wpa-supplicant \
    iw \
    iproute2 \
    rfkill \
    kernel-modules \
    ca-certificates \
    e2fsprogs \
    e2fsprogs-mke2fs \
    systemd-analyze \
    mosquitto-clients \
"

# Reserve headroom for updates and local files
IMAGE_ROOTFS_EXTRA_SPACE = "262144"

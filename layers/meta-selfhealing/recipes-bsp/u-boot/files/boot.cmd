# RAUC A/B Dual-Bank Boot Script for Raspberry Pi 4 (64-bit)
# Evaluates boot attempts, selects active slot, and launches kernel

test -n "${BOOT_ORDER}" || setenv BOOT_ORDER "A B"
test -n "${BOOT_A_LEFT}" || setenv BOOT_A_LEFT 3
test -n "${BOOT_B_LEFT}" || setenv BOOT_B_LEFT 3

setenv bootargs_common "console=serial0,115200 console=tty1 8250.nr_uarts=1 rootwait panic=10"

setenv boot_slot 0
for target in ${BOOT_ORDER}; do
    if test "${target}" = "A" -a ${BOOT_A_LEFT} -gt 0 -a ${boot_slot} -eq 0; then
        echo "RAUC: Selecting Slot A (Remaining attempts: ${BOOT_A_LEFT})"
        setexpr BOOT_A_LEFT ${BOOT_A_LEFT} - 1
        setenv bootargs "${bootargs_common} root=/dev/mmcblk0p2 rootfstype=ext4 rauc.slot=A"
        setenv boot_slot 1
    fi
    if test "${target}" = "B" -a ${BOOT_B_LEFT} -gt 0 -a ${boot_slot} -eq 0; then
        echo "RAUC: Selecting Slot B (Remaining attempts: ${BOOT_B_LEFT})"
        setexpr BOOT_B_LEFT ${BOOT_B_LEFT} - 1
        setenv bootargs "${bootargs_common} root=/dev/mmcblk0p3 rootfstype=ext4 rauc.slot=B"
        setenv boot_slot 2
    fi
done

saveenv

if test ${boot_slot} -gt 0; then
    echo "Loading kernel and device tree from boot partition..."
    load mmc 0:1 ${kernel_addr_r} Image
    load mmc 0:1 ${fdt_addr_r} bcm2711-rpi-4-b.dtb
    booti ${kernel_addr_r} - ${fdt_addr_r}
else
    echo "CRITICAL: Both slots exhausted boot attempts! Resetting board in 5s..."
    sleep 5
    reset
fi

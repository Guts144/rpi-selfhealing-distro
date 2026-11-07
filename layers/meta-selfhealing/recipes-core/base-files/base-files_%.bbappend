do_install:append() {
    install -d ${D}/data

    # Add persistent /data partition mount to /etc/fstab
    echo "" >> ${D}${sysconfdir}/fstab
    echo "# Persistent storage partition across RAUC updates" >> ${D}${sysconfdir}/fstab
    echo "/dev/mmcblk0p4  /data  ext4  defaults,noatime,nofail  0  2" >> ${D}${sysconfdir}/fstab
}

FILES:${PN} += "/data"

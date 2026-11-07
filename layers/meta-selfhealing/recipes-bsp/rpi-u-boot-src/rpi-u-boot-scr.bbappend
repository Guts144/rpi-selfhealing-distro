FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI = "file://boot.cmd"

do_compile() {
    mkimage -A arm64 -T script -C none -n "RAUC Boot Script" -d "${WORKDIR}/boot.cmd" "${B}/boot.scr"
}

do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${B}/boot.scr ${DEPLOYDIR}/boot.scr
}

addtask do_deploy after do_compile before do_build

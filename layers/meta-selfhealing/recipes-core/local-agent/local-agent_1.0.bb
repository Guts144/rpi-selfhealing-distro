SUMMARY = "Native C++ Self-Healing Network & RAUC Telemetry Daemon"
DESCRIPTION = "Autonomous embedded watchdog and self-healing agent that restores \
network connectivity and interfaces with HiveMQ and RAUC OTA update controller."
HOMEPAGE = "https://github.com/custom/meta-selfhealing"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://CMakeLists.txt \
    file://constants.txt \
    file://src/config.hpp \
    file://src/main.cpp \
    file://src/system_monitor.hpp \
    file://src/system_monitor.cpp \
    file://src/network_healer.hpp \
    file://src/network_healer.cpp \
    file://src/mqtt_client.hpp \
    file://src/mqtt_client.cpp \
    file://local-agent.service \
"

S = "${WORKDIR}"

DEPENDS = "paho-mqtt-c jsoncpp systemd"
RDEPENDS:${PN} = "wpa-supplicant iproute2 iw rauc systemd"

inherit cmake pkgconfig systemd

SYSTEMD_SERVICE:${PN} = "local-agent.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install:append() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/local-agent.service ${D}${systemd_system_unitdir}/local-agent.service

    # Install constants.txt into /etc/local-agent/ and /etc/
    install -d ${D}${sysconfdir}/local-agent
    install -m 0644 ${WORKDIR}/constants.txt ${D}${sysconfdir}/local-agent/constants.txt
    install -m 0644 ${WORKDIR}/constants.txt ${D}${sysconfdir}/constants.txt

    # Also install an initial copy to persistent /data partition
    install -d ${D}/data
    install -m 0644 ${WORKDIR}/constants.txt ${D}/data/constants.txt
}

FILES:${PN} += " \
    ${bindir}/local-agent \
    ${systemd_system_unitdir}/local-agent.service \
    ${sysconfdir}/local-agent/constants.txt \
    ${sysconfdir}/constants.txt \
    /data/constants.txt \
"

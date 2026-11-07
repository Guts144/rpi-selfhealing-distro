#include "network_healer.hpp"

#include <iostream>
#include <fstream>
#include <ctime>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

NetworkHealer::NetworkHealer(const std::string& probeHost, int probePort)
    : m_probeHost(probeHost), m_probePort(probePort) {
    runShell("rfkill unblock wifi > /dev/null 2>&1; ip link set wlan0 up > /dev/null 2>&1");
    m_ethIface = findEthernetInterface();
}

std::string NetworkHealer::getActiveMediumString() const {
    switch (m_activeMedium) {
        case ActiveMedium::ETHERNET: return "ETHERNET";
        case ActiveMedium::WIFI:     return "WIFI";
        default:                     return "NONE";
    }
}

std::string NetworkHealer::findEthernetInterface() {
    std::vector<std::string> candidates = {"eth0", "end0", "enp1s0"};
    for (const auto& name : candidates) {
        std::string path = "/sys/class/net/" + name;
        if (access(path.c_str(), F_OK) == 0) {
            return name;
        }
    }

    DIR* dir = opendir("/sys/class/net");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.rfind("eth", 0) == 0 || name.rfind("en", 0) == 0) {
                closedir(dir);
                return name;
            }
        }
        closedir(dir);
    }
    return "eth0";
}

bool NetworkHealer::isEthernetCarrierUp(const std::string& iface) {
    if (iface.empty()) return false;
    std::string carrierPath = "/sys/class/net/" + iface + "/carrier";
    std::ifstream f(carrierPath);
    if (f.is_open()) {
        char val = '0';
        f >> val;
        return (val == '1');
    }
    return false;
}

bool NetworkHealer::checkEthernet(const std::string& iface) {
    runShell("ip link set " + iface + " up > /dev/null 2>&1");

    if (probeTcp(m_probeHost, m_probePort, 2)) {
        return true;
    }

    // Attempt DHCP lease renewal if carrier is active
    runShell("networkctl renew " + iface + " > /dev/null 2>&1 || udhcpc -i " + iface + " -n -q > /dev/null 2>&1");
    sleep(2);
    return probeTcp(m_probeHost, m_probePort, 2);
}

bool NetworkHealer::checkConnectivity() {
    if (m_ethIface.empty()) {
        m_ethIface = findEthernetInterface();
    }

    // 1. Look FIRST for an Ethernet connection
    if (!m_ethIface.empty() && isEthernetCarrierUp(m_ethIface)) {
        if (checkEthernet(m_ethIface)) {
            if (m_activeMedium != ActiveMedium::ETHERNET) {
                std::cout << "[NetworkHealer] Ethernet (" << m_ethIface 
                          << ") connected and ONLINE. Troubleshooting stopped." << std::endl;
                logEvent(0, "Ethernet Connected (" + m_ethIface + ")", true);
            }
            m_activeMedium = ActiveMedium::ETHERNET;
            m_status = NetworkStatus::ONLINE;
            m_consecutiveFailures = 0;
            m_wifiRetries = 0;
            m_wifiTroubleshootingStopped = false;
            return true;
        }
    }

    // 2. If Ethernet was active and now lost
    if (m_activeMedium == ActiveMedium::ETHERNET) {
        std::cout << "[NetworkHealer] Ethernet disconnected. Switching to Wi-Fi..." << std::endl;
        logEvent(0, "Ethernet Disconnected", false);
        m_activeMedium = ActiveMedium::NONE;
        m_wifiRetries = 0;
        m_wifiTroubleshootingStopped = false;
    }

    // 3. Look for Wi-Fi networks (BEE_WIFI_02ED / Topnet)
    return checkWifi();
}

bool NetworkHealer::checkWifi() {
    // If Wi-Fi is already providing working connectivity, stop troubleshooting
    if (probeTcp(m_probeHost, m_probePort, 2)) {
        if (m_activeMedium != ActiveMedium::WIFI) {
            std::cout << "[NetworkHealer] Wi-Fi connected and ONLINE. Troubleshooting stopped." << std::endl;
            logEvent(0, "Wi-Fi Connected", true);
        }
        m_activeMedium = ActiveMedium::WIFI;
        m_status = NetworkStatus::ONLINE;
        m_consecutiveFailures = 0;
        m_wifiRetries = 0;
        m_wifiTroubleshootingStopped = false;
        return true;
    }

    // Wi-Fi is offline or connection lost
    if (m_activeMedium == ActiveMedium::WIFI) {
        std::cout << "[NetworkHealer] Wi-Fi connection lost." << std::endl;
        logEvent(0, "Wi-Fi Disconnected", false);
        m_activeMedium = ActiveMedium::NONE;
        m_wifiRetries = 0;
        m_wifiTroubleshootingStopped = false;
    }

    m_status = NetworkStatus::OFFLINE;
    m_consecutiveFailures++;

    // "and if it connects it stops until connection lost it looks again twice and then stops"
    if (m_wifiRetries < 2) {
        m_wifiRetries++;
        std::cout << "[NetworkHealer] Looking for configured Wi-Fi networks (attempt " 
                  << m_wifiRetries << " of 2)..." << std::endl;

        bool ok = healWifiAttempt(m_wifiRetries);
        if (ok) {
            std::cout << "[NetworkHealer] Wi-Fi successfully connected! Troubleshooting stopped." << std::endl;
            m_activeMedium = ActiveMedium::WIFI;
            m_status = NetworkStatus::ONLINE;
            m_consecutiveFailures = 0;
            m_wifiRetries = 0;
            m_wifiTroubleshootingStopped = false;
            return true;
        }

        if (m_wifiRetries >= 2) {
            m_wifiTroubleshootingStopped = true;
            std::cout << "[NetworkHealer] Wi-Fi attempts exhausted (looked twice). Troubleshooting stopped until connection state changes." << std::endl;
            logEvent(m_wifiRetries, "Wi-Fi Retries Exhausted (2/2) - Stopped", false);
        }
    }

    return false;
}

bool NetworkHealer::healWifiAttempt(int attemptNum) {
    std::string actionName;
    std::string cmd;

    if (attemptNum == 1) {
        actionName = "Wi-Fi Re-association & wpa_supplicant Scan";
        cmd = "rfkill unblock wifi > /dev/null 2>&1; "
              "ip link set wlan0 up > /dev/null 2>&1; "
              "wpa_cli -i wlan0 reassociate > /dev/null 2>&1 || systemctl start wpa_supplicant@wlan0 > /dev/null 2>&1; "
              "networkctl renew wlan0 > /dev/null 2>&1";
    } else {
        actionName = "Wi-Fi Full Stack Restart (wpa_supplicant & networkctl)";
        cmd = "systemctl restart wpa_supplicant@wlan0 > /dev/null 2>&1; "
              "systemctl restart systemd-networkd > /dev/null 2>&1; "
              "sleep 1; "
              "wpa_cli -i wlan0 reassociate > /dev/null 2>&1; "
              "networkctl renew wlan0 > /dev/null 2>&1";
    }

    std::cout << "[NetworkHealer] Executing: " << actionName << std::endl;
    runShell(cmd);

    // Wait for wpa_supplicant handshake and DHCP lease
    sleep(5);

    bool ok = probeTcp(m_probeHost, m_probePort, 2);
    logEvent(attemptNum, actionName, ok);
    return ok;
}

void NetworkHealer::resetHealer() {
    m_consecutiveFailures = 0;
    m_wifiRetries = 0;
    m_wifiTroubleshootingStopped = false;
    m_status = NetworkStatus::ONLINE;
}

bool NetworkHealer::probeTcp(const std::string& host, int port, int timeoutSec) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        close(sock);
        return false;
    }

    connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(sock, &fdset);

    struct timeval tv{};
    tv.tv_sec = timeoutSec;
    tv.tv_usec = 0;

    int sel = select(sock + 1, nullptr, &fdset, nullptr, &tv);
    if (sel > 0) {
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
        close(sock);
        return (so_error == 0);
    }

    close(sock);
    return false;
}

int NetworkHealer::runShell(const std::string& command) {
    return std::system(command.c_str());
}

void NetworkHealer::logEvent(int stage, const std::string& action, bool success) {
    std::time_t now = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));

    HealEvent ev{
        std::string(buf),
        stage,
        action,
        success
    };

    m_events.push_back(ev);
    if (m_events.size() > 20) {
        m_events.erase(m_events.begin());
    }
}

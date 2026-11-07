#pragma once

#include <string>
#include <vector>
#include <chrono>

enum class NetworkStatus {
    ONLINE,
    DEGRADED,
    OFFLINE
};

enum class ActiveMedium {
    NONE,
    ETHERNET,
    WIFI
};

struct HealEvent {
    std::string timestamp;
    int stage;
    std::string action;
    bool success;
};

class NetworkHealer {
public:
    NetworkHealer(const std::string& probeHost = "8.8.8.8", int probePort = 53);
    ~NetworkHealer() = default;

    bool checkConnectivity();
    void resetHealer();

    NetworkStatus getStatus() const { return m_status; }
    ActiveMedium getActiveMedium() const { return m_activeMedium; }
    std::string getActiveMediumString() const;
    int getConsecutiveFailures() const { return m_consecutiveFailures; }
    int getWifiRetries() const { return m_wifiRetries; }
    bool isTroubleshootingStopped() const { return m_wifiTroubleshootingStopped; }
    std::vector<HealEvent> getRecentEvents() const { return m_events; }

    bool probeTcp(const std::string& host, int port, int timeoutSec = 2);

private:
    std::string findEthernetInterface();
    bool isEthernetCarrierUp(const std::string& iface);
    bool checkEthernet(const std::string& iface);
    bool checkWifi();
    bool healWifiAttempt(int attemptNum);

    void logEvent(int stage, const std::string& action, bool success);
    int runShell(const std::string& command);

    std::string m_probeHost;
    int m_probePort;
    NetworkStatus m_status{NetworkStatus::OFFLINE};
    ActiveMedium m_activeMedium{ActiveMedium::NONE};
    std::string m_ethIface;
    int m_consecutiveFailures{0};
    int m_wifiRetries{0};
    bool m_wifiTroubleshootingStopped{false};
    std::vector<HealEvent> m_events;
};

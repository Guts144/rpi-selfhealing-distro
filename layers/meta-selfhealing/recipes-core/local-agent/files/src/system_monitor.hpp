#pragma once

#include <string>
#include <vector>

struct SystemStats {
    double cpu_temp_c;
    double ram_usage_percent;
    double load_1min;
    double load_5min;
    double load_15min;
    int wifi_rssi_dbm;
    std::string active_rauc_slot;
    long uptime_seconds;
};

class SystemMonitor {
public:
    SystemMonitor() = default;
    ~SystemMonitor() = default;

    SystemStats collectStats();
    bool markBootSlotGood();
    std::string getRaucStatusJson();

private:
    double readCpuTemp();
    double readRamUsage();
    int readWifiRssi();
    std::string detectActiveSlot();
    long readUptime();
};

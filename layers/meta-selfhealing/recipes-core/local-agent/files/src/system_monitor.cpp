#include "system_monitor.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <array>
#include <memory>
#include <regex>

SystemStats SystemMonitor::collectStats() {
    SystemStats stats{};
    stats.cpu_temp_c = readCpuTemp();
    stats.ram_usage_percent = readRamUsage();

    double load[3];
    if (getloadavg(load, 3) != -1) {
        stats.load_1min = load[0];
        stats.load_5min = load[1];
        stats.load_15min = load[2];
    }

    stats.wifi_rssi_dbm = readWifiRssi();
    stats.active_rauc_slot = detectActiveSlot();
    stats.uptime_seconds = readUptime();

    return stats;
}

double SystemMonitor::readCpuTemp() {
    std::ifstream file("/sys/class/thermal/thermal_zone0/temp");
    if (!file.is_open()) {
        return -1.0;
    }
    double milli_c = 0.0;
    file >> milli_c;
    return milli_c / 1000.0;
}

double SystemMonitor::readRamUsage() {
    std::ifstream file("/proc/meminfo");
    if (!file.is_open()) {
        return 0.0;
    }

    std::string line;
    long total_kb = 0;
    long avail_kb = 0;

    while (std::getline(file, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            std::sscanf(line.c_str(), "MemTotal: %ld kB", &total_kb);
        } else if (line.rfind("MemAvailable:", 0) == 0) {
            std::sscanf(line.c_str(), "MemAvailable: %ld kB", &avail_kb);
        }
    }

    if (total_kb <= 0) return 0.0;
    long used_kb = total_kb - avail_kb;
    return (static_cast<double>(used_kb) / total_kb) * 100.0;
}

int SystemMonitor::readWifiRssi() {
    // Parse /proc/net/wireless for wlan0 link quality
    std::ifstream file("/proc/net/wireless");
    if (!file.is_open()) {
        return 0;
    }

    std::string line;
    // Skip first two header lines
    std::getline(file, line);
    std::getline(file, line);

    while (std::getline(file, line)) {
        if (line.find("wlan0:") != std::string::npos) {
            std::istringstream iss(line);
            std::string iface;
            int status = 0;
            double quality = 0.0;
            double level = 0.0; // dBm
            iss >> iface >> status >> quality >> level;
            return static_cast<int>(level);
        }
    }
    return 0;
}

std::string SystemMonitor::detectActiveSlot() {
    // 1. Check kernel cmdline for rauc.slot=
    std::ifstream cmdline("/proc/cmdline");
    if (cmdline.is_open()) {
        std::string content;
        std::getline(cmdline, content);
        std::regex re("rauc\\.slot=([A-Za-z0-9_-]+)");
        std::smatch match;
        if (std::regex_search(content, match, re) && match.size() > 1) {
            return match[1].str();
        }
    }

    // 2. Fallback to rauc command
    std::array<char, 128> buffer;
    std::string result;
    using PcloseFunc = int(*)(FILE*);
    std::unique_ptr<FILE, PcloseFunc> pipe(popen("rauc status --output-format=shell 2>/dev/null | grep RAUC_BOOT_PRIMARY", "r"), pclose);
    if (pipe) {
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
        std::regex re("RAUC_BOOT_PRIMARY='?([A-Za-z0-9_.-]+)'?");
        std::smatch match;
        if (std::regex_search(result, match, re) && match.size() > 1) {
            return match[1].str();
        }
    }

    return "unknown";
}

bool SystemMonitor::markBootSlotGood() {
    int ret = std::system("rauc status mark-good > /dev/null 2>&1");
    return (ret == 0);
}

std::string SystemMonitor::getRaucStatusJson() {
    std::array<char, 256> buffer;
    std::string result;
    using PcloseFunc = int(*)(FILE*);
    std::unique_ptr<FILE, PcloseFunc> pipe(popen("rauc status --output-format=json 2>/dev/null", "r"), pclose);
    if (!pipe) {
        return "{}";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result.empty() ? "{}" : result;
}

long SystemMonitor::readUptime() {
    std::ifstream file("/proc/uptime");
    if (!file.is_open()) {
        return 0;
    }
    double sec = 0.0;
    file >> sec;
    return static_cast<long>(sec);
}

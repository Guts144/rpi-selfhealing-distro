#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>

struct AgentConfig {
    std::string broker_uri = "ssl://003405b159a641e2bed741f8db2e0344.s1.eu.hivemq.cloud:8883";
    std::string client_id = "rpi4-selfhealing-agent";
    std::string username = "mgharsal";
    std::string password = "Anzb123.";
    std::string topic_telemetry = "rpi4/selfhealing/telemetry";
    std::string topic_events = "rpi4/selfhealing/events";
    std::string topic_commands = "rpi4/selfhealing/commands";
    std::string probe_host = "8.8.8.8";
    int probe_port = 53;
    int telemetry_interval_sec = 10;
    std::string loaded_path = "defaults";

    static std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    static AgentConfig load(const std::string& customPath = "") {
        AgentConfig config;
        std::vector<std::string> pathsToTry;

        if (!customPath.empty()) {
            pathsToTry.push_back(customPath);
        }
        // 1. Persistent partition (survives RAUC updates)
        pathsToTry.push_back("/data/constants.txt");
        // 2. Default rootfs configuration paths
        pathsToTry.push_back("/etc/local-agent/constants.txt");
        pathsToTry.push_back("/etc/constants.txt");
        // 3. Local working directory fallback
        pathsToTry.push_back("./constants.txt");

        std::ifstream file;
        std::string foundPath;
        for (const auto& path : pathsToTry) {
            file.open(path);
            if (file.is_open()) {
                foundPath = path;
                break;
            }
        }

        if (!file.is_open()) {
            std::cout << "[Config] Warning: constants.txt not found. Using built-in defaults." << std::endl;
            return config;
        }

        config.loaded_path = foundPath;
        std::cout << "[Config] Successfully loaded configuration from: " << foundPath << std::endl;

        std::string line;
        while (std::getline(file, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') {
                continue;
            }
            size_t delimPos = line.find('=');
            if (delimPos == std::string::npos) {
                continue;
            }
            std::string key = trim(line.substr(0, delimPos));
            std::string value = trim(line.substr(delimPos + 1));

            if (key == "BROKER_URI") config.broker_uri = value;
            else if (key == "CLIENT_ID") config.client_id = value;
            else if (key == "USERNAME") config.username = value;
            else if (key == "PASSWORD") config.password = value;
            else if (key == "TOPIC_TELEMETRY") config.topic_telemetry = value;
            else if (key == "TOPIC_EVENTS") config.topic_events = value;
            else if (key == "TOPIC_COMMANDS") config.topic_commands = value;
            else if (key == "PROBE_HOST") config.probe_host = value;
            else if (key == "PROBE_PORT") {
                try { config.probe_port = std::stoi(value); } catch (...) {}
            }
            else if (key == "TELEMETRY_INTERVAL_SEC") {
                try { config.telemetry_interval_sec = std::stoi(value); } catch (...) {}
            }
        }

        return config;
    }
};

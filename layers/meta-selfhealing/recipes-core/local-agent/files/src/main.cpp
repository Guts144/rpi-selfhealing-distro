#include "config.hpp"
#include "system_monitor.hpp"
#include "network_healer.hpp"
#include "mqtt_client.hpp"

#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <atomic>
#include <csignal>
#include <systemd/sd-daemon.h>
#include <json/json.h>

static std::atomic<bool> g_running{true};

void signalHandler(int signum) {
    std::cout << "\n[Main] Caught signal " << signum << ", initiating shutdown..." << std::endl;
    g_running = false;
}

std::string buildTelemetryJson(const SystemStats& stats, 
                               const std::string& deviceId,
                               NetworkStatus netStatus,
                               const std::string& activeMedium,
                               int wifiRetries,
                               bool troubleshootingStopped,
                               const std::vector<HealEvent>& events) {
    Json::Value root;
    root["device_id"] = deviceId;
    root["active_slot"] = stats.active_rauc_slot;
    root["cpu_temp_c"] = stats.cpu_temp_c;
    root["ram_usage_percent"] = stats.ram_usage_percent;
    root["load_1m"] = stats.load_1min;
    root["load_5m"] = stats.load_5min;
    root["load_15m"] = stats.load_15min;
    root["wifi_rssi_dbm"] = stats.wifi_rssi_dbm;
    root["uptime_sec"] = static_cast<Json::Int64>(stats.uptime_seconds);
    root["active_medium"] = activeMedium;
    root["wifi_retries"] = wifiRetries;
    root["troubleshooting_stopped"] = troubleshootingStopped;

    switch (netStatus) {
        case NetworkStatus::ONLINE:   root["network_status"] = "ONLINE"; break;
        case NetworkStatus::DEGRADED: root["network_status"] = "DEGRADED"; break;
        case NetworkStatus::OFFLINE:  root["network_status"] = "OFFLINE"; break;
    }

    Json::Value evArray(Json::arrayValue);
    for (const auto& ev : events) {
        Json::Value item;
        item["timestamp"] = ev.timestamp;
        item["stage"] = ev.stage;
        item["action"] = ev.action;
        item["success"] = ev.success;
        evArray.append(item);
    }
    root["healing_events"] = evArray;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, root);
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "=================================================" << std::endl;
    std::cout << "   Raspberry Pi 4 Embedded Self-Healing Agent    " << std::endl;
    std::cout << "   Ethernet-First, Dual-SSID Wi-Fi & HiveMQ TLS  " << std::endl;
    std::cout << "=================================================" << std::endl;

    // Load configuration constants from constants.txt
    std::string configArg = (argc > 1) ? argv[1] : "";
    AgentConfig config = AgentConfig::load(configArg);

    std::cout << "[Main] Using Broker: " << config.broker_uri << std::endl;
    std::cout << "[Main] Client ID:    " << config.client_id << std::endl;
    std::cout << "[Main] Telemetry:    " << config.topic_telemetry << std::endl;
    std::cout << "[Main] Commands:     " << config.topic_commands << std::endl;

    SystemMonitor monitor;
    NetworkHealer healer(config.probe_host, config.probe_port);
    MqttManager mqtt(config.broker_uri, config.client_id, config.username, config.password);

    bool bootSlotMarkedGood = false;
    auto lastTelemetryTime = std::chrono::steady_clock::now();
    auto bootTime = std::chrono::steady_clock::now();

    // Register MQTT command handler
    mqtt.setCommandHandler([&](const std::string& /*topic*/, const std::string& payload) {
        Json::CharReaderBuilder readerBuilder;
        Json::Value root;
        std::string errs;
        std::istringstream s(payload);
        if (!Json::parseFromStream(readerBuilder, s, &root, &errs)) {
            std::cerr << "[Command] Invalid JSON received: " << errs << std::endl;
            return;
        }

        std::string cmd = root.get("command", "").asString();
        std::cout << "[Command] Processing incoming command: " << cmd << std::endl;

        if (cmd == "ping") {
            Json::Value resp;
            resp["type"] = "pong";
            resp["device"] = config.client_id;
            resp["status"] = "running";
            resp["active_medium"] = healer.getActiveMediumString();
            resp["config_path"] = config.loaded_path;
            Json::StreamWriterBuilder wb;
            wb["indentation"] = "";
            mqtt.publish(config.topic_events, Json::writeString(wb, resp));
        } else if (cmd == "heal") {
            std::cout << "[Command] Manual network heal requested. Resetting state and checking connectivity..." << std::endl;
            healer.resetHealer();
            healer.checkConnectivity();
        } else if (cmd == "mark_good") {
            bool ok = monitor.markBootSlotGood();
            std::cout << "[Command] Manual mark-good outcome: " << (ok ? "SUCCESS" : "FAILED") << std::endl;
        } else if (cmd == "rauc_install") {
            std::string bundleUrl = root.get("url", "").asString();
            if (!bundleUrl.empty()) {
                std::cout << "[Command] Starting RAUC OTA update from: " << bundleUrl << std::endl;
                MqttManager* mqttPtr = &mqtt;
                AgentConfig* cfgPtr = &config;
                std::thread([bundleUrl, mqttPtr, cfgPtr]() {
                    std::string installCmd = "rauc install " + bundleUrl;
                    int res = std::system(installCmd.c_str());
                    Json::Value updateResp;
                    updateResp["type"] = "rauc_install_result";
                    updateResp["success"] = (res == 0);
                    updateResp["exit_code"] = res;
                    Json::StreamWriterBuilder wb;
                    wb["indentation"] = "";
                    mqttPtr->publish(cfgPtr->topic_events, Json::writeString(wb, updateResp));
                }).detach();
            }
        } else if (cmd == "reboot") {
            std::cout << "[Command] Rebooting device in 2 seconds..." << std::endl;
            std::thread([]() {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                static_cast<void>(std::system("systemctl reboot"));
            }).detach();
        }
    });

    // Notify systemd that service is ready
    sd_notify(0, "READY=1");

    while (g_running) {
        // Feed systemd watchdog
        sd_notify(0, "WATCHDOG=1");

        // 1. Run network connectivity check
        // Looks first for Ethernet; if ok, pauses troubleshooting.
        // If no Ethernet or connection lost, looks for configured Wi-Fi (looks twice and then stops).
        bool isOnline = healer.checkConnectivity();

        // 2. MQTT connection handling
        // Try to connect to HiveMQ broker whenever device is connected to the internet until it connects,
        // then halt connection attempts until connection is lost.
        if (isOnline) {
            if (!mqtt.isConnected()) {
                std::cout << "[MQTT] Internet available. Connecting to HiveMQ broker: " 
                          << config.broker_uri << "..." << std::endl;
                if (mqtt.connect()) {
                    std::cout << "[MQTT] Connected to HiveMQ successfully! Halting connection attempts." << std::endl;
                    mqtt.subscribe(config.topic_commands);
                } else {
                    std::cerr << "[MQTT] Connection attempt failed. Will retry next cycle while internet is available." << std::endl;
                }
            }
        } else {
            // Internet offline
            if (mqtt.isConnected()) {
                std::cout << "[MQTT] Internet dropped. Disconnecting MQTT." << std::endl;
                mqtt.disconnect();
            }
        }

        // 3. Mark boot slot good after 60s of stable network & execution
        if (isOnline && !bootSlotMarkedGood) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - bootTime).count();
            if (elapsed >= 60) {
                if (monitor.markBootSlotGood()) {
                    std::cout << "[Agent] RAUC boot slot successfully marked GOOD." << std::endl;
                    bootSlotMarkedGood = true;
                }
            }
        }

        // 4. Publish telemetry periodically if MQTT is connected
        if (mqtt.isConnected()) {
            auto now = std::chrono::steady_clock::now();
            auto sinceTelemetry = std::chrono::duration_cast<std::chrono::seconds>(now - lastTelemetryTime).count();
            if (sinceTelemetry >= config.telemetry_interval_sec) {
                SystemStats stats = monitor.collectStats();
                std::string payload = buildTelemetryJson(stats, 
                                                         config.client_id,
                                                         healer.getStatus(), 
                                                         healer.getActiveMediumString(),
                                                         healer.getWifiRetries(),
                                                         healer.isTroubleshootingStopped(),
                                                         healer.getRecentEvents());
                mqtt.publish(config.topic_telemetry, payload);
                lastTelemetryTime = now;
            }
        }

        // Sleep 3 seconds per iteration
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    // Clean shutdown
    sd_notify(0, "STOPPING=1");
    mqtt.disconnect();
    std::cout << "[Main] Daemon stopped gracefully." << std::endl;
    return 0;
}

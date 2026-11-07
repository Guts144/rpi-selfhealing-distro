#pragma once

#include <string>
#include <functional>
#include <MQTTClient.h>

using CommandHandler = std::function<void(const std::string& cmd, const std::string& payload)>;

class MqttManager {
public:
    MqttManager(const std::string& serverUri, 
                const std::string& clientId,
                const std::string& username = "",
                const std::string& password = "");
    ~MqttManager();

    bool connect();
    void disconnect();
    bool isConnected();

    bool publish(const std::string& topic, const std::string& payload, int qos = 1);
    bool subscribe(const std::string& topic, int qos = 1);

    void setCommandHandler(CommandHandler handler) { m_commandHandler = handler; }

private:
    static int messageArrived(void* context, char* topicName, int topicLen, MQTTClient_message* message);
    static void connectionLost(void* context, char* cause);
    static void deliveryComplete(void* context, MQTTClient_deliveryToken dt);

    std::string m_serverUri;
    std::string m_clientId;
    std::string m_username;
    std::string m_password;
    MQTTClient m_client{nullptr};
    CommandHandler m_commandHandler{nullptr};
    bool m_connected{false};
};

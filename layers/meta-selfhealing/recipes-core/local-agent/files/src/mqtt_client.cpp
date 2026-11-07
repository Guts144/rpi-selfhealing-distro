#include "mqtt_client.hpp"

#include <iostream>
#include <cstring>
#include <unistd.h>

MqttManager::MqttManager(const std::string& serverUri, 
                         const std::string& clientId,
                         const std::string& username,
                         const std::string& password)
    : m_serverUri(serverUri), m_clientId(clientId), m_username(username), m_password(password) {}

MqttManager::~MqttManager() {
    disconnect();
}

bool MqttManager::connect() {
    std::string uri = m_serverUri;
    // Normalize URI if protocol prefix is omitted
    if (uri.find("://") == std::string::npos) {
        if (uri.find(":8883") != std::string::npos) {
            uri = "ssl://" + uri;
        } else {
            uri = "tcp://" + uri;
        }
    }

    if (m_client) {
        MQTTClient_destroy(&m_client);
        m_client = nullptr;
    }

    int rc = MQTTClient_create(&m_client, uri.c_str(), m_clientId.c_str(),
                               MQTTCLIENT_PERSISTENCE_NONE, nullptr);
    if (rc != MQTTCLIENT_SUCCESS) {
        std::cerr << "[MQTT] Failed to create client, code: " << rc << std::endl;
        return false;
    }

    MQTTClient_setCallbacks(m_client, this, connectionLost, messageArrived, deliveryComplete);

    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.connectTimeout = 5;

    if (!m_username.empty()) {
        conn_opts.username = m_username.c_str();
    }
    if (!m_password.empty()) {
        conn_opts.password = m_password.c_str();
    }

    MQTTClient_SSLOptions ssl_opts = MQTTClient_SSLOptions_initializer;
    if (uri.rfind("ssl://", 0) == 0 || uri.rfind("wss://", 0) == 0) {
        ssl_opts.enableServerCertAuth = 1;
        // Point to standard system CA bundle if present
        if (access("/etc/ssl/certs/ca-certificates.crt", 0) == 0) {
            ssl_opts.trustStore = "/etc/ssl/certs/ca-certificates.crt";
        } else if (access("/etc/ssl/cert.pem", 0) == 0) {
            ssl_opts.trustStore = "/etc/ssl/cert.pem";
        }
        conn_opts.ssl = &ssl_opts;
    }

    rc = MQTTClient_connect(m_client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        std::cerr << "[MQTT] Failed to connect to broker at " << uri << ", code: " << rc << std::endl;
        m_connected = false;
        return false;
    }

    m_connected = true;
    std::cout << "[MQTT] Connected successfully to broker: " << uri << std::endl;
    return true;
}

void MqttManager::disconnect() {
    if (m_client) {
        if (m_connected) {
            MQTTClient_disconnect(m_client, 1000);
        }
        MQTTClient_destroy(&m_client);
        m_client = nullptr;
        m_connected = false;
    }
}

bool MqttManager::isConnected() {
    if (!m_client || !m_connected) return false;
    return MQTTClient_isConnected(m_client) == 1;
}

bool MqttManager::publish(const std::string& topic, const std::string& payload, int qos) {
    if (!isConnected()) {
        return false;
    }

    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = const_cast<char*>(payload.c_str());
    pubmsg.payloadlen = static_cast<int>(payload.length());
    pubmsg.qos = qos;
    pubmsg.retained = 0;

    MQTTClient_deliveryToken token;
    int rc = MQTTClient_publishMessage(m_client, topic.c_str(), &pubmsg, &token);
    return (rc == MQTTCLIENT_SUCCESS);
}

bool MqttManager::subscribe(const std::string& topic, int qos) {
    if (!isConnected()) return false;
    int rc = MQTTClient_subscribe(m_client, topic.c_str(), qos);
    if (rc == MQTTCLIENT_SUCCESS) {
        std::cout << "[MQTT] Subscribed to topic: " << topic << std::endl;
        return true;
    }
    std::cerr << "[MQTT] Failed to subscribe to: " << topic << std::endl;
    return false;
}

int MqttManager::messageArrived(void* context, char* topicName, int /*topicLen*/, MQTTClient_message* message) {
    auto* self = static_cast<MqttManager*>(context);
    std::string topic(topicName);
    std::string payload(static_cast<char*>(message->payload), message->payloadlen);

    std::cout << "[MQTT] Incoming message on topic [" << topic << "]: " << payload << std::endl;

    if (self && self->m_commandHandler) {
        self->m_commandHandler(topic, payload);
    }

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void MqttManager::connectionLost(void* context, char* cause) {
    auto* self = static_cast<MqttManager*>(context);
    if (self) {
        self->m_connected = false;
    }
    std::cerr << "[MQTT] Connection lost. Cause: " << (cause ? cause : "unknown") << std::endl;
}

void MqttManager::deliveryComplete(void* /*context*/, MQTTClient_deliveryToken /*dt*/) {
    // Delivery ack
}

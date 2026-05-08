#ifndef MQTT_H
#define MQTT_H

#include <WiFi.h>
#include <PubSubClient.h>

extern bool dashboardSwitchState; 
extern int dashboardSliderValue;

// Variabler eksporteret, så main.cpp kan se dem
bool dashboardSwitchState = false; 
int dashboardSliderValue = 1;

// Maqiatto Broker Settings 
const char* mqtt_server = "maqiatto.com";
const int mqtt_port = 1883;
const char* mqtt_user = "asger.frimor@gmail.com"; 
const char* mqtt_password = "123456789";

const char* topic_switch = "asger.frimor@gmail.com/esp32/switch";
const char* topic_slider = "asger.frimor@gmail.com/esp32/slider";

WiFiClient mqttClient; // Separat klient til MQTT (Firebase bruger sin egen)
PubSubClient client(mqttClient);

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    String messageTemp;
    for (unsigned int i = 0; i < length; i++) {
        messageTemp += (char)payload[i];
    }
    
    if (String(topic) == topic_switch) {
        if (messageTemp == "true" || messageTemp == "1" || messageTemp == "ON") {
            dashboardSwitchState = true; 
            Serial.println("MQTT Switch: ON");
        } else {
            dashboardSwitchState = false;
            Serial.println("MQTT Switch: OFF");
        }
    } 
    else if (String(topic) == topic_slider) {
        dashboardSliderValue = messageTemp.toInt(); 
        Serial.printf("MQTT Slider sat til: %d omdrejninger\n", dashboardSliderValue);
    }
}

void mqtt_setup() {
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(mqtt_callback);
}

void mqtt_loop() {
    if (!client.connected()) {
        if (WiFi.status() == WL_CONNECTED) {
            String clientId = "ESP32Client-" + String(random(0xffff), HEX);
            if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
                Serial.println("MQTT forbundet!");
                client.subscribe(topic_switch);
                client.subscribe(topic_slider);
            }
        }
    }
    if (client.connected()) {
        client.loop();
    }
}

#endif
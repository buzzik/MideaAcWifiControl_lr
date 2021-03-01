// version: 1.01
// #define DEBUG_ENABLE  //uncomment to get debug messages
#ifdef DEBUG_ENABLE
#define DEBUG(x) Serial.println(x)
#else
#define DEBUG(x)
#endif
#ifndef UNIT_TEST
#include <Arduino.h>
#endif
#include <Adafruit_Sensor.h>
#include <ArduinoOTA.h>
#include <DHT_U.h>
#include <ESP8266WiFi.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <MQTTClient.h>
#include <SPI.h>
#include <WiFiUdp.h>
#include <dht.h>
#include <ir_Coolix.h>
#include "GyverTimer.h"
#include "Credentials.h"

#define DHTTYPE DHT22  // DHT 22  (AM2302), AM2321
#define DHTPIN 3       //RX  Digital pin connected to the DHT sensor
// Feather HUZZAH ESP8266 note: use pins 3 RX, 4 D2, 5 D1, 12, 13 or 14 --
// Pin 15 can work but DHT must be disconnected during program upload.
#define IRPIN 4        // ESP8266 GPIO pin to use. Recommended: 4 (D2).
#define DEVICE_IP 213  //only last part, the first is 192.168.31.* it can be changed later
#define ROOM_NAME "livingroom"
int dhtDelay = 10000;
GTimer dhtTimer(MS, dhtDelay);
WiFiUDP ntpUDP;
WiFiClient WiFiclient;
MQTTClient mqttClient;
DHT SleepingRoomdht(DHTPIN, DHTTYPE);
const uint16_t kIrLed = IRPIN;
IRCoolixAC ac(kIrLed);  /// Set the GPIO used for sending messages.

String acModePayload = "off";
String acFanPayload = "medium";
uint8_t fanMode = kCoolixFan;
uint8_t acMode = kCoolixCool;
bool acPower = false;
int acTemp = 24;

// IRsend irsend(kIrLed);  // Set the GPIO to be used to sending the message.
uint64_t StrToHex(const char* str) {
    return (uint64_t)strtoull(str, 0, 16);
}
void printState() {  // Display the settings.
    DEBUG(" A/C remote is in the following state:");
    DEBUG("  %s\n");
    DEBUG(ac.toString().c_str());
}
// https://stackoverflow.com/questions/9072320/split-string-into-string-array
String getValue(String data, char separator, int index) {
    int found = 0;
    int strIndex[] = {0, -1};
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i + 1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void connect() {
    mqttClient.connect(ROOM_NAME "RoomAC", mqttName, mqttPassword);
    DEBUG("\nconnected!");
    mqttClient.subscribe("/" ROOM_NAME "/ac/mode/set");
    mqttClient.subscribe("/" ROOM_NAME "/ac/temperature/set");
    mqttClient.subscribe("/" ROOM_NAME "/ac/fan/set");
    mqttClient.subscribe("/" ROOM_NAME "/ac/swing/set");
    mqttClient.subscribe("/" ROOM_NAME "/conditioner/command");
}
void messageReceived(String& topic, String& payload) {
    DEBUG("incoming mqtt: " + topic + " - " + payload);

    if (topic == "/" ROOM_NAME "/conditioner/command") {
        String commandPower = getValue(payload, ';', 0);
        DEBUG("commandPower - " + commandPower);
        String commandTemperature = getValue(payload, ';', 1);
        DEBUG("commandTemperature - " + commandTemperature);
        String commandFan = getValue(payload, ';', 2);
        DEBUG("commandFan - " + commandFan);
        String commandMode = getValue(payload, ';', 3);
        DEBUG("commandMode - " + commandMode);

        if (commandPower == "OFF") {
            ac.setPower(false);
            ac.send();
            return;
        }
        switch (commandFan.toInt()) {
            case 0:
                fanMode = kCoolixFanMin;
                break;
            case 1:
                fanMode = kCoolixFanMed;
                break;
            case 2:
                fanMode = kCoolixFanMax;
                break;
            case 3:
                fanMode = kCoolixFanAuto;
                break;
        }
        switch (commandMode.toInt()) {
            case 0:
                acMode = kCoolixCool;
                break;
            case 1:
                acMode = kCoolixDry;
                break;
            case 2:
                acMode = kCoolixAuto;
                break;
            case 3:
                acMode = kCoolixHeat;
                break;
            case 4:
                acMode = kCoolixFan;
                break;
        }
        ac.clearSensorTemp();
        ac.setTemp(commandTemperature.toInt());
        ac.setMode(acMode);
        ac.setFan(fanMode);
        ac.send();
        return;
    } else {
        if (topic == "/" ROOM_NAME "/ac/mode/set") {
            acModePayload = payload;
            if (acModePayload == "off") {
                acPower = false;
                ac.setPower(acPower);
                ac.send();
                mqttClient.publish("/" ROOM_NAME "/ac/mode", acModePayload);
                return;
            } else {
                if (acModePayload == "cool") {
                    acMode = kCoolixCool;
                }
                if (acModePayload == "dry") {
                    acMode = kCoolixDry;
                }
                if (acModePayload == "auto") {
                    acMode = kCoolixAuto;
                }
                if (acModePayload == "heat") {
                    acMode = kCoolixHeat;
                }
                if (acModePayload == "fan_only") {
                    acMode = kCoolixFan;
                }
                acPower = true;
            }
        }
        if (topic == "/" ROOM_NAME "/ac/temperature/set") {
            acTemp = payload.toInt();
        }
        if (topic == "/" ROOM_NAME "/ac/fan/set") {
            if (acFanPayload == "low") {
                fanMode = kCoolixFanMin;
            }
            if (acFanPayload == "medium") {
                fanMode = kCoolixFanMed;
            }
            if (acFanPayload == "high") {
                fanMode = kCoolixFanMax;
            }
            if (acFanPayload == "auto") {
                fanMode = kCoolixFanAuto;
            }
        }
        if (topic == "/" ROOM_NAME "/ac/swing/set") {
            return;
        }
        ac.clearSensorTemp();
        ac.setPower(acPower);
        ac.setTemp(acTemp);
        ac.setMode(acMode);
        ac.setFan(fanMode);
        ac.send();
        mqttClient.publish("/" ROOM_NAME "/ac/temperature", (String)acTemp);
        mqttClient.publish("/" ROOM_NAME "/ac/fan", acFanPayload);
        mqttClient.publish("/" ROOM_NAME "/ac/mode", acModePayload);
    }
}
void setup() {
    ac.begin();
    Serial.begin(9600);
    delay(200);
    DEBUG("Connecting to ");
    DEBUG(ssid);
    SleepingRoomdht.begin();
    WiFi.begin(ssid, password);
    WiFi.mode(WIFI_STA);
    IPAddress ip(192, 168, 31, DEVICE_IP);
    IPAddress gateway(192, 168, 31, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.config(ip, gateway, subnet);
    delay(5000);
    DEBUG("begin NTP \nWiFi connected \n IP address: ");
    DEBUG(WiFi.localIP());
    DEBUG("connecting to MQTT broker...");
    mqttClient.begin("192.168.31.200", WiFiclient);
    mqttClient.onMessage(messageReceived);
    Serial.println("Default state of the remote.");
    printState();
    Serial.println("Setting initial state for A/C.");
    ac.setPower(false);
    ac.setFan(kCoolixFanMed);
    ac.setMode(kCoolixCool);
    ac.setTemp(22);
    printState();
    connect();
    // start the WiFi OTA library with internal (flash) based storage
    ArduinoOTA.begin();
}
void loop() {
    // check for WiFi OTA updates
    ArduinoOTA.handle();
    if (!mqttClient.connected()) {
        connect();
    }
    mqttClient.loop();

    if (dhtTimer.isReady()) {
        int sh = SleepingRoomdht.readHumidity();
        int st = SleepingRoomdht.readTemperature();
        DEBUG(F("SleepingRoomdht Humidity: "));
        DEBUG(sh);
        DEBUG(F("%  SleepingRoomdht Temperature: "));
        DEBUG(st);
        mqttClient.publish("/" ROOM_NAME "/humidity/state", (String)sh);
        mqttClient.publish("/" ROOM_NAME "/temp/state", (String)st);
        mqttClient.publish("/" ROOM_NAME "/temperature", (String)st);
    }
}

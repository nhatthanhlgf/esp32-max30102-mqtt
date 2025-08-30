#include <WiFi.h>
#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

//  WiFi 
const char* ssid     = "Thanh";
const char* password = "12345678";

//  HiveMQ Cloud 
const char* mqtt_server   = "8e166eff2d18486eacf7c8b613686d0c.s1.eu.hivemq.cloud";
const int   mqtt_port     = 8883;
const char* mqtt_username = "hivemq.webclient.1756537217940";
const char* mqtt_password = "B.7eXq*&Jvw6$18WaUzR";

WiFiClientSecure espClient;
PubSubClient client(espClient);

//  MAX30102 
MAX30105 particleSensor;
#define MAX_BRIGHTNESS 255

uint32_t irBuffer[100];
uint32_t redBuffer[100];
int32_t bufferLength;
int32_t spo2;
int8_t validSPO2;
int32_t heartRate;
int8_t validHeartRate;


#define LED_PIN 2  // LED on-board ESP32
bool measureEnabled = false;  // trạng thái đo

//  WiFi 
void setup_wifi() {
  Serial.print("Connecting to WiFi ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected. IP:");
  Serial.println(WiFi.localIP());
}

//  MQTT Callback 
void callback(char* topic, byte* payload, unsigned int length) {
  String inMessage = "";
  for (int i = 0; i < length; i++) {
    inMessage += (char)payload[i];
  }

  Serial.println("MQTT Message [" + String(topic) + "]: " + inMessage);

  if (String(topic) == "esp32/control") {
    if (inMessage == "LED_ON") {
      digitalWrite(LED_PIN, HIGH);
      measureEnabled = true;   // bật đo
      Serial.println(">>> LED bật + Bắt đầu đo");
    }
    else if (inMessage == "LED_OFF") {
      digitalWrite(LED_PIN, LOW);
      measureEnabled = false;  // dừng đo
      Serial.println(">>> LED tắt + Ngừng đo");
    }
  }
}

//  MQTT reconnect 
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientID = "ESP32Client-";
    clientID += String(random(0xffff), HEX);

    if (client.connect(clientID.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("connected!");
      client.subscribe("esp32/control");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" => retry in 5s");
      delay(5000);
    }
  }
}

//  Publish 
void publishMessage(const char* topic, String payload, boolean retained) {
  if (client.publish(topic, payload.c_str(), retained)) {
    Serial.println("Published [" + String(topic) + "]: " + payload);
  }
}

// Setup 
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  setup_wifi();

  espClient.setInsecure(); 
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("MAX30102 not found. Check wiring!");
    while (1);
  }

  byte ledBrightness = 60;
  byte sampleAverage = 4;
  byte ledMode = 2;
  int sampleRate = 100;
  int pulseWidth = 411;
  int adcRange = 4096;
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
}

//  Loop 
void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  if (measureEnabled) {
    bufferLength = 100;
    for (byte i = 0; i < bufferLength; i++) {
      while (!particleSensor.available()) particleSensor.check();
      redBuffer[i] = particleSensor.getRed();
      irBuffer[i] = particleSensor.getIR();
      particleSensor.nextSample();
    }

    maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer,
                                          &spo2, &validSPO2, &heartRate, &validHeartRate);

    if (validHeartRate && validSPO2) {
      DynamicJsonDocument doc(256);
      doc["heartRate"] = heartRate;
      doc["spo2"] = spo2;
      char mqtt_message[128];
      serializeJson(doc, mqtt_message);

      publishMessage("esp32/data", mqtt_message, true);

      Serial.printf("Heart Rate: %d bpm | SpO2: %d %%\n", heartRate, spo2);
    }

    delay(2000);  
  }
}

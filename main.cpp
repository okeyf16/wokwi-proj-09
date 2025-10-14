#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- PASTE YOUR CREDENTIALS HERE ---
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";
const char* IOT_HUB_HOST = "hub30906.azure-devices.net";
const char* DEVICE_ID = "Wokwinode01";
const char* SAS_TOKEN = "SharedAccessSignature sr=hub30906.azure-devices.net%2Fdevices%2FWokwinode01&sig=Uq39JNfa4HpUb9q65VSa%2Fwt%2FfNfGnoWdkYfre33kMxs%3D&se=1785619149";

// --- Pin Definitions & Constants ---
#define LED_PIN 2
const long TELEMETRY_INTERVAL = 27000;
const char* AZURE_TELEMETRY_TOPIC = "devices/Wokwinode01/messages/events/";
const char* AZURE_METHODS_SUB_TOPIC = "$iothub/methods/POST/#";

// --- Global Variables ---
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);
unsigned long lastTelemetryMillis = 0;
bool generatorState = false;

// --- Function Prototypes ---
void connectWifi();
void connectAzure();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void sendTelemetry();
// Function renamed for clarity
void handleSetStateCommand(String requestId, byte* payload, unsigned int length);

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  connectWifi();

  wifiClient.setInsecure(); 
  mqttClient.setServer(IOT_HUB_HOST, 8883);
  mqttClient.setCallback(mqttCallback);
}

void loop() {
  if (!mqttClient.connected()) {
    connectAzure();
  }
  mqttClient.loop();

  if (millis() - lastTelemetryMillis > TELEMETRY_INTERVAL) {
    lastTelemetryMillis = millis();
    sendTelemetry();
  }
}

void connectWifi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
}

void connectAzure() {
  String username = String(IOT_HUB_HOST) + "/" + String(DEVICE_ID) + "/?api-version=2021-04-12";
  Serial.println("Connecting to Azure IoT Hub...");
  while (!mqttClient.connect(DEVICE_ID, username.c_str(), SAS_TOKEN)) {
    Serial.print("Failed, rc=");
    Serial.print(mqttClient.state());
    Serial.println(" - Retrying in 5 seconds...");
    delay(5000);
  }
  Serial.println("Successfully connected to Azure IoT Hub!");
  mqttClient.subscribe(AZURE_METHODS_SUB_TOPIC);
  Serial.println("Subscribed to Direct Method topic.");
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message received on topic: ");
  Serial.println(topic);
  String topicStr(topic);
  if (topicStr.startsWith("$iothub/methods/POST/")) {
    topicStr.replace("$iothub/methods/POST/", "");
    int slashIndex = topicStr.indexOf("/");
    if (slashIndex != -1) {
      String methodName = topicStr.substring(0, slashIndex);
      String requestId = topicStr.substring(topicStr.indexOf("?$rid=") + 6);
      
      Serial.print("Received Direct Method: '");
      Serial.print(methodName);
      Serial.println("'");
      
      if (methodName == "toggle") {
        // Call the renamed function
        handleSetStateCommand(requestId, payload, length);
      } else {
        String responseTopic = "$iothub/methods/res/404/?$rid=" + requestId;
        mqttClient.publish(responseTopic.c_str(), "{\"result\":\"Unknown method\"}");
      }
    }
  }
}

// Renamed from handleToggleCommand for clarity
void handleSetStateCommand(String requestId, byte* payload, unsigned int length) {
  StaticJsonDocument<256> doc;
  deserializeJson(doc, payload, length);

  const char* desiredState = doc["payload"]["state"];

  Serial.print("Desired state received: ");
  Serial.println(desiredState);

  // Set the generator state based on the payload, not just a toggle
  if (strcmp(desiredState, "on") == 0) {
    generatorState = true;
  } else if (strcmp(desiredState, "off") == 0) {
    generatorState = false;
  }
  
  digitalWrite(LED_PIN, generatorState);
  Serial.print("Generator state set to: ");
  Serial.println(generatorState ? "ON" : "OFF");

  String responseTopic = "$iothub/methods/res/200/?$rid=" + requestId;
  String responsePayload = "{\"result\":\"success\",\"newState\":\"" + String(generatorState ? "on" : "off") + "\"}";
  mqttClient.publish(responseTopic.c_str(), responsePayload.c_str());
  Serial.println("Sent command response to Azure.");
}

void sendTelemetry() {
  StaticJsonDocument<200> doc;
  doc["PartitionKey"] = DEVICE_ID;
  doc["fuelLevel"] = random(20, 95);
  doc["outputVoltage"] = random(2350, 2450) / 10.0;
  doc["temperature"] = random(60, 85);
  doc["generatorStatus"] = generatorState ? "ON" : "OFF";
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);
  if (mqttClient.publish(AZURE_TELEMETRY_TOPIC, jsonBuffer)) {
    Serial.print("Telemetry sent: ");
    Serial.println(jsonBuffer);
  } else {
    Serial.println("Failed to send telemetry.");
  }
}

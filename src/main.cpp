#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <RCSwitch.h>
#include <secrets.h>

#define MQTT_VERSION                                MQTT_VERSION_3_1_1

const char* MQTT_CLIENT_ID =                "livingroom_rc";
const char* MQTT_SENSOR_TOPIC =             "/house/livingroom/sensor_dht";
const char* MQTT_LIGHT_STATE_TOPIC[] = {    "/house/livingroom/light_left/status", "/house/livingroom/light_right/status", "/house/livingroom/light_center/status" };
const char* MQTT_LIGHT_COMMAND_TOPIC[] = {  "/house/livingroom/light_left/switch", "/house/livingroom/light_right/switch", "/house/livingroom/light_center/switch" };
const char* MQTT_MOVEMENT_STATE_TOPIC =     "/house/livingroom/movement/status";

RCSwitch mySwitch = RCSwitch();
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

int IRB =                                           14;
int RCPIN =                                         2;
char* housecode =                                   "11010"; //first 5 dip switches on rc switches
char* socketcodes[] = {                             "00010", "00001", "10000" }; //last 5 dip switches on rc switches
unsigned long wait = 300000;
unsigned long now;
unsigned long lowIn;
bool lockLow = true;
bool lockHigh;
bool takeLowTime;

void mqttReconnect() {
  while (!mqttClient.connected()) { //loop until we're reconnected
    Serial.println("[MQTT] INFO: Attempting connection...");
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("[MQTT] INFO: connected");
      for (int i = 0; i < 3; i++) {
        mqttClient.subscribe(MQTT_LIGHT_COMMAND_TOPIC[i]); //subscribe to all light topics
        Serial.print("[MQTT] INFO: subscribing to: ");
        Serial.println(MQTT_LIGHT_COMMAND_TOPIC[i]);
      }
    } else {
      Serial.print("[MQTT] ERROR: failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println("[MQTT] DEBUG: try again in 5 seconds");
      delay(2000); //wait 5 seconds before retrying
    }
  }
}

void wifiSetup() {
  Serial.print("[WIFI] INFO: Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD); //connect to wifi
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("[WIFI] INFO: WiFi connected");
  Serial.println("[WIFI] INFO: IP address: ");
  Serial.println(WiFi.localIP());
}

void mqttCallback(char* p_topic, byte* p_payload, unsigned int p_length) { //handle mqtt callbacks
  String payload;
  for (uint8_t i = 0; i < p_length; i++) { //concatenate payload
    payload.concat((char)p_payload[i]);
  }

  for (int i = 0; i < 3; i++) {
    if (String(MQTT_LIGHT_COMMAND_TOPIC[i]).equals(String(p_topic))) {
      Serial.print("[MQTT] TOPIC: ");
      Serial.println(p_topic);
      Serial.print("[MQTT] PAYLOAD:");
      Serial.println(payload);

      if (payload=="ON") {
        mySwitch.switchOn(housecode, socketcodes[i]);
        mqttClient.publish(MQTT_LIGHT_STATE_TOPIC[i], "ON", true);
      }
      else {
        mySwitch.switchOff(housecode, socketcodes[i]);
        mqttClient.publish(MQTT_LIGHT_STATE_TOPIC[i], "OFF", true);
      }
    }
  }
}

void movement() {
  if (digitalRead(IRB) == HIGH) {
    if (lockLow) {
      mqttClient.publish(MQTT_MOVEMENT_STATE_TOPIC, "ON", true); //publish the state to mqtt
      Serial.println("[SENSOR] INFO: Movement detected!");
      lockLow = false;
      delay(50);
      }
    takeLowTime = true;
    }
  if (digitalRead(IRB) == LOW) {
    if (takeLowTime) {
      lowIn = millis();
      takeLowTime = false;
    }
    if (!lockLow && millis() - lowIn >= wait) {
      mqttClient.publish(MQTT_MOVEMENT_STATE_TOPIC, "OFF", true); //publish the state to mqtt
      Serial.println("[SENSOR] INFO: No more movement detected!");
      lockLow = true;
    }
  }
}

void setup() {
  Serial.begin(115200);
  ArduinoOTA.setHostname("rc-livingroom");
  ArduinoOTA.onStart([]() {
    String type;
    type = "sketch";
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  mySwitch.enableTransmit(RCPIN); //enable transmit on RCPIN
  wifiSetup();
  mqttClient.setServer(MQTT_SERVER_IP, MQTT_SERVER_PORT);
  mqttClient.setCallback(mqttCallback);
}

void loop() {
  yield();
  ArduinoOTA.handle();
  if (!mqttClient.connected()) {
    mqttReconnect();
  }
  mqttClient.loop();
  movement();
}

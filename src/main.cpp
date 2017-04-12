#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <RCSwitch.h>
#include <fauxmoESP.h>
#include <secrets.h>

#define MQTT_VERSION                                MQTT_VERSION_3_1_1
#define IRB 14
#define RCPIN 2

const PROGMEM char* mqttClient_ID =                "livingroom_rc";
const PROGMEM char* MQTT_SENSOR_TOPIC =             "/house/livingroom/sensor_dht";
const PROGMEM char* MQTT_LIGHT_STATE_TOPIC[] = {    "/house/livingroom/light_left/status", "/house/livingroom/light_right/status", "/house/livingroom/light_center/status" };
const PROGMEM char* MQTT_LIGHT_COMMAND_TOPIC[] = {  "/house/livingroom/light_left/switch", "/house/livingroom/light_right/switch", "/house/livingroom/light_center/switch" };
const PROGMEM char* MQTT_MOVEMENT_STATE_TOPIC =     "/house/livingroom/movement/status";

const char* housecode =                             "11010"; //first 5 dip switches on rc switches
const char* socketcodes[] = {                       "00010", "00001", "10000" }; //last 5 dip switches on rc switches

unsigned long wait = 30000;
unsigned long wait_off = 120000;
unsigned long now = millis();
unsigned long last_millis = 0;

int lastState = LOW;

//initialize classes
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
RCSwitch mySwitch = RCSwitch();
fauxmoESP fauxmo;

void fauxmoCallback(unsigned char device_id, const char * device_name, bool state) {
  Serial.printf("[FAUXMO] Device #%d (%s) state: %s\n", device_id, device_name, state ? "1" : "0");
  mqttClient.publish(MQTT_LIGHT_STATE_TOPIC[device_id], state ? "1" : "0", true);

  int hc_int = atol(housecode);
  int sc_int = atol(socketcodes[device_id]);

  if (state) {
    mySwitch.switchOn(hc_int, sc_int);
  }
  else {
    mySwitch.switchOff(hc_int, sc_int);
  }
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
      fauxmoCallback(i, p_topic, payload != "0");
    }
  }
}

void mqttReconnect() {
  while (!mqttClient.connected()) { //loop until we're reconnected
    Serial.println("[MQTT] INFO: Attempting connection...");
    if (mqttClient.connect(mqttClient_ID, MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("[MQTT] INFO: connected");
      for (int i = 0; i < 3; i++) {
        mqttClient.subscribe(MQTT_LIGHT_COMMAND_TOPIC[i]); //subscribe to all light topics
        Serial.printf("[MQTT] INFO: subscribing to %s\n", MQTT_LIGHT_COMMAND_TOPIC[i]);
      }
    } else {
      Serial.print("[MQTT] ERROR: failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println("[MQTT] DEBUG: try again in 5 seconds");
      delay(5000); //wait 5 seconds before retrying
    }
  }
}

void wifiSetup() {
  Serial.print("[WIFI] INFO: Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD); //connect to wifi
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("[WIFI] INFO: WiFi connected");
  Serial.println("[WIFI] INFO: IP address: ");
  Serial.println(WiFi.localIP());
}

void fauxmoSetup() {
  fauxmo.addDevice("Licht eins");
  fauxmo.addDevice("Licht zwei");
  fauxmo.addDevice("Licht drei");
  fauxmo.onMessage(fauxmoCallback);
}

void movement(unsigned long now) {
  if (digitalRead(IRB) == HIGH) {
    if (now - last_millis >= wait) {
      last_millis = now;
      mqttClient.publish(MQTT_MOVEMENT_STATE_TOPIC, "ON", true); //publish the state to mqtt
      Serial.println("INFO: Movement detected!");
    }
    lastState = HIGH;
  }
  if (digitalRead(IRB) == LOW) {
    if (now - last_millis >= wait_off) {
      last_millis = now;
      mqttClient.publish(MQTT_MOVEMENT_STATE_TOPIC, "OFF", true); //publish the state to mqtt
      Serial.println("INFO: No more movement detected!");
    }
    lastState = LOW;
  }
}

void setup() {
  Serial.begin(115200);
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
  fauxmoSetup();
  mqttClient.setServer(MQTT_SERVER_IP, MQTT_SERVER_PORT);
  mqttClient.setCallback(mqttCallback);

}

void loop() {
  yield();
  unsigned long now = millis();
  if (!mqttClient.connected()) {
    mqttReconnect();
  }
  mqttClient.loop();
  fauxmo.handle();
  movement(now);
  ArduinoOTA.handle();
}

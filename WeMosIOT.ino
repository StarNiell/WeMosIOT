#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <PubSubClient.h>

ESP8266WebServer server(80);
WiFiClient espClient;
PubSubClient mqtt_client(espClient);

const bool SENSOR_ENABLED      = false;
const bool SWITCH_ENABLED      = true;
const bool BUTTON_ENABLED      = true;
const bool CONTACT_ENABLED     = false;
const bool INFRARED_ENABLED    = false;

const uint16_t PIN_ONOFF      = 4;  //D2
const uint16_t PIN_CONTACT    = 0;  //D3
const uint16_t PIN_MANUAL     = 14; //D5
const uint16_t PIN_LED_RED    = 16; //D0
const uint16_t PIN_LED_GREEN  = 5;  //D1
const uint16_t PIN_MODE       = 15; //D8 (Pulldown)

bool lastConnectionStatus = false;
String sid = "StarNiell WiFi";
String pwd = "abcd1234";
const char *sidAP = "WD1R2-REMOTE";
const char *pwdAP = "12345678";
uint8_t iMode = 0;
bool lastContactState = false;

// Json Configuration variables
struct Config {
  char wifi_ssid[64];
  char wifi_pwd[16];
  char mqtt_server[64];
  int mqtt_port;
  char mqtt_username[64];
  char mqtt_pwd[64];
  char device_uid[16];
  char topic_sub[64];
  char topic_pub[64];
};

const char *typeSwitch = "switch";
const char *typeSensor = "sensor";
const char *typeInfrar = "infrar";
const char *typeButton = "button";
const char *typeContact = "contact";
const char *statusOK = "ok";
const char *statusKO = "ko";

const char *config_filename = "/store/config.json";  // <- SD library uses 8.3 filenames
Config config; 

String pub_payload = "{\"device_id\": \"<type>-<device_id>\", \"timestamp\": \"<timestamp>\", \"type\": \"<type>\", \"value\": <value>, \"unit\": \"<unit>\", \"subscription\": \"<subscription>\" , \"status\": \"<status>\" }";
String pub_topic = "<topic>/<type>-<device_id>";

unsigned long sensorInterval = 60; //Seconds between 
unsigned long sensorStartTime = millis();
unsigned long sensorElapsedTime = 0;

// Loads the configuration from a file
void loadConfiguration(const char *filename, Config &config) {
  // Open file for reading

  Serial.println(filename);
  File file = LittleFS.open(filename, "r");

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  Serial.println("before JsonDocument doc");
  JsonDocument doc;

  // Deserialize the JSON document
  Serial.println("before DeserializationError");
  DeserializationError error = deserializeJson(doc, file);
  if (error)
    Serial.println(F("Failed to read file, using default configuration"));

  // Copy values from the JsonDocument to the Config
  //config.mqtt_port = doc["mqtt-port"] | 2731;

  strlcpy(config.wifi_ssid,     doc["wifi-ssid"]      | "<YOUR WIFI SSID>", sizeof(config.wifi_ssid));
  strlcpy(config.wifi_pwd,      doc["wifi-pwd"]       | "<YOUR WIFI PWD>", sizeof(config.wifi_pwd));
  strlcpy(config.mqtt_server,   doc["mqtt-server"]    | "<MQTT SERVER>", sizeof(config.mqtt_server));
  config.mqtt_port            = doc["mqtt-port"]      | 1883;
  strlcpy(config.mqtt_username, doc["mqtt-username"]  | "<MQTT USERNAME>", sizeof(config.mqtt_username));
  strlcpy(config.mqtt_pwd,      doc["mqtt-pwd"]       | "<MQTT PWD>", sizeof(config.mqtt_pwd));
  strlcpy(config.device_uid,    doc["device-uid"]     | "999", sizeof(config.device_uid));
  strlcpy(config.topic_sub,     doc["topic-sub"]      | "iotx/in", sizeof(config.topic_sub));
  strlcpy(config.topic_pub,     doc["topic-pub"]      | "iotx/out", sizeof(config.topic_pub));

  // Close the file (Curiously, File's destructor doesn't close the file)
  file.close();
}

// Saves the configuration to a file
bool saveConfiguration(const char *filename, const Config &config) {
  // Delete existing file, otherwise the configuration is appended to the file
  LittleFS.remove(filename);

  // Open file for writing
  File file = LittleFS.open(filename, "w");
  if (!file) {
    Serial.println(F("Failed to create file"));
    return false;
  }

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/assistant to compute the capacity.
  JsonDocument doc;

  // Set the values in the document
  doc["wifi-ssid"]      = config.wifi_ssid; 
  doc["wifi-pwd"]       = config.wifi_pwd;
  doc["mqtt-server"]    = config.mqtt_server;
  doc["mqtt-port"]      = config.mqtt_port;
  doc["mqtt-username"]  = config.mqtt_username;
  doc["mqtt-pwd"]       = config.mqtt_pwd;
  doc["device-uid"]     = config.device_uid;
  doc["topic-sub"]      = config.topic_sub;
  doc["topic-pub"]      = config.topic_pub;

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write to file"));
  }

  // Close the file
  file.close();

  GenerateIndexHtml(config);

  return true;
}

void GenerateIndexHtml(const Config &config) {
  String buffer = "";
  File file = LittleFS.open("/index.static", "r");
  while(file.available()){
    buffer += file.readString();
  } 
  file.close();

  Serial.println(buffer);

  char mqtt_port_str[6];
  itoa(config.mqtt_port, mqtt_port_str, 10);

  buffer.replace("wifi_ssid_value",     config.wifi_ssid);
  buffer.replace("wifi_pwd_value",      config.wifi_pwd);
  buffer.replace("mqtt_server_value",   config.mqtt_server);
  buffer.replace("mqtt_port_value",     mqtt_port_str);
  buffer.replace("mqtt_user_value",     config.mqtt_username);
  buffer.replace("mqtt_pwd_value",      config.mqtt_pwd);
  buffer.replace("mqtt_deviceid_value", config.device_uid);
  buffer.replace("topic_sub_value",     config.topic_sub);
  buffer.replace("topic_pub_value",     config.topic_pub);
 
  file = LittleFS.open("/index.html", "w");
  file.print(buffer);
  delay(500);
  //Close the file
  file.close();  

  Serial.println("file index saved");
  Serial.println("");
  Serial.println(config.wifi_ssid);
  Serial.println("");
  Serial.println("");

  Serial.print(buffer);


}

void setupAP() {
  Serial.println("Entering AP Mode...");
  WiFi.softAP(sidAP, pwdAP);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  Serial.print("SSID: ");
  Serial.println(sidAP);
  Serial.print("PWD : ");
  Serial.println(pwdAP);

  setupWebServer();
}

char * getPayload(char *device_id, const char *type, float value, const char *unit, int decimals)
{
  String payload = pub_payload;
  char timestamp[] = "0000-00-00T00:00:00Z";

  payload.replace("<device_id>", device_id);
  payload.replace("<timestamp>", timestamp);
  payload.replace("<type>", type);
  
  char value_str[10];
  //itoa(value, value_str, 10);
  dtostrf(value, 6, decimals, value_str);

  payload.replace("<value>", value_str);

  payload.replace("<unit>", unit);
  payload.replace("<subscription>", config.topic_sub);
  payload.replace("<status>", "ok");

  int str_len = payload.length() + 1;
  static char payload_char[256];
  payload.toCharArray(payload_char, str_len);

  return payload_char;
}

char * getPubTopic(char *device_id, const char *type) {
  String pubTopic = pub_topic;
  pubTopic.replace("<topic>", config.topic_pub);
  pubTopic.replace("<type>", type);
  pubTopic.replace("<device_id>", device_id);

  int str_len = pubTopic.length() + 1;
  static char pubTopic_char[128];
  pubTopic.toCharArray(pubTopic_char, str_len);

  Serial.print("pubTopic.toCharArray");
  Serial.println(pubTopic_char);

  return pubTopic_char;
}

float getSensorValue() 
{
    return (random(0, 600) / 10); 
}

int getContactValue()
{
  return digitalRead(PIN_CONTACT);
}

void setActionOnOff(int value)
{
  char *payload = getPayload(config.device_uid, typeSwitch, (float)value, "state", 0);
  char *pubTopic = getPubTopic(config.device_uid, typeSwitch);

  Serial.print("pubTopic: ");
  Serial.println(pubTopic);

  if (value > 0)
  {
    digitalWrite(PIN_ONOFF, HIGH);
    digitalWrite(PIN_LED_RED, HIGH);
    Serial.println("Set Action to ON");
  }
  else
  {
    digitalWrite(PIN_ONOFF, LOW);
    digitalWrite(PIN_LED_RED, LOW);
    Serial.println("Set Action to OFF");
  }

  if (mqtt_client.connected())
  {
    mqtt_client.publish(pubTopic, payload, true);
  }
}

void setActionInfrar()
{

  char *payload = getPayload(config.device_uid, typeInfrar, 1.0f, "state", 0);
  char *pubTopic = getPubTopic(config.device_uid, typeInfrar);

  //Send feedback
  if (mqtt_client.connected())
  {
    mqtt_client.publish(pubTopic, payload, true);
  }

  //TODO: Not yet implemented
  delay(1000);

  //Send feedback
  payload = getPayload(config.device_uid, typeInfrar, 0.0f, "state", 0);

  //Send feedback
  if (mqtt_client.connected())
  {
    mqtt_client.publish(pubTopic, payload, true);
  }
}

void setActionSensor(int value)
{

  if (value > 1)
  {
    sensorInterval = value;
  }

  float sensorValue = getSensorValue(); 

  char *payload = getPayload(config.device_uid, typeSensor, sensorValue, "°C", 1);
  char *pubTopic = getPubTopic(config.device_uid, typeSensor);
  
  //Send feedback
  if (mqtt_client.connected())
  {
    mqtt_client.publish(pubTopic, payload, true);
  }

}

void setActionContact(int value)
{

  char *payload = getPayload(config.device_uid, typeContact, float(value), "state", 0);
  char *pubTopic = getPubTopic(config.device_uid, typeContact);
  
  //Send feedback
  if (mqtt_client.connected())
  {
    mqtt_client.publish(pubTopic, payload, true);
  }

}

void setActionButton()
{
  char *payload = getPayload(config.device_uid, typeButton, 1.0f, "state", 0);
  char *pubTopic = getPubTopic(config.device_uid, typeButton);

  digitalWrite(PIN_ONOFF, HIGH);
  digitalWrite(PIN_LED_RED, HIGH);
  Serial.println("Set setActionButton to ON");

  //Send feedback
  if (mqtt_client.connected())
  {
    mqtt_client.publish(pubTopic, payload, true);
  }

  delay(1000);

  payload = getPayload(config.device_uid, typeButton, 0.0f, "state", 0);

  digitalWrite(PIN_ONOFF, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  Serial.println("Set setActionButton to OFF");

  //Send feedback
  if (mqtt_client.connected())
  {
    mqtt_client.publish(pubTopic, payload, true);
  }
}

void sendInitialStatusRetained()
{
  char *payload = "";
  char *pubTopic = "";

  Serial.println("sendInitialStatusRetained()");

  //Switch
  if (SWITCH_ENABLED)
  {
    payload = getPayload(config.device_uid, typeSwitch, 0.0f, "state", 0);
    pubTopic = getPubTopic(config.device_uid, typeSwitch);
    if (mqtt_client.connected())
    {
      mqtt_client.publish(pubTopic, payload, true);
    }
  }

  //Button
  if (BUTTON_ENABLED)
  {
    payload = getPayload(config.device_uid, typeButton, 0.0f, "state", 0);
    pubTopic = getPubTopic(config.device_uid, typeButton);
    if (mqtt_client.connected())
    {
      mqtt_client.publish(pubTopic, payload, true);
    }
  }

  //Sensor
  if (SENSOR_ENABLED)
  {
    float sensorValue = getSensorValue(); 
    payload = getPayload(config.device_uid, typeSensor, sensorValue, "°C", 1);
    pubTopic = getPubTopic(config.device_uid, typeSensor);
    if (mqtt_client.connected())
    {
      mqtt_client.publish(pubTopic, payload, true);
      Serial.print("Sensor payload: ");
      Serial.println(payload);
    }
  }

  //Contact
  if (CONTACT_ENABLED)
  {
    int contactValue = getContactValue()==1 ? 0 : 1; 
    lastContactState = (getContactValue() == HIGH);
    payload = getPayload(config.device_uid, typeContact, float(contactValue), "state", 0);
    pubTopic = getPubTopic(config.device_uid, typeContact);
    if (mqtt_client.connected())
    {
      mqtt_client.publish(pubTopic, payload, true);
      Serial.print("Contact payload: ");
      Serial.println(payload);
    }
  }

  //Infrared
  if (INFRARED_ENABLED)
  {
    //TODO: Feature missing
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  char received_topic[128];
  char received_payload[128];
  strcpy(received_topic, topic);
  memcpy(received_payload, payload, length);

  /*
  Serial.print("incoming: ");
  Serial.println(received_topic);
  Serial.print("Message:");
  Serial.println(received_payload);
  Serial.println("-----------------------");
  */

  char device_id[16];
  char idx[16];
  int value = -1;
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, received_payload);
  if (error)
    Serial.println(F("Failed to read payload, using default configuration"));

  strlcpy(device_id, doc["device_id"] | "", sizeof(device_id));
  value = doc["value"] | -1;
  Serial.print("device_id: ");
  Serial.print(device_id);
  Serial.print(" - value: ");
  Serial.println(value);

  //find device_id
  char * idxfind = strstr(device_id, config.device_uid);
  
  if (idxfind)
  {
    Serial.println("device_id == config.device_uid.... OK");
    
    //Switch
    if (strstr(device_id, typeSwitch) && value >= 0)
    {
      Serial.println("Type is: switch");
      setActionOnOff(value);
    }
    //Infrared
    else if (strstr(device_id, typeInfrar))
    {
      Serial.println("Type is: infrar");
      setActionInfrar();
    }
    //Button
    else if (strstr(device_id, typeButton))
    {
      Serial.println("Type is: button");
      setActionButton();
    }
    //Sensor
    else if (strstr(device_id, typeSensor))
    {
      Serial.println("Type is: sensor");
      setActionSensor(value);
    }
  }

  // Note: Do not use the client in the callback to publish, subscribe or
  // unsubscribe as it may cause deadlocks when other things arrive while
  // sending and receiving acknowledgments. Instead, change a global variable,
  // or push to a queue and handle it in the loop after calling `client.loop()`.
}

void wifiConnect() {
  Serial.println("WiFi setup...");
  Serial.print("WiFi ssid: ");
  Serial.println(config.wifi_ssid);
  Serial.print("WiFi pwd: ");
  Serial.println(config.wifi_pwd);

  WiFi.begin(config.wifi_ssid, config.wifi_pwd);

  Serial.print("checking wifi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }

  Serial.println("\nWiFi connected.");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP().toString());
}

void setupMqtt() {

  wifiConnect();

  mqtt_client.setServer(config.mqtt_server, config.mqtt_port);
  mqtt_client.setCallback(mqttCallback);
}

void mqttConnect() {
  while(!mqtt_client.connected())
  {
    if (mqtt_client.connect(config.device_uid, config.mqtt_username, config.mqtt_pwd)) {
        Serial.print("Connected to MQTT broker as Client: ");
        Serial.println(config.device_uid);
        mqtt_client.subscribe(config.topic_sub);
        Serial.print("Subscribed: ");
        Serial.println(config.topic_sub);
        
        // Publish message upon successful connection
        sendInitialStatusRetained();
    }
    else
    {
      Serial.print(".");
      delay(1000);
    }
  }
}

void setupWebServer() {
  server.serveStatic("/", LittleFS, "/", "no-cache, no-store, must-revalidate");
  
  server.onNotFound([](){ 
    server.send(404, "text/plain", "Landing page not found! you can chose: 'index.html' or 'config.html!");  
  });
 
  server.on("/saveconfig", HTTP_POST, []() {
    strcpy(config.wifi_ssid,     server.arg("wifi_ssid").c_str());
    strcpy(config.wifi_pwd,      server.arg("wifi_pwd").c_str());
    strcpy(config.device_uid,    server.arg("mqtt_deviceid").c_str());
    strcpy(config.mqtt_server,   server.arg("mqtt_server").c_str());
    config.mqtt_port =           atoi(server.arg("mqtt_port").c_str());
    strcpy(config.mqtt_username, server.arg("mqtt_user").c_str());
    strcpy(config.mqtt_pwd,      server.arg("mqtt_pwd").c_str());
    strcpy(config.topic_sub,     server.arg("topic_sub").c_str());
    strcpy(config.topic_pub,     server.arg("topic_pub").c_str());

    Serial.print("wfsid: ");
    Serial.println(config.wifi_ssid);
    Serial.print("wfpwd: ");
    Serial.println(config.wifi_pwd);

    if (saveConfiguration(config_filename, config))
    {
      server.send(200, "text/plane","IOT eXperience Configuration saved! Please reboot the system.");
    }
    else
    {
      server.send(500, "text/plane","Unable to save the IOT eXperience Configuration! Please reboot the system and retry.");
    }
  });
  server.begin();
  digitalWrite(PIN_LED_RED, HIGH);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_ONOFF, OUTPUT);
  pinMode(PIN_MANUAL, INPUT_PULLUP);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_MODE, INPUT_PULLDOWN_16);
  pinMode(PIN_CONTACT, INPUT_PULLUP);

  digitalWrite(PIN_LED_GREEN, HIGH);
  digitalWrite(PIN_LED_RED, HIGH);

  Serial.println("ESP starts");

  LittleFS.begin();

  delay(2000);

  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED, LOW);

  if (digitalRead(PIN_MODE) == HIGH)
  {
    Serial.println("Detected button pressed for AP");
    iMode = 1;
  }

  Serial.println("Loading json configuration...");

  loadConfiguration(config_filename, config);
  
  Serial.print("config.wifi_ssid: ");   Serial.println(config.wifi_ssid);
  Serial.print("config.wifi_pwd: ");    Serial.println(config.wifi_pwd);
  Serial.print("config.device_uid: "); Serial.println(config.device_uid);
  Serial.print("config.mqtt-server: "); Serial.println(config.mqtt_server);
  Serial.print("config.mqtt_port: ");   Serial.println(config.mqtt_port);
  Serial.print("config.mqtt_username: "); Serial.println(config.mqtt_username);
  Serial.print("config.mqtt_pwd: "); Serial.println(config.mqtt_pwd);

  if (iMode == 0)
    setupMqtt();
  else
    setupAP();

  lastContactState = (getContactValue() == HIGH);
}

void loop() {
  if (iMode == 1)
  {
    server.handleClient();
  }
  else
  {
    if (!mqtt_client.connected()) {
      digitalWrite(PIN_LED_GREEN, LOW);
      mqttConnect();
    }
    else
    {
      digitalWrite(PIN_LED_GREEN, HIGH);
    }
    mqtt_client.loop();

    if (SENSOR_ENABLED)
    {
      if ((millis() - sensorStartTime) >= (sensorInterval * 1000))
      {
        sensorStartTime = millis();
        setActionSensor(0);
      }
    }

    if (CONTACT_ENABLED)
    {
      int currentContactState = getContactValue();
      if ((currentContactState == HIGH) != lastContactState)
      {
        setActionContact(currentContactState == HIGH ? 0 : 1);
      }
      lastContactState = (currentContactState == HIGH);
    }

  }
}

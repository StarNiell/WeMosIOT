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

const uint16_t PIN_ONOFF       = 4;  //D2
const uint16_t PIN_MANUAL      = 14; //D5
const uint16_t PIN_LED_GREEN   = 16; //D0
const uint16_t PIN_LED_RED     = 5;  //D1
const uint16_t PIN_MODE        = 15; //D8 (Pulldown)

bool lastConnectionStatus = false;
String sid = "StarNiell WiFi";
String pwd = "abcd1234";
const char *sidAP = "WD1R2-REMOTE";
const char *pwdAP = "12345678";
uint8_t iMode = 0;

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

const char *config_filename = "/store/config.json";  // <- SD library uses 8.3 filenames
Config config; 

String pub_payload = "{\"device_id\": \"<device_id>\", \"timestamp\": \"<timestamp>\", \"type\": \"<type>\", \"value\": <value>, \"unit\": \"<unit>\", \"status\": \"<status>\" }";

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

String getPayload(char *device_id, char *type, int value, char *unit)
{
  String payload = pub_payload;
  char timestamp[] = "0000-00-00T00:00:00Z";
  payload.replace("<device_id>", device_id);
  payload.replace("<timestamp>", timestamp);
  payload.replace("<type>", type);
  
  char value_str[8];
  itoa(value, value_str, 10);
  payload.replace("<value>", value_str);

  payload.replace("<unit>", unit);
  payload.replace("<status>", "ok");
  
  return payload;
}


void setAction(int value)
{
  char type[] = "switch";
  char unit[] = "";
  String payload = getPayload(config.device_uid, type, value, unit);
  /*
  String payload = pub_payload;
  payload.replace("<device_id>", config.device_uid);
  char value_str[8];
  itoa(value, value_str, 10);
  payload.replace("<value>", value_str);
  */
  if (value > 0)
  {
    digitalWrite(PIN_ONOFF, HIGH);
    Serial.println("Set Action to ON");
  }
  else
  {
    digitalWrite(PIN_ONOFF, LOW);
    Serial.println("Set Action to OFF");
  }

  if (mqtt_client.connected())
  {
    int str_len = payload.length() + 1;
    char payload_char[str_len];
    payload.toCharArray(payload_char, str_len);
    mqtt_client.publish(config.topic_pub, payload_char, true);
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

  if (strcmp(device_id, config.device_uid) == 0)
  {
    Serial.println("device_id == config.device_uid.... OK");
    if (value >= 0)
    {
      setAction(value);
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
        //mqtt_client.publish(mqtt_topic, "Hi EMQX I'm ESP8266 ^^");
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
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_ONOFF, OUTPUT);
  pinMode(PIN_MANUAL, INPUT_PULLUP);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_MODE, INPUT_PULLDOWN_16);

  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED, HIGH);

  Serial.println("ESP starts");

  LittleFS.begin();

  delay(2000);

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
}

void loop() {
  if (iMode == 1)
  {
    server.handleClient();
  }
  else
  {
    if (!mqtt_client.connected()) {
        mqttConnect();
    }
    mqtt_client.loop();
  }
}

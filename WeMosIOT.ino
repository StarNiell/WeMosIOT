#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>

ESP8266WebServer server(80);
//WiFiServer serverAP(8080);

const uint16_t PIN_ON          = 0;  //D3 (pullup)
const uint16_t PIN_OFF         = 14; //D5
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
  config.mqtt_port            = doc["mqtt-port"]      | 2731;
  strlcpy(config.mqtt_username, doc["mqtt-username"]  | "<MQTT USERNAME>", sizeof(config.mqtt_username));
  strlcpy(config.mqtt_pwd,      doc["mqtt-pwd"]       | "<MQTT PWD>", sizeof(config.mqtt_pwd));
  strlcpy(config.device_uid,    doc["device-uid"]     | "<DEVICE UID>", sizeof(config.device_uid));
  strlcpy(config.topic_sub,     doc["topic-sub"]      | "<TOPIC SUB>", sizeof(config.topic_sub));
  strlcpy(config.topic_pub,     doc["topic-pub"]      | "<TOPIC PUB>", sizeof(config.topic_pub));

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

  buffer.replace("wifi_ssid_value", config.wifi_ssid);
  buffer.replace("wifi_pwd_value", config.wifi_pwd);
  buffer.replace("mqtt_server_value", config.mqtt_server);
  buffer.replace("mqtt_port_value", mqtt_port_str);
  buffer.replace("mqtt_username_value", config.mqtt_username);
  buffer.replace("mqtt_pwd_value", config.mqtt_pwd);
  buffer.replace("device_uid_value", config.device_uid);
  buffer.replace("topic_sub_value", config.topic_sub);
  buffer.replace("topic_pub_value", config.topic_pub);
 
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

void setupMqtt() {

}

void setupWebServer()
{
  server.serveStatic("/", LittleFS, "/", "no-cache, no-store, must-revalidate");
  
  server.onNotFound([](){ 
    server.send(404, "text/plain", "Landing page not found! you can chose: 'index.html' or 'config.html!");  
  });
 
  server.on("/saveconfig", HTTP_POST, []() {
    strcpy(config.wifi_ssid,     server.arg("wifi_ssid").c_str());
    strcpy(config.wifi_pwd,      server.arg("wifi_pwd").c_str());
    strcpy(config.mqtt_server,   server.arg("mqtt_server").c_str());
    config.mqtt_port =           atoi(server.arg("mqtt_port").c_str());
    strcpy(config.mqtt_username, server.arg("mqtt_username").c_str());
    strcpy(config.mqtt_pwd,      server.arg("mqtt_pwd").c_str());
    strcpy(config.device_uid,    server.arg("device_uid").c_str());
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
  pinMode(PIN_ON, INPUT_PULLUP);
  pinMode(PIN_OFF, INPUT_PULLUP);
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
  Serial.print("config.mqtt-server: "); Serial.println(config.mqtt_server);
  Serial.print("config.mqtt_port: ");   Serial.println(config.mqtt_port);

  if (iMode == 0)
    setupMqtt();
  else
    setupAP();
}

void loop() {
  if (iMode == 1)
    server.handleClient();
}

#include <Arduino.h>
#include <ArduinoJson.h>
#include <IRremote.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>

ESP8266WebServer server(80);
//WiFiServer serverAP(8080);

const uint16_t PIN_IR_RECV     = 12; //D6
const uint16_t PIN_IR_SEND     = 13; //D7
const uint16_t PIN_ON          = 0;  //D3 (pullup)
const uint16_t PIN_OFF         = 14; //D5
const uint16_t PIN_LED_GREEN   = 16; //D0
const uint16_t PIN_LED_RED     = 5;  //D1
const uint16_t PIN_MODE        = 15; //D8 (Pulldown)

//Led strip
uint16_t rawDataON[67] = {9380,4570, 480,670, 480,670, 530,620, 530,620, 530,670, 480,670, 480,670, 480,670, 480,1770, 480,1770, 480,1770, 480,1770, 480,670, 480,1770, 480,1770, 480,1770, 480,1770, 480,1770, 480,670, 480,670, 480,670, 480,670, 480,670, 530,670, 480,670, 480,670, 480,1770, 480,1770, 480,1720, 530,1770, 480,1770, 480,1770, 480};  // Protocol=NEC Address=0xEF00 Command=0x3 Raw-Data=0xFC03EF00 32 bits LSB first
uint16_t rawDataOFF[67] = {9480,4520, 580,570, 580,570, 580,570, 580,570, 580,570, 580,570, 580,570, 580,570, 580,1670, 580,1670, 580,1670, 580,1670, 580,570, 580,1670, 580,1670, 580,1670, 580,570, 580,1670, 630,520, 630,520, 630,520, 630,570, 580,570, 580,570, 580,1670, 580,570, 630,1620, 630,1620, 580,1670, 580,1720, 580,1670, 580,1670, 580};  // Protocol=NEC Address=0xEF00 Command=0x2 Raw-Data=0xFD02EF00 32 bits LSB first

//Air condit
uint16_t khz = 38;

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
  doc["device-uid"]     = config.device_uid
  doc["topic-sub"]      = config.topic_sub;
  doc["topic-pub"]      = config.topic_pub;

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write to file"));
  }

  // Close the file
  file.close();

  return true;
}

void IRSendOn() {
  Serial.println("Send ON");
  //IrSender.sendNEC(0xEF00, 0x3, 1); // strip led easy control ON
  //IrSender.sendSamsung(0x7, 0x2, 1); // Samsung TV easy control ON/OFF
  IrSender.sendRaw(rawDataON, sizeof(rawDataON), khz);
  delay(500);
}

void IRSendOff() {
  Serial.println("Send OFF");
  //IrSender.sendNEC(0xEF00, 0x2, 1); // strip led easy control OFF
  IrSender.sendRaw(rawDataOFF, sizeof(rawDataON), khz);
  delay(500);
}
/*
void readConnectionParams()
{
  String buffer = "";
  File file = LittleFS.open("/store/wifissid.txt", "r");
  while(file.available()){
    buffer += file.readString();
  } 
  file.close();
  sid = buffer;

  file = LittleFS.open("/store/wifipwd.txt", "r");
  buffer = "";
  while(file.available()){
    buffer += file.readString();
  } 
  file.close();
  pwd = buffer;
}

bool writeConnectionParams(String sSSID, String sPWD)
{
  File file = LittleFS.open("/store/wifissid.txt", "w");
  file.print(sSSID);
  delay(1);
  //Close the file
  file.close();

  file = LittleFS.open("/store/wifipwd.txt", "w");
  file.print(sPWD);
  delay(1);
  //Close the file
  file.close();

  String buffer = "";
  file = LittleFS.open("/config.static", "r");
  while(file.available()){
    buffer += file.readString();
  } 
  file.close();

  buffer.replace("wifissid_value", sSSID);
  buffer.replace("wifipassword_value", sPWD);
 
  file = LittleFS.open("/config.html", "w");
  file.print(buffer);
  delay(500);
  //Close the file
  file.close();

  return true;
}
*/
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

void setupWebServer()
{
  server.serveStatic("/", LittleFS, "/", "no-cache, no-store, must-revalidate");
  
  server.onNotFound([](){ 
    server.send(404, "text/plain", "Landing page not found! you can chose: 'index.html' or 'config.html!");  
  });
 
  server.on("/led", HTTP_POST, []() {    
    
    const String ledState = server.arg("ledstate");
    if(ledState=="on"){
      IRSendOn();
    }
    else if(ledState=="off"){
      IRSendOff();
    }
    server.send(200, "text/json", "{\"result\":\"ok\"}");
  });

  server.on("/saveconfig", HTTP_POST, []() {
    String wfsid = server.arg("wifissid");
    String wfpwd = server.arg("wifipwd");
    Serial.print("wfsid: ");
    Serial.println(wfsid);
    Serial.print("wfpwd: ");
    Serial.println(wfpwd);

    if (saveConfiguration(config_filename, config))
    {
      server.send(200, "text/plane","WiFi Configuration saved! Please reboot the system.");
    }
    else
    {
      server.send(500, "text/plane","Unable to save the WiFi Configuration! Please reboot the system and retry.");
    }
  });
  server.begin();
}

void setupRemoteControl()
{
  Serial.println("Entering Remote Control Mode");

  Serial.print("SSID: ");
  Serial.println(sid);
  Serial.print("PWD : ");
  Serial.println(pwd);

  WiFi.begin(sid, pwd);
  Serial.print("Connecting...");
  while(WiFi.status()!=WL_CONNECTED){ //Loop which makes a point every 500ms until the connection process has finished
    delay(500);
    Serial.print(".");
  }
  lastConnectionStatus = true;
  Serial.println();
  Serial.print("Connected! IP-Address: ");
  Serial.println(WiFi.localIP()); //Displaying the IP Address
  // if (MDNS.begin("nerd-corner")) {
  //   Serial.println("DNS started, available with: ");
  //   Serial.println("http://nerd-corner.local/");
  // }
  
  setupWebServer();

  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_LED_GREEN, HIGH);
}

void setup() {
  Serial.begin(115200);
  IrReceiver.begin(PIN_IR_RECV, ENABLE_LED_FEEDBACK);
  IrSender.begin(PIN_IR_SEND);
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
    setupRemoteControl();
  else
    setupAP();
}

void handleControl() {
  if (digitalRead(PIN_ON) == LOW)
  {
    IRSendOn();
  }
  if (digitalRead(PIN_OFF) == LOW)
  {
    IRSendOff();
  }
}

/*
void serverAPLoop()
{
  String buffer = "";
  WiFiClient client = serverAP.available();   // attesa di nuovi client
  if (client) { 
    Serial.println("New Client.");
    String currentLine = "";  
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {     
        char c = client.read();             // read a byte, then
        //Serial.write(c);                    // print it out the serial monitor
        buffer += c;
        if (c == '\n') {                    // if the byte is a newline character
          Serial.println(buffer);
          if (buffer.startsWith("GET /"))
          {
            Serial.println("Arrivato GET..................................................");
          }
          buffer = "";
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println();

            String buff = "";
            File file = LittleFS.open("/config.html", "r");
            while(file.available()){
              buff += file.readString();
            } 

            buff.replace("wifissid_value", sid);
            buff.replace("wifipassword_value", pwd);
            //Serial.println(buff);
            client.print(buff);

            break;
          } else {    // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }

      }
    }
    // close the connection:
    client.stop();
    Serial.println("Client Disconnected.");  
  }  
}
*/

void loop() {
  if (IrReceiver.decode())
  {
    Serial.println(IrReceiver.decodedIRData.decodedRawData, HEX);
    //IrReceiver.printIRResultShort(&Serial);
    //IrReceiver.printIRSendUsage(&Serial);
    IrReceiver.printIRResultRawFormatted(&Serial);
    IrReceiver.resume();
  }

  handleControl(); // Manual control by switch

  if (iMode == 0) // Remote Control Server
  {
    if (WiFi.status()!=WL_CONNECTED && lastConnectionStatus)
    {
      Serial.println("Connection lost");
      lastConnectionStatus = false;
      digitalWrite(PIN_LED_RED, HIGH);
      digitalWrite(PIN_LED_GREEN, LOW);
    }
    else if (WiFi.status()==WL_CONNECTED && !lastConnectionStatus)
    {
      Serial.println("Connection established");
      lastConnectionStatus = true;
      digitalWrite(PIN_LED_RED, LOW);
      digitalWrite(PIN_LED_GREEN, HIGH);
    }
    
    if (!lastConnectionStatus)
    {
      if (digitalRead(PIN_LED_RED) == HIGH)
        digitalWrite(PIN_LED_RED, LOW);
      else
        digitalWrite(PIN_LED_RED, HIGH);
      delay(1000);
    }

    server.handleClient();
    //MDNS.update();
  }
  else // AP mode server
  {
    //serverAPLoop();
      server.handleClient();

    if (digitalRead(PIN_LED_GREEN) == HIGH)
      digitalWrite(PIN_LED_GREEN, LOW);
    else
      digitalWrite(PIN_LED_GREEN, HIGH);
    delay(1000);
  }
}

//////// Core Libraries ///////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <FS.h>
#ifdef ESP32
#include <WiFi.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <Update.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#else
#error "Platform not supported"
#endif
#include <EEPROM.h>
#include <Ticker.h>
#include <ArduinoOTA.h>
#include "version.h"

///////// External Libraries///////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <Bounce2.h>              //https://github.com/thomasfredericks/Bounce2
#include <MQTT.h>                 //https://github.com/256dpi/arduino-mqtt
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager/archive/development.zip
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson/releases/download/v6.8.0-beta/ArduinoJson-v6.8.0-beta.zip
#include <WebSocketsServer.h>     //https://github.com/Links2004/arduinoWebSockets
#ifdef ESP8266
#include <DDUpdateUploadServer.h> //https://github.com/debsahu/DDUpdateUploadServer
#endif
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//#include "secrets.h" //uncomment to use values in secrets.h

#define HOSTNAME "MQTT_Siren" //DO NOT use spaces

#ifdef ESP32
#define SKR_PIN 21 //GPIO21 on ESP32
#define BUTTON_PIN 0 //GPIO0 on ESP32
#else
#define SKR_PIN 5 //D1 on NodeMCU
#define BUTTON_PIN 0 //FLASH_BUTTON on NodemMCU
#endif

#ifndef SECRET
char mqtt_server[40] = "192.168.0.xxx";
char mqtt_username[40] = "";
char mqtt_password[40] = "";
char mqtt_port[6] = "1883";
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef ESP32
WebServer server(80);
const char* updateIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";
#else
ESP8266WebServer server(80);
DDUpdateUploadServer httpUpdater;
#endif
WebSocketsServer webSocket = WebSocketsServer(81);
WiFiClient net;
MQTTClient client(512);
Ticker sendStat;
Bounce debouncer = Bounce();
float batVoltage = 0.0;

#define MAX_DEVICES 1

struct SIREN
{
  uint8_t pin;
  bool state = false;
} SIREN[MAX_DEVICES];

uint8_t esp_pins[MAX_DEVICES] = {SKR_PIN};

char light_topic_in[100] = "";
char light_topic_out[100] = "";

char mqtt_client_name[100] = HOSTNAME;

bool shouldSaveConfig = false;
bool shouldUpdateLights = false;
bool shouldReboot = false;
unsigned long previousMillis = 0;

void saveConfigCallback()
{
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

// /*****************  Read SPIFFs values *****************************/
void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
#ifdef ESP32
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root)
  {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory())
  {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file)
  {
    if (file.isDirectory())
    {
      Serial.print("  DIR : ");
      Serial.print(file.name());
      time_t t = file.getLastWrite();
      struct tm *tmstruct = localtime(&t);
      Serial.printf("  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min, tmstruct->tm_sec);
      if (levels)
      {
        listDir(fs, file.name(), levels - 1);
      }
    }
    else
    {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.print(file.size());
      time_t t = file.getLastWrite();
      struct tm *tmstruct = localtime(&t);
      Serial.printf("  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min, tmstruct->tm_sec);
    }
    file = root.openNextFile();
  }
  Serial.println(F("SPIFFs started"));
  Serial.println(F("---------------------------"));
#else

  Dir dir = SPIFFS.openDir(dirname);
  while (dir.next())
  {
    String fileName = dir.fileName();
    size_t fileSize = dir.fileSize();
    if (Serial)
      Serial.printf("FS File: %s, size: %dB\n", fileName.c_str(), fileSize);
  }

  FSInfo fs_info;
  SPIFFS.info(fs_info);
  if (Serial)
  {
    Serial.printf("FS Usage: %d/%d bytes\n", fs_info.usedBytes, fs_info.totalBytes);
    Serial.println(F("SPIFFs started"));
    Serial.println(F("---------------------------"));
  }
#endif
}

// /*****************  EEPROM to save light state *****************************/

int eeprom_addr = 0;

void writeEEPROM(void)
{
  size_t LED_data_size = sizeof(SIREN[0]);
  for (uint8_t i = 0; i < MAX_DEVICES; i++)
    EEPROM.put(eeprom_addr + 1 + LED_data_size * i, SIREN[i]);
  EEPROM.commit();
}

void readEEPROM(void)
{
  char eeprominit = char(EEPROM.read(eeprom_addr));
  if (eeprominit != 'w')
  {
    EEPROM.write(eeprom_addr, 'w');
    writeEEPROM();
    shouldSaveConfig = true;
  }
  else
  {
    size_t LED_data_size = sizeof(SIREN[0]);
    for (uint8_t i = 0; i < MAX_DEVICES; i++)
      EEPROM.get(eeprom_addr + 1 + LED_data_size * i, SIREN[i]);
  }
}

// /*****************  Light's States *****************************/

void initLights(void)
{
  for (uint8_t i = 0; i < MAX_DEVICES; i++)
  {
    SIREN[i].pin = esp_pins[i];
    pinMode(SIREN[i].pin, OUTPUT);
  }
}

void setLights(uint8_t single_pin = 99)
{
  uint8_t begin_i = 0, end_i = MAX_DEVICES;
  if (single_pin != 99)
  {
    begin_i = single_pin;
    end_i = single_pin + 1;
  }
  for (uint8_t i = begin_i; i < end_i; i++)
    digitalWrite(SIREN[i].pin, (SIREN[i].state) ? HIGH : LOW); // active HIGH is siren ON
  writeEEPROM();
  shouldUpdateLights = false;
}

void setAllOn(void)
{
  for (uint8_t i = 0; i < MAX_DEVICES; i++)
    SIREN[i].state = true;
  setLights();
}

void setAllOff(void)
{
  for (uint8_t i = 0; i < MAX_DEVICES; i++)
    SIREN[i].state = false;
  setLights();
}

// /*****************  MQTT Stuff *****************************/

String statusMsg(void)
{
  /*
  Will send out something like this:
  {
    "siren1":"OFF",
  }
  */

  DynamicJsonDocument json(JSON_OBJECT_SIZE(MAX_DEVICES) + 100);
  for (uint8_t i = 0; i < MAX_DEVICES; i++)
  {
    String l_name = "siren" + String(i + 1);
    json[l_name] = (SIREN[i].state) ? "ON" : "OFF";
  }
  String msg_str;
  serializeJson(json, msg_str);
  return msg_str;
}

void processJson(String &payload)
{
  /*
  incoming message template:
  {
    "siren": 1,
    "state": "ON"
  }
  */

  DynamicJsonDocument jsonBuffer(JSON_OBJECT_SIZE(2) + 100);
  DeserializationError error = deserializeJson(jsonBuffer, payload);
  if (error)
  {
    Serial.print(F("parseObject() failed: "));
    Serial.println(error.c_str());
  }
  JsonObject root = jsonBuffer.as<JsonObject>();

  if (root.containsKey("siren"))
  {
    uint8_t index = jsonBuffer["siren"];
    index--;
    if (index >= MAX_DEVICES)
      return;
    String stateValue = jsonBuffer["state"];
    if (stateValue == "ON" or stateValue == "on")
    {
      SIREN[index].state = true;
      shouldUpdateLights = true;
      sendMQTTStatusMsg();
      webSocket.broadcastTXT(statusMsg().c_str());
      //writeEEPROM();
    }
    else if (stateValue == "OFF" or stateValue == "off")
    {
      SIREN[index].state = false;
      shouldUpdateLights = true;
      sendMQTTStatusMsg();
      webSocket.broadcastTXT(statusMsg().c_str());
      //writeEEPROM();
    }
  }
}

void sendMQTTStatusMsg(void)
{
  Serial.print(F("Sending ["));
  Serial.print(light_topic_out);
  Serial.print(F("] >> "));
  Serial.println(statusMsg());
  client.publish(light_topic_out, statusMsg());
  sendStat.detach();
}

void publishBatVoltage(void)
{
  DynamicJsonDocument json(JSON_OBJECT_SIZE(1) + 100);
  json["voltage"] = batVoltage;
  
  String msg_str;
  serializeJson(json, msg_str);

  Serial.print(F("Sending ["));
  Serial.print(light_topic_out);
  Serial.print(F("] >> "));
  Serial.println(msg_str);
  client.publish(light_topic_out, msg_str);
}

void messageReceived(String &topic, String &payload)
{
  Serial.println("Incoming: [" + topic + "] << " + payload);
  processJson(payload);
}

void sendAutoDiscoverySwitch(String index, String &discovery_topic)
{
  /*
  "discovery topic" >> "homeassistant/switch/XXXXXXXXXXXXXXXX/config"

  Sending data that looks like this >>
  {
    "name":"siren1",
    "state_topic": "home/aabbccddeeff/out",
    "command_topic": "home/aabbccddeeff/in",
    "payload_on":"{'siren':1,'state':'ON'}",
    "payload_off":"{'siren':1,'state':'OFF'}",
    "value_template": "{{ value_json.siren1.value }}",
    "state_on": "ON",
    "state_off": "OFF",
    "optimistic": false,
    "qos": 0,
    "retain": true
  }
  */

  const size_t capacity = JSON_OBJECT_SIZE(11) + 500;
  DynamicJsonDocument json(capacity);

  json["name"] = String(HOSTNAME) + " " + index;
  json["state_topic"] = light_topic_out;
  json["command_topic"] = light_topic_in;
  json["payload_on"] = "{'siren':" + index + ",'state':'ON'}";
  json["payload_off"] = "{'siren':" + index + ",'state':'OFF'}";
  json["value_template"] = "{{value_json.siren" + index + "}}";
  json["state_on"] = "ON";
  json["state_off"] = "OFF";
  json["optimistic"] = false;
  json["qos"] = 0;
  json["retain"] = true;

  String msg_str;
  Serial.print(F("Sending AD MQTT ["));
  Serial.print(discovery_topic);
  Serial.print(F("] >> "));
  serializeJson(json, Serial);
  serializeJson(json, msg_str);
  client.publish(discovery_topic, msg_str, true, 0);
  Serial.println();
}

void sendAutoDiscovery(void)
{
  for (uint8_t i = 0; i < MAX_DEVICES; i++)
  {
    String dt = "homeassistant/switch/" + String(HOSTNAME) + String(i + 1) + "/config";
    sendAutoDiscoverySwitch(String(i + 1), dt);
  }
}

void connect_mqtt(void)
{
  Serial.print(F("Checking wifi "));
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(F("."));
    delay(1000);
  }
  Serial.println(F(" connected!"));

  uint8_t retries = 0;
  Serial.print(F("Connecting MQTT "));
  while (!client.connect(mqtt_client_name, mqtt_username, mqtt_password) and retries < 15)
  {
    Serial.print(".");
    delay(5000);
    retries++;
  }
  if (!client.connected())
    ESP.restart();
  Serial.println(F(" connected!"));

  // we are here only after sucessful MQTT connect
  client.subscribe(light_topic_in);      //subscribe to incoming topic
  sendAutoDiscovery();                   //send auto-discovery topics
  sendStat.attach(2, sendMQTTStatusMsg); //send status of switches
}

// /****************************  Read/Write MQTT Settings from SPIFFs ****************************************/

bool readConfigFS()
{
  //if (resetsettings) { SPIFFS.begin(); SPIFFS.remove("/config.json"); SPIFFS.format(); delay(1000);}
  if (SPIFFS.exists("/config.json"))
  {
    Serial.print(F("Read cfg: "));
    File configFile = SPIFFS.open("/config.json", "r");
    if (configFile)
    {
      size_t size = configFile.size(); // Allocate a buffer to store contents of the file.
      std::unique_ptr<char[]> buf(new char[size]);
      configFile.readBytes(buf.get(), size);
      StaticJsonDocument<200> jsonBuffer;
      DeserializationError error = deserializeJson(jsonBuffer, buf.get());
      if (!error)
      {
        JsonObject json = jsonBuffer.as<JsonObject>();
        serializeJson(json, Serial);
        strcpy(mqtt_server, json["mqtt_server"]);
        strcpy(mqtt_port, json["mqtt_port"]);
        strcpy(mqtt_username, json["mqtt_username"]);
        strcpy(mqtt_password, json["mqtt_password"]);
        return true;
      }
      else
        Serial.println(F("Failed to parse JSON!"));
    }
    else
      Serial.println(F("Failed to open \"/config.json\""));
  }
  else
    Serial.println(F("Couldn't find \"/config.json\""));
  return false;
}

bool writeConfigFS()
{
  Serial.print(F("Saving /config.json: "));
  StaticJsonDocument<200> jsonBuffer;
  JsonObject json = jsonBuffer.to<JsonObject>();
  json["mqtt_server"] = mqtt_server;
  json["mqtt_port"] = mqtt_port;
  json["mqtt_username"] = mqtt_username;
  json["mqtt_password"] = mqtt_password;
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile)
  {
    Serial.println(F("failed to open config file for writing"));
    return false;
  }
  serializeJson(json, Serial);
  serializeJson(json, configFile);
  configFile.close();
  Serial.println(F("ok!"));
  return true;
}

// /**********************  WebSocket & WebServer ******************************/

#include "indexhtml.h" //contains gzipped minimized index.html

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{

  switch (type)
  {
  case WStype_DISCONNECTED:
    Serial.printf("[%u] Disconnected!\n", num);
    break;
  case WStype_CONNECTED:
  {
    IPAddress ip = webSocket.remoteIP(num);
    Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
    Serial.printf("WS [%u] << %s\n", num, payload);
    webSocket.sendTXT(num, statusMsg().c_str());
  }
  break;
  case WStype_TEXT:
    Serial.printf("WS [%u] << %s\n", num, payload);
    String msg = String((char *)payload);
    processJson(msg);
    webSocket.sendTXT(num, "OK");
    break;
  }
}

void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

// /****************************  SETUP  ****************************************/

void setup()
{
  EEPROM.begin(512);
  Serial.begin(115200);
  delay(10);

  initLights();
  readEEPROM();
  setLights();

  Serial.println(F("---------------------------"));
  Serial.println(F("Starting SPIFFs"));
#ifdef ESP32
  if (SPIFFS.begin(true)) //format SPIFFS if needed
#else
  if (SPIFFS.begin())
#endif
  {
    listDir(SPIFFS, "/", 0);
  }

  if (readConfigFS())
    Serial.println(F(" yay!"));

  char NameChipId[64] = {0}, chipId[9] = {0};
  WiFi.mode(WIFI_STA); // Make sure you're in station mode

#ifdef ESP32
  WiFi.setHostname(HOSTNAME);
  snprintf(chipId, sizeof(chipId), "%08x", (uint32_t)ESP.getEfuseMac());
  snprintf(NameChipId, sizeof(NameChipId), "%s_%08x", HOSTNAME, (uint32_t)ESP.getEfuseMac());
  WiFi.setHostname(const_cast<char *>(NameChipId));
#else
  WiFi.hostname(HOSTNAME);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  snprintf(chipId, sizeof(chipId), "%06x", ESP.getChipId());
  snprintf(NameChipId, sizeof(NameChipId), "%s_%06x", HOSTNAME, ESP.getChipId());
  WiFi.hostname(const_cast<char *>(NameChipId));
#endif
  WiFiManager wifiManager;

  WiFiManagerParameter custom_mqtt_server("mqtt_server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_username("mqtt_username", "mqtt username", mqtt_username, 40, " maxlength=31");
  WiFiManagerParameter custom_mqtt_password("mqtt_password", "mqtt password", mqtt_password, 40, " maxlength=31 type='password'");
  WiFiManagerParameter custom_mqtt_port("mqtt_port", "mqtt port", mqtt_port, 6);

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);

  if (!wifiManager.autoConnect(HOSTNAME))
  {
    Serial.println(F("failed to connect and hit timeout"));
    delay(3000);
    ESP.restart();
    delay(5000);
  }
  Serial.println(F("connected!"));

  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_username, custom_mqtt_username.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());

  if (shouldSaveConfig)
  {
    writeConfigFS();
    shouldSaveConfig = false;
  }

  debouncer.attach(BUTTON_PIN, INPUT_PULLUP);
  debouncer.interval(25);

  byte mac[6];
  WiFi.macAddress(mac);
  sprintf(mqtt_client_name, "%02X%02X%02X%02X%02X%02X%s", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], HOSTNAME);
  sprintf(light_topic_in, "home/%02X%02X%02X%02X%02X%02X%s", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], "/in");
  sprintf(light_topic_out, "home/%02X%02X%02X%02X%02X%02X%s", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], "/out");

  client.begin(mqtt_server, atoi(mqtt_port), net);
  client.onMessage(messageReceived);

  ArduinoOTA.setHostname(NameChipId);
  ArduinoOTA.onStart([]() {
#ifdef ESP8266
    digitalWrite(BUILTIN_LED, LOW);
#endif
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
#ifdef ESP8266
    digitalWrite(BUILTIN_LED, HIGH);
#endif
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.print(F(" HTTP server starting "));
  server.on("/", HTTP_GET, [&] {
    server.sendHeader("Content-Encoding", "gzip", true);
    server.send_P(200, PSTR("text/html"), index_htm_gz, index_htm_gz_len);
  });
  server.on("/status", HTTP_GET, [&] {
    server.send(200, "application/json", statusMsg());
  });
  server.on("/version", HTTP_GET, [&] {
    server.send(200, "text/plain", SKETCH_VERSION);
  });
  server.on("/battery", HTTP_GET, [&] {
    server.send(200, "text/plain", String(batVoltage));
  });
  server.on("/restart", HTTP_GET, [&]() {
    Serial.println(F("/restart"));
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/html", "<META http-equiv='refresh' content='15;URL=/'><body align=center><H4>Restarting...</H4></body>");
    shouldReboot = true;
  });
  server.on("/reset_wifi", HTTP_GET, [&]() {
    Serial.println(F("/reset_wlan"));
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/html", "<META http-equiv='refresh' content='15;URL=/'><body align=center><H4>Resetting WLAN and restarting...</H4></body>");
    WiFiManager wm;
    wm.resetSettings();
    shouldReboot = true;
  });
  #ifdef ESP32
  server.on("/update", HTTP_GET, []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/html", updateIndex);
  });
  server.on("/update", HTTP_POST, []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", (Update.hasError()) ? "<META http-equiv='refresh' content='15;URL=/'><body align=center><H4>Update: FAILED, refreshing in 15s.</H4></body>": "<META http-equiv='refresh' content='15;URL=/'><body align=center><H4>Update: OK, refreshing in 15s.</H4></body>");
      shouldReboot = true;
    }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.setDebugOutput(true);
        Serial.printf("Update: %s\n", upload.filename.c_str());
        if (!Update.begin()) { //start with max available size
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
        Serial.setDebugOutput(false);
      }
  });
  #else
  httpUpdater.setup(&server);
  #endif
  server.begin();
  Serial.print(F(" done!\n"));
}

// /*****************  MAIN LOOP  ****************************************/

void loop()
{
  if (millis() - previousMillis >= 1000 * 30) { //publish every 30s
    previousMillis = millis();
    batVoltage = (float) analogRead(A0) * 4.0 * 3.3 / 1024.0 ; // Scale to 3.3V first, then use 100k/300k voltage divider to scale to 13V (3S Battery)
    publishBatVoltage();
  }

  debouncer.update();
  if (debouncer.fell())
    setAllOn();
  if (debouncer.rose())
    setAllOff();

  if (shouldUpdateLights)
    setLights();

  ArduinoOTA.handle();
  server.handleClient();
  webSocket.loop();

  if (shouldReboot)
  {
    Serial.println(F("Rebooting..."));
    delay(100);
    ESP.restart();
  }

  if (!client.connected())
    connect_mqtt();
  else
    client.loop();
}
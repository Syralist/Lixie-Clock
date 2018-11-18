// this needs to be first, or it all crashes and burns...
#include <FS.h>                  
// Include Lixie Library
#include "Lixie.h" 
// setup number of pixels
const bool SIX_DIGIT = false; // True if 6-digit clock with seconds
const int NUM_LIXIES = (SIX_DIGIT) ? 6 : 4; // 4 or 6 digits
const int DISPLAY_INTERVAL = (SIX_DIGIT) ? 1000 : 60000; // update once a second or once a minute
// setup pin number and create Lixie object
const int DATA_PIN = 5;
Lixie lix(DATA_PIN, NUM_LIXIES);

//---------------------------------------
const CRGB TIME_COLOR_RGB(0,255,255); // set default color to CYAN
//---------------------------------------

// Include time libraries
#include <Time.h>
#include <Timezone.h>
// configure Central European Time (Frankfurt, Paris)
const TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     //Central European Summer Time
const TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       //Central European Standard Time
Timezone CE(CEST, CET);

// Include networking libraries
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>

// Include Json and Mqtt libraries
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <MQTT.h>

// setup NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP,"de.pool.ntp.org");

// setup MQTT
WiFiClient net;
MQTTClient client(512);
// define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40];
char mqtt_port[6] = "8080";
char mqtt_topic[34] = "home/wohnzimmer/lixie";
char mqtt_cmd_topic[50] = "home/wohnzimmer/lixie/cmd";
char mqtt_client[16] = "lixie-001";
char mqtt_user[16] = "user";
char mqtt_pass[16] = "pass";
const char* on_cmd = "ON";
const char* off_cmd = "OFF";

// Maintained state for reporting to HA
byte red = 255;
byte green = 255;
byte blue = 255;
byte brightness = 255;

//Onboard blue led
const int txPin = 1;

// Real values to write to the LEDs (ex. including brightness and state)
byte realRed = 0;
byte realGreen = 0;
byte realBlue = 0;

bool stateOn = false;

// Globals for fade/transitions
bool startFade = false;
unsigned long lastLoop = 0;
int transitionTime = 0;
bool inFade = false;
int loopCount = 0;
int stepR, stepG, stepB;
int redVal, grnVal, bluVal;

// Globals for flash
bool flash = false;
bool startFlash = false;
int flashLength = 0;
unsigned long flashStartTime = 0;
byte flashRed = red;
byte flashGreen = green;
byte flashBlue = blue;
byte flashBrightness = brightness;

//flag for saving data
bool shouldSaveConfig = false;

// global variables for clock update
const unsigned int updateInterval = 10000;
unsigned int lastUpdate = 0;

bool WiFiConnected = false;
bool NTPupdated = false;

static unsigned long msLast = 0;

// declare functions
void digitalClockDisplay();
void connect();
void messageReceived(String &topic, String &payload);
bool processJson(String &message);
void setColor(int inR, int inG, int inB);
void sendState();
int calculateStep(int prevValue, int endValue);
int calculateVal(int step, int val, int i);
void saveConfigCallback();

void setup()
{
  lix.begin(); // Initialize LEDs
  Serial.begin(115200);

  msLast = millis();

  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_topic, json["mqtt_topic"]);
          strcpy(mqtt_cmd_topic, json["mqtt_topic"]);
          sprintf(mqtt_cmd_topic, "%s%s", mqtt_topic, "/cmd");
          strcpy(mqtt_client, json["mqtt_client"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(mqtt_pass, json["mqtt_pass"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

  // This sets all lights to yellow while we're connecting to WIFI
  lix.color(255, 255, 0);
  lix.write(8888);

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 5);
  WiFiManagerParameter custom_mqtt_topic("topic", "mqtt topic", mqtt_topic, 32);
  WiFiManagerParameter custom_mqtt_client("client", "mqtt client", mqtt_client, 32);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 32);
  WiFiManagerParameter custom_mqtt_pass("pass", "mqtt pass", mqtt_pass, 32);

  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_topic);
  wifiManager.addParameter(&custom_mqtt_client);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_pass);

  if (!wifiManager.autoConnect("LixieClockAP", "password")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  WiFiConnected = true;
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_topic, custom_mqtt_topic.getValue());
  strcpy(mqtt_cmd_topic, custom_mqtt_topic.getValue());
  sprintf(mqtt_cmd_topic, "%s%s", mqtt_topic, "/cmd");
  strcpy(mqtt_client, custom_mqtt_client.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_pass, custom_mqtt_pass.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_topic"] = mqtt_topic;
    json["mqtt_client"] = mqtt_client;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_pass"] = mqtt_pass;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  // Note: Local domain names (e.g. "Computer.local" on OSX) are not supported by Arduino.
  // You need to set the IP address directly.
  client.begin(mqtt_server, net);
  client.setOptions(10, true, 5000);
  client.onMessage(messageReceived);

  connect();

  // Green on connection success
  lix.color(0, 255, 0);
  lix.write(9999);
  delay(500);

  // Reset colors to default
  lix.color(255, 255, 255);
  lix.clear();

  Serial.print("IP number assigned by DHCP is ");
  Serial.println(WiFi.localIP());

  // setup over the air programming
  ArduinoOTA.setHostname("Wordclock12OTA");
  ArduinoOTA.onStart([]() {
          Serial.println("OTA Start");
          });
  ArduinoOTA.onEnd([]() {
          Serial.println("\nOTA End");
          });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
          Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
          });
  ArduinoOTA.onError([](ota_error_t error) {
          Serial.printf("OTA Error[%u]: ", error);
          if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
          else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
          else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
          else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
          else if (error == OTA_END_ERROR) Serial.println("End Failed");
          });
  ArduinoOTA.begin();

  // start NTP Client
  timeClient.begin();

}

void loop()
{
  // handle over the air programming
  ArduinoOTA.handle();
  client.loop();
  delay(10);  // <- fixes some issues with WiFi stability

  if (!client.connected()) {
    connect();
  }

  // update display every second if SIX_DIGIT is true, else once a minute
  unsigned long msNow = millis();
  if((millis() - msLast >= DISPLAY_INTERVAL) || (millis() < msLast || NTPupdated))
  {
      msLast = millis();
      digitalClockDisplay();
      NTPupdated = false;
  }

  // update time from ntp
  if((millis() - lastUpdate) > updateInterval)
  {
      lastUpdate = millis();
      NTPupdated = timeClient.update();
      if(CE.utcIsDST(timeClient.getEpochTime()))
      {
          timeClient.setTimeOffset(2*3600); // daylight saving time: UTC+2
          // timeClient.setTimeOffset(6*3600); // test with extra offset
      }
      else
      {
          timeClient.setTimeOffset(1*3600); // normal time: UTC+1
      }
  }

  if (flash) {
    if (startFlash) {
      startFlash = false;
      flashStartTime = millis();
    }

    if ((millis() - flashStartTime) <= flashLength) {
      if ((millis() - flashStartTime) % 1000 <= 500) {
        setColor(flashRed, flashGreen, flashBlue);
      }
      else {
        setColor(0, 0, 0);
        // If you'd prefer the flashing to happen "on top of"
        // the current color, uncomment the next line.
        // setColor(realRed, realGreen, realBlue);
      }
    }
    else {
      flash = false;
      setColor(realRed, realGreen, realBlue);
    }
  }

  if (startFade) {
    // If we don't want to fade, skip it.
    if (transitionTime == 0) {
      setColor(realRed, realGreen, realBlue);

      redVal = realRed;
      grnVal = realGreen;
      bluVal = realBlue;

      startFade = false;
    }
    else {
      loopCount = 0;
      stepR = calculateStep(redVal, realRed);
      stepG = calculateStep(grnVal, realGreen);
      stepB = calculateStep(bluVal, realBlue);

      inFade = true;
    }
  }

  if (inFade) {
    startFade = false;
    unsigned long now = millis();
    if (now - lastLoop > transitionTime) {
      if (loopCount <= 1020) {
        lastLoop = now;
        
        redVal = calculateVal(stepR, redVal, loopCount);
        grnVal = calculateVal(stepG, grnVal, loopCount);
        bluVal = calculateVal(stepB, bluVal, loopCount);
        
        setColor(redVal, grnVal, bluVal); // Write current values to LED pins

        Serial.print("Loop count: ");
        Serial.println(loopCount);
        loopCount++;
      }
      else {
        inFade = false;
      }
    }
  }
}

void digitalClockDisplay()
{
  // digital clock display of the time
  String time_now;
  int Second = timeClient.getSeconds();
  int Minute = timeClient.getMinutes();
  int Hour = timeClient.getHours();

  /* Put the time into a string.
  Leftmost zeros will not be displayed because the string will be converted 
  to a number in the library. To work around this add a number unequal to zero
  at the beginning of the string.
  */
  time_now += "1";  
  if(Hour < 10){
    time_now += "0";
  }
  time_now += String(Hour);
  time_now += ":";

  if(Minute < 10){
    time_now += "0";
  }
  time_now += String(Minute);

  if(SIX_DIGIT == true){
    time_now += ":";
    if(Second < 10){
      time_now += "0";
    }
    time_now += String(Second);
  }

  char buf[10];
  time_now.toCharArray(buf,10);

  // lix.color(TIME_COLOR_RGB);
  lix.write(buf);
  Serial.println(time_now);
}

void connect() {
  Serial.print("checking wifi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(3000);
  }

  Serial.print("\nconnecting...");
  while (!client.connect(mqtt_client, mqtt_user, mqtt_pass)) {
    Serial.print(".");
    delay(3000);
  }

  Serial.println("\nconnected!");
  client.subscribe(mqtt_cmd_topic);
  Serial.print("subscribed to ");
  Serial.println(mqtt_cmd_topic);
}

void messageReceived(String &topic, String &payload) {
  Serial.println("incoming: " + topic + " - " + payload);
  if (!processJson(payload)) {
    return;
  }

  if (stateOn) {
    // Update lights
    realRed = map(red, 0, 255, 0, brightness);
    realGreen = map(green, 0, 255, 0, brightness);
    realBlue = map(blue, 0, 255, 0, brightness);
  }
  else {
    realRed = 0;
    realGreen = 0;
    realBlue = 0;
  }

  startFade = true;
  inFade = false; // Kill the current fade

  sendState();
}

bool processJson(String &message) {
  DynamicJsonBuffer jsonBuffer;

  JsonObject& root = jsonBuffer.parseObject(message);

  if (!root.success()) {
    Serial.println("parseObject() failed");
    return false;
  }

  if (root.containsKey("state")) {
    if (strcmp(root["state"], on_cmd) == 0) {
      stateOn = true;
    }
    else if (strcmp(root["state"], off_cmd) == 0) {
      stateOn = false;
    }
  }

  // If "flash" is included, treat RGB and brightness differently
  if (root.containsKey("flash")) {
    flashLength = (int)root["flash"] * 1000;

    if (root.containsKey("brightness")) {
      flashBrightness = root["brightness"];
    }
    else {
      flashBrightness = brightness;
    }

    if (root.containsKey("color")) {
      flashRed = root["color"]["r"];
      flashGreen = root["color"]["g"];
      flashBlue = root["color"]["b"];
    }
    else {
      flashRed = red;
      flashGreen = green;
      flashBlue = blue;
    }

    flashRed = map(flashRed, 0, 255, 0, flashBrightness);
    flashGreen = map(flashGreen, 0, 255, 0, flashBrightness);
    flashBlue = map(flashBlue, 0, 255, 0, flashBrightness);

    flash = true;
    startFlash = true;
  }
  else { // Not flashing
    flash = false;

    if (root.containsKey("color")) {
      red = root["color"]["r"];
      green = root["color"]["g"];
      blue = root["color"]["b"];
    }

    if (root.containsKey("brightness")) {
      brightness = root["brightness"];
    }

    if (root.containsKey("transition")) {
      transitionTime = root["transition"];
    }
    else {
      transitionTime = 0;
    }
  }

  return true;
}

void setColor(int inR, int inG, int inB) {
  lix.color(inR, inG, inB);
  lix.show();

  Serial.println("Setting LEDs:");
  Serial.print("r: ");
  Serial.print(inR);
  Serial.print(", g: ");
  Serial.print(inG);
  Serial.print(", b: ");
  Serial.println(inB);
}

void sendState() {
  DynamicJsonBuffer jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();

  root["state"] = (stateOn) ? on_cmd : off_cmd;
  JsonObject& color = root.createNestedObject("color");
  color["r"] = red;
  color["g"] = green;
  color["b"] = blue;

  root["brightness"] = brightness;

  // char buffer[root.measureLength() + 1];
  // root.printTo(buffer, sizeof(buffer));
  String buffer;
  root.printTo(buffer);

  client.publish(mqtt_topic, buffer, true, 0);
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

// From https://www.arduino.cc/en/Tutorial/ColorCrossfader
/* BELOW THIS LINE IS THE MATH -- YOU SHOULDN'T NEED TO CHANGE THIS FOR THE BASICS
* 
* The program works like this:
* Imagine a crossfade that moves the red LED from 0-10, 
*   the green from 0-5, and the blue from 10 to 7, in
*   ten steps.
*   We'd want to count the 10 steps and increase or 
*   decrease color values in evenly stepped increments.
*   Imagine a + indicates raising a value by 1, and a -
*   equals lowering it. Our 10 step fade would look like:
* 
*   1 2 3 4 5 6 7 8 9 10
* R + + + + + + + + + +
* G   +   +   +   +   +
* B     -     -     -
* 
* The red rises from 0 to 10 in ten steps, the green from 
* 0-5 in 5 steps, and the blue falls from 10 to 7 in three steps.
* 
* In the real program, the color percentages are converted to 
* 0-255 values, and there are 1020 steps (255*4).
* 
* To figure out how big a step there should be between one up- or
* down-tick of one of the LED values, we call calculateStep(), 
* which calculates the absolute gap between the start and end values, 
* and then divides that gap by 1020 to determine the size of the step  
* between adjustments in the value.
*/
int calculateStep(int prevValue, int endValue) {
    int step = endValue - prevValue; // What's the overall gap?
    if (step) {                      // If its non-zero, 
        step = 1020/step;            //   divide by 1020
    }
    
    return step;
}

/* The next function is calculateVal. When the loop value, i,
*  reaches the step size appropriate for one of the
*  colors, it increases or decreases the value of that color by 1. 
*  (R, G, and B are each calculated separately.)
*/
int calculateVal(int step, int val, int i) {
    if ((step) && i % step == 0) { // If step is non-zero and its time to change a value,
        if (step > 0) {              //   increment the value if step is positive...
            val += 1;
        }
        else if (step < 0) {         //   ...or decrement it if step is negative
            val -= 1;
        }
    }
    
    // Defensive driving: make sure val stays in the range 0-255
    if (val > 255) {
        val = 255;
    }
    else if (val < 0) {
        val = 0;
    }
    
    return val;
}


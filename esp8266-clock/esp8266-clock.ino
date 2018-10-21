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

// setup NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP,"de.pool.ntp.org");

// global variables
const unsigned int updateInterval = 10000;
unsigned int lastUpdate = 0;

bool WiFiConnected = false;
bool NTPupdated = false;

static unsigned long msLast = 0;

// declare functions
void digitalClockDisplay();

void setup()
{
  lix.begin(); // Initialize LEDs
  Serial.begin(115200);

  msLast = millis();

  // This sets all lights to yellow while we're connecting to WIFI
  lix.color(255, 255, 0);
  lix.write(8888);

  WiFiManager wifiManager;
  wifiManager.autoConnect("LixieClockAP");
  WiFiConnected = true;

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

  // update display every second if SIX_DIGIT is true, else once a minute
  unsigned long msNow = millis();
  if((millis() - msLast >= DISPLAY_INTERVAL) || (millis() < msLast))
  {
      msLast = millis();
      digitalClockDisplay();
  }

  // update time from ntp
  if((millis() - lastUpdate) > updateInterval)
  {
      lastUpdate = millis();
      NTPupdated = timeClient.update();
      if(CE.utcIsDST(timeClient.getEpochTime()))
      {
          timeClient.setTimeOffset(7200); // daylight saving time: UTC+2
      }
      else
      {
          timeClient.setTimeOffset(3600); // normal time: UTC+1
      }
  }
}

void digitalClockDisplay()
{
  // digital clock display of the time
  String time_now;
  String h;
  String m;
  String s;
  int Second = timeClient.getSeconds();
  int Minute = timeClient.getMinutes();
  int Hour = timeClient.getHours();

  if(Hour < 10){
    h = "0"+String(Hour);
  }
  else{
    h = String(Hour);
  }
  if(Minute < 10){
    m = "0"+String(Minute);
  }
  else{
    m = String(Minute);
  }
  if(Second < 10){
    s = "0"+String(Second);
  }
  else{
    s = String(Second);
  }

  time_now += h;
  time_now += ":";
  time_now += m;
  if(SIX_DIGIT == true){
    time_now += ":";
    time_now += s;
  }

  char buf[10];
  time_now.toCharArray(buf,10);

  lix.color(TIME_COLOR_RGB);
  lix.write(buf);
  Serial.println(time_now);
}

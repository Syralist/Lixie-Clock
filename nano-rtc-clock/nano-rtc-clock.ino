// Include Lixie Library
#include "Lixie.h" 
// setup number of pixels
const bool SIX_DIGIT = true; // True if 6-digit clock with seconds
const int NUM_LIXIES = (SIX_DIGIT) ? 6 : 4; // 4 or 6 digits
const int DISPLAY_INTERVAL = (SIX_DIGIT) ? 1000 : 60000; // update once a second or once a minute
// setup pin number and create Lixie object
const int DATA_PIN = 5;
Lixie lix(DATA_PIN, NUM_LIXIES);

//---------------------------------------
const CRGB TIME_COLOR_RGB(0,255,0); // set default color to CYAN
//---------------------------------------

// Include time libraries
#include <Time.h>
#include <Timezone.h>
// configure Central European Time (Frankfurt, Paris)
const TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     //Central European Summer Time
const TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       //Central European Standard Time
Timezone CE(CEST, CET);
TimeChangeRule *tcr;        //pointer to the time change rule, use to get the TZ abbrev
time_t utc;

// include RTC libraries
#include <Wire.h>
#include <DS1307RTC.h>

const int TIME_MSG_LEN = 11;   // time sync to PC is HEADER followed by unix time_t as ten ascii digits
const char TIME_HEADER = 'T';   // Header tag for serial time sync message
const int TIME_REQUEST = 7;    // ASCII bell character requests a time sync message

// global variables
static unsigned long msLast = 0;

// declare functions
void digitalClockDisplay(time_t t);
void processSyncMessage();

void setup()
{
  lix.begin(); // Initialize LEDs
  Serial.begin(9600);

  msLast = millis();

  // This sets all lights to yellow while we're setting up
  lix.color(255, 255, 0);
  lix.write(8888);

  setSyncProvider(RTC.get);

  // Green on success
  lix.color(0, 255, 0);
  lix.write(9999);
  delay(500);

  // Reset colors to default
  lix.color(255, 255, 255);
  lix.clear();
}

void loop()
{
  // set time via serial
  if (Serial.available())
  {
    processSyncMessage();
  }

  // update display every second if SIX_DIGIT is true, else once a minute
  unsigned long msNow = millis();
  if((millis() - msLast >= DISPLAY_INTERVAL) || (millis() < msLast))
  {
      msLast = millis();
      utc = now();
      // utc = RTC.get();
      digitalClockDisplay(CE.toLocal(utc, &tcr));
  }

}

void digitalClockDisplay(time_t t)
{
  // digital clock display of the time
  String time_now;
  int Second = second(t);
  int Minute = minute(t);
  int Hour = hour(t);

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

  lix.color(TIME_COLOR_RGB);
  lix.write(buf);
  Serial.println(time_now);
  Serial.println(RTC.chipPresent());
}

void processSyncMessage() {
    // if time sync available from serial port, update time and return true
    while(Serial.available() >=  TIME_MSG_LEN ){  // time message consists of a header and ten ascii digits
        char c = Serial.read();
        Serial.print(c);
        if( c == TIME_HEADER )
        {
            time_t pctime = 0;
            for(int i=0; i < TIME_MSG_LEN -1; i++)
            {
                c = Serial.read();
                if( c >= '0' && c <= '9')
                {
                    pctime = (10 * pctime) + (c - '0') ; // convert digits to a number
                }
            }
            Serial.println(RTC.set(pctime));   // Sync Arduino clock to the time received on the serial port
            Serial.print("Time set to: ");
            Serial.print(pctime);
        }
    }
}

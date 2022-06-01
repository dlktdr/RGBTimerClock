#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "math.h"
#include "esp_wifi.h"
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <esp_now.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <EEPROM.h>

/*#define LED_R 255
#define LED_G 0
#define LED_B 90*/
#define TIMER_HISTORY 10

#define LED_R 45
#define LED_G 0
#define LED_B 10

// Replace with your network credentials
const char *ssid     = "ShopWireless";
const char *password = "Shop1234";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

/* Codes sent over ESPNow. All Broadcast.
 *   *    - Start Timer
 *   X    - Stop Timer
 *   T    - Toggle Start / Stop
 *   R    - Reset Timer
 *   U    - Set Mode Up count
 *   D(t) - Set Mode Down Count. t=Preset Time
 *   @    - Timer isn't running
 *   #    - Timer is running
 *   u    - Mode is in Up count
 *   d(t) - Mode is in Down count. t = Preset Time
 *   F(t) - Current Time
 */

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;
int64_t currenttime=0; // In us
int64_t presettime=300000; // In us

enum {
  TmrMode_Stopwatch,
  TmrMode_DownTimer,
  TmrMode_Count
};
volatile int TmrMode = TmrMode_Stopwatch;

enum {
  TmrDown_Setup,
  TmrDown_Idle,
  TmrDown_Counting,
  TmrDown_Complete
};
volatile int DwnTimerState = TmrDown_Idle;

enum {
  TmrDisplay_Mins=0,
  TmrDisplay_Seconds,
  TmrDisplay_Count
};
volatile int TmrDisplay = TmrDisplay_Mins;

enum {
  TmrState_Reset,
  TmrState_Running,
  TmrState_Paused,
  TmrState_Stop,
};
volatile int TmrState = TmrState_Reset;

#define NOPIXELS 129
#define DO_PIN 22

void sendData(const char *data)
{
  if(strlen(data) != 10) {
    Serial.println("FAULT DATA LENGTH NOT 10");
    return;
  }
  esp_now_send(broadcastAddress, (uint8_t *) data, strlen(data));
  Serial.printf("Sent - %s - %d\r\n",data, strlen(data));
}

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NOPIXELS, DO_PIN, NEO_GRB + NEO_KHZ800);
  //                        1   2  3   4  5   6  7
uint32_t leddigits[14] = {0b1111111111100011111111111, // 0
                          0b1111000000000011110000000, // 1
                          0b1111111000011100001111111, // 2
                          0b1111111000011111111110000, // 3
                          0b1111000111111111110000000, // 4
                          0b0000111111111111111110000, // 5
                          0b0000111111111111111111111, // 6
                          0b1111111000000011110000000, // 7
                          0b1111111111111111111111111, // 8
                          0b1111111111111111111110000, // 9
                          0b0000000000011100000000000, // -
                          0b0000111111100000001111111, // C
                          0b0000111000000000001110000, // Hat + Underscore
                          0b1111111000000011111110000, // Backwards C
                          };


// Increment or decrement time based on last time called
void updateTime()
{
  static int64_t usecs = 0;
  if(TmrState == TmrState_Running) {
    if(TmrMode == TmrMode_Stopwatch) // Up Count
      currenttime += (esp_timer_get_time() - usecs);
    else { // Down Count
      currenttime -= (esp_timer_get_time() - usecs);
      if(currenttime <= 0) {
        currenttime = 0;
        TmrState = TmrState_Stop;
      }
    }
  }
  usecs = esp_timer_get_time();
}

void buildDigits()
{
  static uint64_t curtime = 0;
  static bool showdigits=true;
  // Flash the Zero's
  if(TmrState == TmrState_Stop && TmrMode == TmrMode_DownTimer) {
    if(curtime < esp_timer_get_time()) {
      curtime = esp_timer_get_time() + 200000; // 0.2sec
      showdigits = !showdigits;
    }
  } else {
    showdigits = true;
  }

  // Digits
  uint32_t display[5] = {0,0,0,0,0};

  // Show Dashes in reset state in stopwatch mode
  if(TmrState == TmrState_Reset && TmrMode == TmrMode_Stopwatch) {
    for(int i=0; i < 5; i++) {
      display[i] = leddigits[10];
    }
  // Show SSS.ms
  } else if(TmrDisplay == TmrDisplay_Seconds) {
    int value = currenttime / 10000;
    for(int i=0; i < 5; i++) {
      int curdigit = value % 10;
      display[i] = leddigits[curdigit];
      value = value / 10;
    }
  // Show MM:SS.ms
  } else if (TmrDisplay == TmrDisplay_Mins) {
    int mins = currenttime / 60000000;
    int secs = (currenttime - (mins * 60000000)) / 100000;

    // Secs
    for(int i=0; i < 3; i++) {
      int curdigit = secs % 10;
      display[i] = leddigits[curdigit];
      secs = secs / 10;
    }

    // Mins
    bool blanklead=false;
    for(int i=3; i < 5; i++) {
      int curdigit = mins % 10;
      if(blanklead)
        display[i] = 0;
      else
        display[i] = leddigits[curdigit];
      mins = mins / 10;
      if(mins == 0)
        blanklead = true;
    }
  }

  for(int i=0; i < 5; i++) {
    uint32_t ledout = display[i];

    // Set Data
    for(int j=0; j < 25; j++) {
      uint8_t r=LED_R,g=LED_G,b=LED_B; // LED Color + Brightness
      if(TmrState == TmrState_Reset) {
        r = 0; g=255; b=0;
      }
      /*if(i>2 && curdigit == 0 && value == 0) {
        r=0;g=0;b=0;
      }*/

      // Hide all if requested
      if(!showdigits) {
        r=0;g=0;b=0;
      }

      pixels.setPixelColor(i*25+j,
                          (ledout & 1<<(24-j))?r:0,
                          (ledout & 1<<(24-j))?g:0,
                          (ledout & 1<<(24-j))?b:0);
    }
  }

  if(TmrState == TmrState_Reset) {
    pixels.setPixelColor(125,0,0,0);
    pixels.setPixelColor(126,0,0,0);
    pixels.setPixelColor(127,0,0,0);
    pixels.setPixelColor(128,0,0,0);
  } else {
    if(TmrDisplay == TmrDisplay_Seconds) {
      pixels.setPixelColor(125,0,0,0);
      pixels.setPixelColor(126,0,0,0);
      pixels.setPixelColor(127,LED_R,LED_G,LED_B);
      pixels.setPixelColor(128,0,0,0);
    } else {
      pixels.setPixelColor(125,LED_R,LED_G,LED_B);
      pixels.setPixelColor(126,LED_R,LED_G,LED_B);
      pixels.setPixelColor(127,0,0,0);
      pixels.setPixelColor(128,LED_R,LED_G,LED_B);
    }
  }
  pixels.show();
}

void chase()
{
  static int ledon=0;

  for(int i=0; i < NOPIXELS; i++) {
    if(i == ledon) {
      pixels.setPixelColor(i,LED_R,LED_G,LED_B);
    } else {
      pixels.setPixelColor(i,0,0,0);
    }
  }
  ledon++;
  if(ledon >= NOPIXELS)
    ledon = 0;
  pixels.show();
  delay(20);
}

void rainbow()
{
  uint32_t bits[5]; // 5 Characters

  // Top and bottom Rainbow
  bits[0] = leddigits[13];
  bits[1] = leddigits[12];
  bits[2] = leddigits[12];
  bits[3] = leddigits[12];
  bits[4] = leddigits[11];

  // Count number of leds which are on
  int bitcount = 0;
  for(int i=0; i < 5; i++) {
    for(int j=0; j < 25; j++) {
      if(bits[i] & j<<1)
        bitcount++;
    }
  }

  // Set Decimal Off
  pixels.setPixelColor(125,0,0,0);
  pixels.setPixelColor(126,0,0,0);
  pixels.setPixelColor(127,0,0,0);
  pixels.setPixelColor(128,0,0,0);

  // Rainbow it.
  for(long firstPixelHue = 0; firstPixelHue < 5*65536; firstPixelHue += 256) {
    int curhue=0;
    for(int i=0; i < 5; i++) {
      for(int j=0; j < 25; j++) {
        if(bits[i] & 1<<(24-j)) {
          int pixelHue = firstPixelHue + (curhue++ * 65536L / bitcount);
          pixels.setPixelColor((i*25)+j, pixels.gamma32(pixels.ColorHSV(pixelHue)));
        }
        else
          pixels.setPixelColor((i*25)+j,0,0,0); // Pixel off
      }
    }
    if(TmrState != TmrState_Reset) // Quickly jump out if timer started
      break;
    delay(20);
    pixels.show();
  }
}

// callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  //Serial.print("\r\nLast Packet Send Status:\t");
  if(status != ESP_NOW_SEND_SUCCESS)
    Serial.println("Delivery Fail");
}


// callback function that will be executed when data is received
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  if(len != 10)
    return;

  // Copy and null terminate
  char incoming[11];
  memcpy(incoming, incomingData, 10);
  incoming[10] = '\0';
  char function = incoming[0];

  switch(function) {
    case '*':
      Serial.println("Start Rec");
      break;
    case 'X':
      Serial.println("Stop Rec");
      break;
    case 'T':
      Serial.println("Toggle Rec");
      break;
    case 'R':
      Serial.println("Reset Rec");
      break;
    default:
      Serial.println("Unknown Rec");
      break;
  }

  // Toggle (start/stop)
  if(function == 'T') {
    if(TmrState == TmrState_Running)
      function = 'X'; // Stop
    if(TmrState == TmrState_Stop ||
       TmrState == TmrState_Paused ||
       TmrState == TmrState_Reset)
      function = '*'; // Start
  }

  if(function == ' ') // Communication with device
    Serial.print(".");
  else if(function == 'X') { // Stop
    switch(TmrState) {
      case TmrState_Running:
        // Store Exact current time
        updateTime();
        char sendchar[11];
        sprintf(sendchar, "F%09lld", currenttime/1000);
        sendData(sendchar);
        TmrState = TmrState_Paused;
        break;
      default:
        break;
    }
  } else if(function == '*') { // Start
    switch(TmrState) {
      case TmrState_Reset:
      case TmrState_Stop:
      case TmrState_Paused:
        // Set current time
        TmrState = TmrState_Running;
        break;
      default:
        break;
    }
  } else if(function == 'R') { // Reset
    //switch(TmrState) {
      //case TmrState_Stop:
        // Set current time
        if(TmrMode == TmrMode_Stopwatch)
          currenttime = 0;
        else if(TmrMode == TmrMode_DownTimer)
          currenttime = presettime*1000;
        TmrState = TmrState_Reset;
    //    break;
    //}
  } else if(function == 'D') { // Down Count Mode
    TmrMode = TmrMode_DownTimer;
    presettime = atoi(incoming+1) * 1000;
    Serial.printf("Down count mode, Preset = %lld\r\n", presettime);
    TmrState = TmrState_Stop;
    currenttime = presettime;
  } else if(function == 'U') { // Stopwatch Mode
    TmrMode = TmrMode_Stopwatch;
    TmrState = TmrState_Reset;
    currenttime = 0;
  }
}

void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);

  EEPROM.begin(100);
  presettime = EEPROM.readLong64(0);
  if(presettime > 5940000000) {
    // 99 Mins
    presettime = 0;
    EEPROM.writeLong64(0,0);
  }
  // Strip remainder
  presettime = (presettime / 10000) * 10000;

  TmrMode = EEPROM.readBool(5);

  // Connect to Wi-Fi
  /*Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

// Initialize a NTPClient to get time
  timeClient.begin();
  // Set offset time in seconds to adjust for your timezone, for example:
  // GMT +1 = 3600
  // GMT +8 = 28800
  // GMT -1 = -3600
  // GMT 0 = 0
  timeClient.setTimeOffset(-28800);
*/
  pinMode(23, OUTPUT);
  digitalWrite(23, LOW);

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  esp_wifi_set_protocol( WIFI_IF_STA, WIFI_PROTOCOL_LR );
  esp_wifi_set_max_tx_power(WIFI_POWER_19_5dBm);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  // Register peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  // Add peer
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }

  // Setup Pixels
  pixels.begin();
  for(int i=0; i < 129; i++) {
    pixels.setPixelColor(i,0,5,0);
  }
  pixels.show();
}

void loop() {
  updateTime();

  if(TmrState == TmrState_Reset)
    rainbow();
  else
    buildDigits();

  // Periodically Send if The timer is running and the time
  // to sync up remote screens
  static int cnt=0;
  if(cnt++ == 400) {
    cnt = 0;
    if(TmrState != TmrState_Running)
      sendData("@         ");
    else {
      sendData("#         ");
    }
    if(TmrMode == TmrMode_DownTimer) {
      char sendchar[11];
      sprintf(sendchar, "d%09lld", presettime/1000); // Force update remote screen to perfectly match
      sendData(sendchar);
    } else {
      sendData("u         ");
    }
    char sendchar[11];
    sprintf(sendchar, "F%09lld", currenttime/1000); // Force update remote screen to perfectly match
    sendData(sendchar);
  }
}


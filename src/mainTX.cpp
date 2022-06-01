#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "math.h"
#include "esp_wifi.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include "EasyButton.h"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


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

/*
  Rui Santos
  Complete project details at https://RandomNerdTutorials.com/esp-now-esp32-arduino-ide/

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files.

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
*/

// REPLACE WITH YOUR RECEIVER MAC Address

int64_t delaytimer=0;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;
int64_t timerval=0; // In Milliseconds
int64_t presettime=354321;
volatile bool debounce[4]= {false,false,false,false};
volatile bool timerrunning = false;
int cursorposition=0;

#define BUTTON_PIN 23

EasyButton menuButton(0);

bool inMenu=false;

enum {
  TmrMode_Stopwatch,
  TmrMode_DownTimer,
  TmrMode_Count
};
int TmrMode = TmrMode_Stopwatch;

enum {
  TmrDown_Setup,
  TmrDown_Idle,
  TmrDown_Counting,
  TmrDown_Complete
};
int DwnTimerState = TmrDown_Idle;

enum {
  TmrDisplay_Mins=0,
  TmrDisplay_Seconds,
  TmrDisplay_Count
};
int TmrDisplay = TmrDisplay_Mins;

enum {
  TmrMenu_Mode=0,
  TmrMenu_Time,
  TmrMenu_MinsSecs,
  //TmrMenu_RGB, // Set Display Color
  TmrMenu_Count
};
int TimerMenu = TmrMenu_Mode;

void sendData(const char *data)
{
  if(strlen(data) != 10) {
    Serial.println("FAULT DATA LENGTH NOT 10");
    return;
  }
  esp_now_send(broadcastAddress, (uint8_t *) data, strlen(data));
  Serial.printf("Sent - %s - %d\r\n",data, strlen(data));
}

// callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  //Serial.print("\r\nLast Packet Send Status:\t");
  if(status != ESP_NOW_SEND_SUCCESS)
    Serial.println("Delivery Fail");
}

void IRAM_ATTR ISRStart() {
  // Don't resend it waiting for debounce
  if(debounce[0])
    return;
  debounce[0] = true;
  sendData("*         ");
  timerrunning = true;
}

void IRAM_ATTR ISRStop() {
  // Don't resend it waiting for debounce
  if(debounce[1])
    return;
  debounce[1] = true;
  sendData("X         ");
  timerrunning = false;
}

void IRAM_ATTR ISRToggle() {
  if(debounce[2])
      return;
  debounce[2] = true;

  if(inMenu && TimerMenu == TmrMenu_Time) {
    int curval=0;
    int maxval=9;
    int step=0;
    Serial.printf("PresetTime %lld\r\n", presettime);
    switch(cursorposition) {
      case 0:
        curval = presettime / 600000;
        step = 600000;
        Serial.printf("CurVal %d\r\n", curval);
        break;
      case 1:
        curval = presettime / 60000;
        step = 60000;
        Serial.printf("CurVal %d\r\n", curval);
        break;
      case 2:
        curval = presettime % 100000 / 10000;
        step = 10000;
        maxval = 5;
        Serial.printf("CurVal %d\r\n", curval);
        break;
      case 3:
        curval = presettime % 10000 / 1000;
        step = 1000;
        Serial.printf("CurVal %d\r\n", curval);
        break;
      case 4:
        curval = presettime % 1000 / 100;
        step = 100;
        Serial.printf("CurVal %d\r\n", curval);
        break;
      case 5:
        curval = presettime % 100 / 10;
        step = 10;
        Serial.printf("CurVal %d\r\n", curval);
        break;
      }
      // Increment/Roll over Value
      if(curval == maxval) {
        presettime -= step * curval;
      } else {
        presettime += step;
      }
      char data[11];
      sprintf(data, "D%09lld", presettime);
      sendData(data);

  } else {
    // Don't resend it waiting for debounce

    debounce[2] = true;
    sendData("T         ");
    timerrunning = !timerrunning;
  }
}

void MenuButtonLongPressed()
{
  Serial.print("Menu Pressed\r\n");
  // Enter Setup Mode
  if(!inMenu) {
    inMenu = true;

  // Already in setup.
  } else {
    TimerMenu++;

    // Skip entering time if in stopwatch mode
    if(TimerMenu == TmrMenu_Time && TmrMode == TmrMode_Stopwatch)
      TimerMenu++;

    // End of Menu Quit
    if(TimerMenu == TmrMenu_Count) {
      TimerMenu = 0;
      inMenu = false;
      // Send new mode to remote
      if(TmrMode == TmrMode_DownTimer) {
        char data[11];
        sprintf(data, "D%09lld", presettime);
        sendData(data);
        EEPROM.writeLong64(0,presettime);
        EEPROM.commit();
      } else {
        char data[11];
        sprintf(data, "U         ");
        sendData(data);
      }
    }
  }
}

void MenuButtonPressed()
{
  if(inMenu) {
    switch(TimerMenu) {
      case TmrMenu_Mode:
        TmrMode++;
        if(TmrMode == TmrMode_Count)
          TmrMode = 0;
        if(TmrMode == TmrMode_DownTimer) {
          char data[11];
          sprintf(data, "D%09lld", presettime);
          sendData(data);
        } else {
          char data[11];
          sprintf(data, "U         ");
          sendData(data);
        }
        break;
      case TmrMenu_Time:
        cursorposition++;
        if(cursorposition > 5)
          cursorposition = 0;
      case TmrMenu_MinsSecs:
        TmrDisplay++;
        if(TmrDisplay == TmrDisplay_Count)
          TmrDisplay = 0;
    }
  }

}

// callback function that will be executed when data is received
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  //Serial.printf("Data Received %d\r\n", len);
  if(len != 10)
    return;
  char incoming[11];
  memcpy(&incoming, incomingData, 10);
  incoming[10] = '\0';

  switch(incoming[0]) {
    case 'F': {
      uint32_t time = atoi(incoming+1);
      Serial.printf("TimeRec = %d\r\n", time);
      timerval = time;
      break;
    }
    case '@': {
      timerrunning = false;
      break;
    }
    case '#': {
      timerrunning = true;
      break;
    }
    case 'u': {
      TmrMode = TmrMode_Stopwatch;
      break;
    }
    case 'd': { // Down Count Mode
      timerrunning = true;
      uint32_t time = atoi(incoming+1);
      Serial.printf("TimeRec = %d\r\n", time);
      presettime = time * 1000;
      TmrMode = TmrMode_DownTimer;
      break;
    }

  }
}

void setup() {
  // Init Serial Monitor
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

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  esp_wifi_set_protocol( WIFI_IF_STA, WIFI_PROTOCOL_LR );
  esp_wifi_set_max_tx_power(WIFI_POWER_19_5dBm);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Once ESPNow is successfully Init, we will register for Send CB to
  // get the status of Trasnmitted packet
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

  menuButton.begin();
  menuButton.onPressed(MenuButtonPressed);
  menuButton.onPressedFor(500,MenuButtonLongPressed);

  pinMode(25,INPUT_PULLUP); // Start only
  pinMode(26,INPUT_PULLUP); // Stop only
  pinMode(22,INPUT_PULLUP); // Reset
  pinMode(BUTTON_PIN,INPUT_PULLUP); // Start/Stop/Reset(LongPress)
  attachInterrupt(BUTTON_PIN, ISRToggle, FALLING); // Toggle
  attachInterrupt(25, ISRStart, FALLING); // Start
  attachInterrupt(26, ISRStop, FALLING); // Stop

  //SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.setRotation(2);
}

#define DEBOUNCE_US   100000
#define RESET_US     1500000

volatile bool lastdebounce[4]={false,false,false,false};
int64_t usecs[4]={0,0,0,0};

void loop() {
  menuButton.read();

  for(int i=0; i < 4; i++) {
    // Debounce Start/Stop Buttons
    if(debounce[i] == true && lastdebounce[i] == false) { // Rising Edge
      lastdebounce[i] = true;
      usecs[i] = esp_timer_get_time();
    } else if(debounce[i] && lastdebounce[i] && (esp_timer_get_time() - usecs[i]) > DEBOUNCE_US) {
      debounce[i] = false; // Clear debounce flag, allow trigger again on start/stop pin
      lastdebounce[i] = false;
    }
  }

  // Reset Button
  static int64_t resettimer=0;
  static bool lastresetbutton=false;
  bool resetbutton = !digitalRead(22);
  static bool resetsent = false;

  // Reset was just pressed, rising edge
  if(resetbutton && lastresetbutton == false) {
    resettimer = esp_timer_get_time();

  // Button still down, wait for time to elapse
  } else if (resetbutton &&
             lastresetbutton &&
             (esp_timer_get_time() - resettimer > RESET_US) &&
             resetsent == false) {
    char sendchar[] = "R         ";
    esp_now_send(broadcastAddress, (uint8_t *)sendchar, strlen(sendchar));
    Serial.println("Reset Sent");
    resetsent = true;

  // Button released
  } else if(resetbutton == false) {
    resettimer = 0;
    resetsent = false;
  }
  lastresetbutton = resetbutton;

  display.clearDisplay();
  display.setTextSize(2); // Draw 2X-scale text

  int minutes=0;
  int seconds=0;
  int milisec=0;

  // Operating Mode
  if(!inMenu) {
    switch(TmrMode) {
      case TmrMode_Stopwatch:
        display.setTextColor(WHITE);
        display.setCursor(11, 50);
        display.printf("Stopwatch");
        display.setTextSize(2); // Draw 2X-scale text
        minutes = timerval / 60000;
        seconds = (timerval - (minutes * 60000)) / 1000;
        milisec = (timerval - (minutes * 60000) - (seconds * 1000));
        display.setCursor(15, 20);
        display.printf("%02d:%02d.%02d",minutes, seconds, milisec / 10 );
        break;
      case TmrMode_DownTimer:
        display.setTextColor(WHITE);
        display.setCursor(35, 50);
        display.printf("Timer");
        display.setTextSize(2); // Draw 2X-scale text
        minutes = timerval / 60000;
        seconds = (timerval - (minutes * 60000)) / 1000;
        milisec = (timerval - (minutes * 60000) - (seconds * 1000));
        display.setCursor(15, 20);
        display.printf("%02d:%02d.%02d",minutes, seconds, milisec / 10);
        break;
    }

  // Menu Mode
  } else {
    switch(TimerMenu) {
      case TmrMenu_Mode:
        display.setCursor(45,50);
        display.printf("Mode");
        switch(TmrMode) {
          case TmrMode_DownTimer:
            display.setCursor(10,20);
            display.printf("DownTimer");
            break;
          case TmrMode_Stopwatch:
            display.setCursor(10,20);
            display.printf("StopWatch");
            break;
        }
        break;
        case TmrMenu_MinsSecs: {
          display.setCursor(24,48);
          display.printf("Display");
          switch (TmrDisplay)
          {
          case TmrDisplay_Mins:
            display.setCursor(20,20);
            display.printf("Minutes");
            break;
          case TmrDisplay_Seconds:
            display.setCursor(20,20);
            display.printf("Seconds");
            break;
          }
        }
        break;
/*      case TmrMenu_RGB:
        display.setCursor(35,50);
        display.printf("Color");
        break;*/
      case TmrMenu_Time:
        display.setCursor(25,50);
        display.printf("Timeout");
        minutes = presettime / 60000;
        seconds = (presettime - (minutes * 60000)) / 1000;
        milisec = (presettime - (minutes * 60000) - (seconds * 1000));
        display.setCursor(15, 20);
        display.printf("%02d:%02d.%02d",minutes, seconds, milisec / 10);
        int charspace = 12;
        int ypos = 38;
        int xstart = cursorposition*charspace + 15;
        if(cursorposition > 1)
          xstart += 12;
        if(cursorposition > 3)
          xstart += 12;
        display.drawLine(xstart,ypos,xstart+charspace-4,ypos,WHITE);
        display.drawLine(xstart,ypos+1,xstart+charspace-4,ypos+1,WHITE);
        break;
    }
  }
  display.display();      // Show initial text

  delay(50);

  static int64_t lasttime=esp_timer_get_time() / 1000;
  int64_t elapsed = esp_timer_get_time() / 1000 - lasttime;
  if(timerrunning) {
    if(TmrMode == TmrMode_Stopwatch)
      timerval += elapsed;
    else
      timerval -= elapsed;
  }
  lasttime=esp_timer_get_time() / 1000;
  if(timerval < 0) timerval = 0;

}

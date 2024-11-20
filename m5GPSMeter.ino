#include <M5Unified.h> // Core & Core2
#include <M5GFX.h>
#include <TinyGPS++.h>

#include "image.c"

// Global variable
TinyGPSPlus tGPS;                             // TinyGPS++ object
HardwareSerial hSerial(2);                    // NEO-M8N serial
static const uint32_t kTimeOffsetHour = 9;    // UTC to JST offset
LGFX_Sprite g_TFTBuf = LGFX_Sprite(&M5.Lcd);  // Display Buffer

// Task define
#define GPSTASK_INTERVAL 10
#define GPSTASK_PRIORITY 15  // High
#define LCDTASK_INTERVAL 100
#define LCDTASK_PRIORITY 10

#define BACKLIGHT_UP(x)              \
  for (int i = 0; i < 200; i += 5) { \
    M5.Lcd.setBrightness(i);         \
    delay(x);                        \
  }
#define BACKLIGHT_DOWN(x)            \
  for (int i = 200; i > 0; i -= 5) { \
    M5.Lcd.setBrightness(i);         \
    delay(x);                        \
  }

// Task Update Flag
static bool g_isWritingSD = false;

void setup() {
  M5.begin();

  // Display init
  g_TFTBuf.setColorDepth(8);
  g_TFTBuf.createSprite(320, 240);
  g_TFTBuf.setTextFont(4);
  g_TFTBuf.setTextSize(2);
  g_TFTBuf.setTextColor(TFT_WHITE);

  // Speaker Noise Reduce for Core1
  /*
  M5.Speaker.mute();
  pinMode(25, OUTPUT);
  digitalWrite(25, LOW);

  M5.Power.begin();
  M5.Power.setPowerVin(true);
  */

  // Opening Display
  M5.Lcd.pushImage(0, 0, 320, 240, image_data_Image);
  BACKLIGHT_UP(10)
  delay(1000);
  BACKLIGHT_DOWN(10)

  // Mode Select Display
  g_TFTBuf.setTextFont(4);
  g_TFTBuf.setTextSize(1);
  g_TFTBuf.fillSprite(TFT_BLACK);
  g_TFTBuf.drawString("> Select NEO-M8N baud to", 0, 30);
  g_TFTBuf.drawString("    enter serial through mode", 0, 60);
  g_TFTBuf.drawString("9600", 30, 200);
  g_TFTBuf.drawString("115200", 220, 200);
  g_TFTBuf.pushSprite(0, 0);
  BACKLIGHT_UP(10)
  delay(2000);

  // Mode Select
  M5.update();
  if (M5.BtnA.isPressed()) {
    hSerial.begin(9600);
    serialThroughMode(9600);
  }
  if (M5.BtnC.isPressed()) {
    hSerial.begin(115200);
    serialThroughMode(115200);
  }
  // dim
  BACKLIGHT_DOWN(10)

  // Initial Black Screen
  g_TFTBuf.fillSprite(TFT_BLACK);
  g_TFTBuf.pushSprite(0, 0);

  // You need configure Baud of the GPS module before use
  hSerial.begin(115200);  // set 9600bps when you use GPS moudle in default settings

  // Start GPS task
  xTaskCreatePinnedToCore(GPSUpdateTask, "GPSUpdateTask", 8192, NULL, GPSTASK_PRIORITY, NULL, 1);
  // Start Display task
  xTaskCreatePinnedToCore(LCDUpdateTask, "LCDUpdateTask", 8192, NULL, LCDTASK_PRIORITY, NULL, 1);
  
  // only for CORE2
  xTaskCreatePinnedToCore(SDWriteTask, "SDWriteTask", 8192, NULL, SDTASK_PRIORITY, NULL, 1);
}

// [Task Pri=1] main
void loop() {
  delay(10000);
}

// [Task Pri=15] Update GPS Info
void GPSUpdateTask(void* args) {
  static char temp_chr;

  while (1) {
    // Receive GPS Data and update TGPS object
    while (hSerial.available() > 0) {
      temp_chr = hSerial.read();
      tGPS.encode(temp_chr);
      // Serial.write(temp_chr);
    }
    delay(GPSTASK_INTERVAL);
  }
}

// [Task Pri=10] Update Screen Info
void LCDUpdateTask(void* args) {
  static int nowAlt, nowSats, nowSpeed, nowHeading;

  while (1) {
    nowAlt = (int)(tGPS.altitude.meters());
    nowSats = tGPS.satellites.value();
    nowSpeed = (int)(tGPS.speed.kmph() - 0.5);
    nowHeading = (int)(tGPS.course.deg());

    g_TFTBuf.fillSprite(TFT_BLACK);
    g_TFTBuf.setTextColor(TFT_WHITE);

    if (nowSats >= 0) {
      g_TFTBuf.setTextSize(2);
      g_TFTBuf.drawString(String(nowAlt) + "m    ", 0, 0);
      g_TFTBuf.setTextSize(2);
      g_TFTBuf.drawString(String(nowSpeed), 0, 50, 8);
      updateDirection(nowHeading);
      updateBrightness(tGPS);
    } else {
      g_TFTBuf.setTextSize(2);
      g_TFTBuf.drawString("-----", 15, 50, 8);
    }
    g_TFTBuf.setTextSize(1);
    g_TFTBuf.setTextColor(TFT_WHITE);
    g_TFTBuf.drawString("km/h", 250, 210);
    g_TFTBuf.setTextColor(TFT_LIGHTGREY);
    g_TFTBuf.drawString("sats: " + String(nowSats), 0, 210);

    // Update Task indicator
    // g_TFTBuf.fillCircle(315, 235, 3, GREEN);
    // g_TFTBuf.fillCircle(315, 235, 3, g_gps_log_data_semaphore ? BLACK : RED);

    g_TFTBuf.pushSprite(0, 0);
    delay(LCDTASK_INTERVAL);
  }
}

// Adjust LCD Brightness [on LCDUpdateTask]
// Dim 18pm to 6am
void updateBrightness(TinyGPSPlus& gps) {
  // Change Night Mode to Adjust Brightness
  static int converted_hour = (gps.time.hour() + kTimeOffsetHour >= 24) ? gps.time.hour() + kTimeOffsetHour - 24 : gps.time.hour() + kTimeOffsetHour;
  static bool isNight = converted_hour <= 6 || converted_hour >= 18;

  if (isNight) {
    M5.Lcd.setBrightness(200);
  } else {
    M5.Lcd.setBrightness(200);
  }
}

// Update Direction Info [on LCDUpdateTask]
void updateDirection(int nowCourse) {
  static const int width = (int)(45.0 / 2.0 + 1.0);
  static String dispStr;
  static uint16_t strColor;

  if (nowCourse > 360 || nowCourse < 0) {
    dispStr = "-- ";
  } else if (nowCourse >= 360 - width || nowCourse <= 0 + width) {
    dispStr = "N  ";
    strColor = TFT_RED;
  } else if (45 - width <= nowCourse && nowCourse <= 45 + width) {
    dispStr = "NE ";
    strColor = TFT_ORANGE;
  } else if (90 - width <= nowCourse && nowCourse <= 90 + width) {
    dispStr = "E  ";
    strColor = TFT_YELLOW;
  } else if (135 - width <= nowCourse && nowCourse <= 135 + width) {
    dispStr = "SE ";
    strColor = TFT_GREENYELLOW;
  } else if (180 - width <= nowCourse && nowCourse <= 180 + width) {
    dispStr = "S  ";
    strColor = TFT_GREEN;
  } else if (225 - width <= nowCourse && nowCourse <= 225 + width) {
    dispStr = "SW ";
    strColor = TFT_CYAN;
  } else if (270 - width <= nowCourse && nowCourse <= 270 + width) {
    dispStr = "W  ";
    strColor = TFT_DARKCYAN;
  } else if (315 - width <= nowCourse && nowCourse <= 315 + width) {
    dispStr = "NW ";
    strColor = TFT_PURPLE;
  }

  g_TFTBuf.setTextSize(2);
  g_TFTBuf.setTextColor(strColor);
  g_TFTBuf.drawString(dispStr, 240, 0);
}

// -----------------------------------------------------------
// Serial Through Mode to Configure GPS Module from PC
// -----------------------------------------------------------
void serialThroughMode(uint32_t baud) {
  g_TFTBuf.fillSprite(TFT_BLACK);
  g_TFTBuf.setTextColor(TFT_RED);
  g_TFTBuf.drawString("Serial Through Mode", 0, 0);
  g_TFTBuf.setTextColor(TFT_WHITE);

  g_TFTBuf.drawString("NEO-M8N (GPS Module):", 0, 60);
  g_TFTBuf.drawString(String(baud), 140, 90);
  g_TFTBuf.drawString("(bps)", 240, 90);

  g_TFTBuf.drawString("USB Serial (PC):", 0, 150);
  g_TFTBuf.drawString("(bps)", 240, 180);
  g_TFTBuf.drawString("115200", 140, 180);

  g_TFTBuf.pushSprite(0, 0);

  while (1) {
    // GPS module to PC
    while (hSerial.available() > 0) {
      Serial.write(hSerial.read());
    }

    // PC to GPS module
    while (Serial.available() > 0) {
      hSerial.write(Serial.read());
    }
  }
}

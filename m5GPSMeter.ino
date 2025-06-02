// GPS Speed & Altimeter (M5Unified + M5Canvas version)
#include <M5Unified.h>
#include <TinyGPS++.h>
#include <SD.h>      // for File & SD access
#include "image.c"   // 320×240 RGB565 splash

// Short aliases --------------------------------------------------
#define DISP    M5.Display
#define POWER   M5.Power
#define SPEAKER M5.Speaker

namespace {
  //----------------------------------------------------------------
  // Compile‑time constants
  //----------------------------------------------------------------
  constexpr uint32_t kWriteBufThresh   = 2500;  // flush to SD every N chars
  constexpr int8_t   kUtcOffsetHours   = 9;     // JST
  constexpr uint16_t kGpsTaskPeriodMs  = 10;    // GPS polling
  constexpr uint16_t kLcdTaskPeriodMs  = 500;   // screen refresh
  constexpr uint16_t kSdTaskPeriodMs   = 2000;  // SD flush

  //----------------------------------------------------------------
  // Globals
  //----------------------------------------------------------------
  TinyGPSPlus   gps;                 // NMEA parser
  HardwareSerial& gpsUart = Serial2; // UART2 GPIO16/17
  String         nmeaBuf;            // double‑buffer for SD writes
  volatile bool  bufLock  = false;   // simple semaphore
  M5Canvas       canvas(&DISP);      // off‑screen buffer (was TFT_eSprite)

  //----------------------------------------------------------------
  // Helper functions
  //----------------------------------------------------------------
  void fadeBrightness(uint8_t from, uint8_t to, int stepDelay = 10) {
    if (from == to) return;
    int8_t dir = (from < to) ? 1 : -1;
    for (uint8_t b = from; b != to; b += dir) {
      DISP.setBrightness(b);
      delay(stepDelay);
    }
    DISP.setBrightness(to);
  }

  inline bool isNightJST() {
    int hour = (gps.time.hour() + kUtcOffsetHours) % 24;
    return (hour <= 6 || hour >= 18);
  }

  String makeFilename() {
    int hour = (gps.time.hour() + kUtcOffsetHours) % 24;
    uint8_t day = gps.date.day() + ((gps.time.hour() + kUtcOffsetHours) >= 24);
    char buf[32];
    snprintf(buf, sizeof(buf), "/%04u%02u%02u_%02u%02u%02u.nmea",
             gps.date.year(), gps.date.month(), day,
             hour, gps.time.minute(), gps.time.second());
    return String(buf);
  }
}

//================================================================
//                          FreeRTOS Tasks
//================================================================
void taskGPS(void*) {
  for (;;) {
    while (gpsUart.available() && !bufLock) {
      char c = gpsUart.read();
      gps.encode(c);
      nmeaBuf += c;
    }
    vTaskDelay(pdMS_TO_TICKS(kGpsTaskPeriodMs));
  }
}

void taskLCD(void*) {
  constexpr uint8_t kBrightDay   = 200;
  constexpr uint8_t kBrightNight = 60;

  for (;;) {
    canvas.fillSprite(TFT_BLACK);

    int sats = gps.satellites.value();
    int alt  = static_cast<int>(gps.altitude.meters());
    int spd  = static_cast<int>(gps.speed.kmph() + 0.5f);
    int head = static_cast<int>(gps.course.deg());

    if (sats >= 3) {
      canvas.setTextSize(2);
      canvas.drawString(String(alt) + " m", 0, 0);
      canvas.drawNumber(spd, 0, 50, 8);

      // Direction -------------------------------------------------
      static const char* dirs[] = {"N ", "NE", "E ", "SE",
                                   "S ", "SW", "W ", "NW"};
      static const uint16_t dirClr[] = {TFT_RED, TFT_ORANGE, TFT_YELLOW,
                                        TFT_GREENYELLOW, TFT_GREEN, TFT_CYAN,
                                        TFT_DARKCYAN, TFT_PURPLE};
      int idx = ((head + 22) % 360) / 45;  // 0‑7 bucket
      canvas.setTextColor(dirClr[idx]);
      canvas.drawString(dirs[idx], 240, 0);
    } else {
      canvas.setTextSize(2);
      canvas.drawString("-----", 0, 50, 8);
    }

    // Footer ------------------------------------------------------
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_LIGHTGREY);
    canvas.drawString("sats:" + String(sats), 0, 210);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString("km/h", 250, 210);

    // Auto brightness -------------------------------------------
    DISP.setBrightness(isNightJST() ? kBrightNight : kBrightDay);

    canvas.pushSprite(0, 0);
    vTaskDelay(pdMS_TO_TICKS(kLcdTaskPeriodMs));
  }
}

void taskSD(void*) {
  File fp;
  for (;;) {
    if (gps.satellites.value() >= 0) {
      if (!fp) {
        String fn = makeFilename();
        fp = SD.open(fn, FILE_APPEND);
      }
      if (fp && nmeaBuf.length() > kWriteBufThresh) {
        bufLock = true;
        fp.print(nmeaBuf);
        fp.flush();
        nmeaBuf.clear();
        bufLock = false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(kSdTaskPeriodMs));
  }
}

//================================================================
//                 Serial‑through utility (blocking)
//================================================================
void serialThrough(uint32_t baudGps) {
  canvas.fillSprite(TFT_BLACK);
  canvas.setTextColor(TFT_RED);
  canvas.drawString("Serial Through Mode", 0, 0);
  canvas.setTextColor(TFT_WHITE);
  canvas.drawString("GPS:" + String(baudGps) + " bps", 0, 60);
  canvas.drawString("USB:115200 bps", 0, 90);
  canvas.pushSprite(0, 0);

  Serial.begin(115200);
  for (;;) {
    while (gpsUart.available()) Serial.write(gpsUart.read());
    while (Serial.available())  gpsUart.write(Serial.read());
  }
}

//================================================================
//                         SETUP / LOOP
//================================================================
void setup() {
  auto cfg = M5.config();   // Default config is fine for Core & Core2
  cfg.output_power = true;   // enable AXP192 / IP5306 control
  cfg.internal_spk = true;
  M5.begin(cfg);
  M5.Speaker.begin();
  M5.Speaker.stop();

  DISP.pushImage(0, 0, 320, 240, image_data_Image);
  fadeBrightness(0, 200);

  // sprite setup
  canvas.setColorDepth(8);
  canvas.createSprite(320, 240);
  canvas.setTextFont(4);

  // Mode‑select screen
  canvas.fillSprite(TFT_BLACK);
  canvas.drawString("> Select baud (GPS)", 0, 0);
  canvas.drawString("BtnA:  9600", 0, 30);
  canvas.drawString("BtnC: 115200", 0, 60);
  canvas.pushSprite(0, 0);
  fadeBrightness(200, 255);
  delay(2000);

  M5.update();
  if (M5.BtnA.isPressed()) {
    gpsUart.begin(9600);
    serialThrough(9600);
  }
  if (M5.BtnC.isPressed()) {
    gpsUart.begin(115200);
    serialThrough(115200);
  }

  fadeBrightness(255, 0, 5);
  DISP.fillScreen(TFT_BLACK);

  // Pre‑allocate NMEA buffer
  nmeaBuf.reserve(kWriteBufThresh * 2);

  // GPS @115200 by default
  gpsUart.begin(115200);

  // Tasks --------------------------------------------------------
  xTaskCreatePinnedToCore(taskGPS, "GPS", 4096, nullptr, 15, nullptr, 1);
  xTaskCreatePinnedToCore(taskLCD, "LCD", 4096, nullptr, 10, nullptr, 1);

  if (SD.begin(GPIO_NUM_4)) {  // microSD挿入を判定
    xTaskCreatePinnedToCore(taskSD, "SD", 4096, nullptr, 5, nullptr, 1);
  }
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(10000));
}

// GPS Speed & Altimeter (compact single‑file version)
// ----------------------------------------------------
//  ▸ M5Unified + TinyGPS++
//  ▸ FreeRTOS tasks: GPS / LCD / SD
//  ▸ Auto‑brightness, splash, baud‑select, NMEA logging
// ----------------------------------------------------

#include <M5Unified.h>
#include <SD.h>
#include <TinyGPS++.h>

#include "image.c"  // 320×240 RGB565 splash image

// ----------------------------------------------------
// Compile‑time constants
// ----------------------------------------------------
namespace cfg
{
constexpr int8_t kUtcOffsetHours = 9;    // JST
constexpr uint32_t kFlushThresh = 2500;  // NMEA flush threshold (chars)
constexpr uint16_t kGpsPeriodMs = 10;
constexpr uint16_t kLcdPeriodMs = 500;
constexpr uint16_t kSdPeriodMs = 2000;
constexpr uint8_t kBrightDay = 200;
constexpr uint8_t kBrightNight = 60;
constexpr uint16_t kStackSz = 4096;  // FreeRTOS task stack
}  // namespace cfg

// ----------------------------------------------------
// Globals (kept minimal)
// ----------------------------------------------------
TinyGPSPlus gps;
HardwareSerial& gpsUart = Serial2;  // UART2 GPIO16/17
String nmeaBuf;
SemaphoreHandle_t nmeaMtx = xSemaphoreCreateMutex();
File sdFp;
M5Canvas canvas(&M5.Display);

// ----------------------------------------------------
// Utility helpers
// ----------------------------------------------------
void fadeBrightness(uint8_t from, uint8_t to, int stepDelay = 10) {
  if (from == to) return;
  const int8_t dir = (from < to) ? 1 : -1;
  for (uint8_t b = from; b != to; b = static_cast<uint8_t>(b + dir)) {
    M5.Display.setBrightness(b);
    delay(stepDelay);
  }
  M5.Display.setBrightness(to);
}

bool isNight() {
  const int hour = (gps.time.hour() + cfg::kUtcOffsetHours) % 24;
  return (hour <= 6 || hour >= 18);
}

String makeFilename() {
  int hourLocal = gps.time.hour() + cfg::kUtcOffsetHours;
  const uint8_t carry = hourLocal / 24;
  hourLocal %= 24;
  const uint8_t day = gps.date.day() + carry;
  char buf[32];
  snprintf(buf, sizeof(buf), "/%04u%02u%02u_%02u%02u%02u.nmea",
           gps.date.year(), gps.date.month(), day,
           hourLocal, gps.time.minute(), gps.time.second());
  return String(buf);
}

// ----------------------------------------------------
// FreeRTOS tasks
// ----------------------------------------------------
void taskGPS(void*) {
  for (;;) {
    while (gpsUart.available()) {
      const char c = gpsUart.read();
      gps.encode(c);
      if (xSemaphoreTake(nmeaMtx, 0)) {
        nmeaBuf += c;
        xSemaphoreGive(nmeaMtx);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(cfg::kGpsPeriodMs));
  }
}

void taskLCD(void*) {
  static const char* kDirs[] = {"N ", "NE", "E ", "SE", "S ", "SW", "W ", "NW"};
  static const uint16_t kDirCol[] = {TFT_RED, TFT_ORANGE, TFT_YELLOW, TFT_GREENYELLOW,
                                     TFT_GREEN, TFT_CYAN, TFT_DARKCYAN, TFT_PURPLE};
  for (;;) {
    const int sats = gps.satellites.value();
    const int alt = static_cast<int>(gps.altitude.meters());
    const int spd = static_cast<int>(gps.speed.kmph() + 0.5f);
    const int head = static_cast<int>(gps.course.deg());

    canvas.fillSprite(TFT_BLACK);
    canvas.setTextSize(2);

    if (sats >= 3) {
      canvas.drawString(String(alt) + " m", 0, 0);
      canvas.drawNumber(spd, 0, 50, 8);
      const uint8_t idx = ((head + 22) % 360) / 45;  // 0‑7 bucket
      canvas.setTextColor(kDirCol[idx]);
      canvas.drawString(kDirs[idx], 240, 0);
    } else {
      canvas.drawString("-----", 0, 50, 8);
    }

    canvas.setTextSize(1);
    canvas.setTextColor(TFT_LIGHTGREY);
    canvas.drawString("sats:" + String(sats), 0, 210);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString("km/h", 250, 210);

    M5.Display.setBrightness(isNight() ? cfg::kBrightNight : cfg::kBrightDay);
    canvas.pushSprite(0, 0);

    vTaskDelay(pdMS_TO_TICKS(cfg::kLcdPeriodMs));
  }
}

void taskSD(void*) {
  for (;;) {
    if (gps.satellites.value() >= 0) {  // fix acquired
      if (!sdFp) sdFp = SD.open(makeFilename(), FILE_APPEND);

      if (sdFp && nmeaBuf.length() > cfg::kFlushThresh) {
        if (xSemaphoreTake(nmeaMtx, pdMS_TO_TICKS(100))) {
          sdFp.print(nmeaBuf);
          sdFp.flush();
          nmeaBuf.clear();
          xSemaphoreGive(nmeaMtx);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(cfg::kSdPeriodMs));
  }
}

// ----------------------------------------------------
// Serial‑through (blocking) – transparent UART bridge
// ----------------------------------------------------
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
    while (Serial.available()) gpsUart.write(Serial.read());
  }
}

// ----------------------------------------------------
// Setup & Loop – Arduino entry points
// ----------------------------------------------------
void setup() {
  // M5 init -----------------------------------------------------------
  auto sys = M5.config();
  sys.output_power = true;
  sys.internal_spk = true;
  M5.begin(sys);
  M5.Speaker.begin();
  M5.Speaker.stop();

  // Splash -----------------------------------------------------------
  M5.Display.pushImage(0, 0, 320, 240, image_data_Image);
  fadeBrightness(0, 200);

  // Canvas -----------------------------------------------------------
  canvas.setColorDepth(8);
  canvas.createSprite(320, 240);
  canvas.setTextFont(4);

  // Mode select ------------------------------------------------------
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
    serialThrough(9600);  // never returns
  } else if (M5.BtnC.isPressed()) {
    gpsUart.begin(115200);
    serialThrough(115200);  // never returns
  }

  fadeBrightness(255, 0, 5);
  M5.Display.fillScreen(TFT_BLACK);

  // Runtime init ------------------------------------------------------
  nmeaBuf.reserve(cfg::kFlushThresh * 2);
  gpsUart.begin(115200);

  // Tasks -------------------------------------------------------------
  xTaskCreatePinnedToCore(taskGPS, "GPS", cfg::kStackSz, nullptr, 15, nullptr, 1);
  xTaskCreatePinnedToCore(taskLCD, "LCD", cfg::kStackSz, nullptr, 10, nullptr, 1);
  if (SD.begin(GPIO_NUM_4)) {
    xTaskCreatePinnedToCore(taskSD, "SD", cfg::kStackSz, nullptr, 5, nullptr, 1);
  }
}

void loop() { vTaskDelay(pdMS_TO_TICKS(10'000)); }

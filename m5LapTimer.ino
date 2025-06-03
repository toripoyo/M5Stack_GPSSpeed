// -----------------------------------------------------------------------------
//  Lap Timer PRO (M5Stack Core / Gray) – UI polished with reference layout
//  * init updated: 8‑bit off‑screen buffer @ 320×240 (requested)
// -----------------------------------------------------------------------------
#include <M5Unified.h>
#include <TinyGPS++.h>
#include <SPI.h>    // ★ 追加
#include <SD.h>

namespace cfg
{
constexpr uint32_t BAUD_PC = 115200;
constexpr uint32_t BAUD_GPS = 115200;
constexpr float LAT0 = 35.3698692322f;
constexpr float LON0 = 138.9336547852f;
constexpr float RAD_DEF_M = 15.0f;
constexpr char CSV[] = "/lap_log.csv";
constexpr uint8_t TXT = 2;
constexpr uint32_t FLUSH_MS = 5000;
constexpr bool ENABLE_SD = true;
}  // namespace cfg

struct Snapshot {
  float lat = NAN, lon = NAN, alt = NAN, vKmh = NAN, distM = NAN;
  uint8_t sats = 0;
  uint16_t yr = 0;
  uint8_t mon = 0, day = 0, hr = 0, min = 0, sec = 0;
  uint32_t epochMs = 0;
};

struct LapInfo {
  float sec = NAN;
  float topSpeed = NAN;
};

class GpsReader {
 public:
  void begin(HardwareSerial& s) { ser_ = &s; }
  bool update(Snapshot& o) {
    while (ser_->available()) gps_.encode(ser_->read());
    o.lat = gps_.location.lat();
    o.lon = gps_.location.lng();
    o.alt = gps_.altitude.meters();
    o.vKmh = gps_.speed.kmph();
    o.sats = gps_.satellites.value();
    o.yr = gps_.date.year();
    o.mon = gps_.date.month();
    o.day = gps_.date.day();
    o.hr = (gps_.time.hour() + 9) % 24;
    o.min = gps_.time.minute();
    o.sec = gps_.time.second();
    o.distM = gps_.distanceBetween(o.lat, o.lon, lat0_, lon0_);
    o.epochMs = millis();
    return true;
  }
  void setOrigin(float la, float lo) {
    lat0_ = la;
    lon0_ = lo;
  }

 private:
  HardwareSerial* ser_{};
  TinyGPSPlus gps_;
  float lat0_ = cfg::LAT0, lon0_ = cfg::LON0;
};

class LapEngine {
 public:
  void setRadius(float r) { rad_ = r; }
  float radius() const { return rad_; }
  bool update(const Snapshot& s, LapInfo& o) {
    if (std::isnan(s.distM)) return false;
    if (!inside_ && s.distM <= rad_) {
      inside_ = true;
      entry_ = s.epochMs;
      top_ = 0;
    } else if (inside_ && s.distM > rad_) {
      inside_ = false;
      uint32_t mid = entry_ + (s.epochMs - entry_) / 2;
      o.sec = (laps_ == 0 ? NAN : (mid - prev_) / 1000.0f);
      prev_ = mid;
      ++laps_;
      o.topSpeed = top_;
      for (int i = 4; i > 0; --i) hist_[i] = hist_[i - 1];
      hist_[0] = o;
      if (laps_ == 1) hist_[0].sec = NAN;
      if (laps_ > 1 && (std::isnan(best_) || o.sec < best_)) best_ = o.sec;
      avg_ = 0;
      int n = 0;
      for (int i = 0; i < 5 && !std::isnan(hist_[i].sec); ++i) {
        avg_ += hist_[i].sec;
        ++n;
      }
      if (n) avg_ /= n;
      return true;
    }
    if (inside_ && s.vKmh > top_) top_ = s.vKmh;
    return false;
  }
  uint32_t lapCount() const { return laps_; }
  const LapInfo& last(int i) const { return hist_[i]; }
  float best() const { return best_; }
  float avg() const { return avg_; }

 private:
  float rad_ = cfg::RAD_DEF_M;
  bool inside_ = false;
  uint32_t entry_ = 0, prev_ = 0, laps_ = 0;
  float top_ = 0;
  LapInfo hist_[5];
  float best_ = NAN, avg_ = NAN;
};

class CsvLogger {
 public:
  bool begin() {
    if (!cfg::ENABLE_SD) return false;
    if (!SD.begin()) return false;
    f_ = SD.open(cfg::CSV, FILE_APPEND);
    if (!f_) return false;
    if (f_.size() == 0) f_.println("lap,time,top,v_kmh,yyyy/mm/dd-hh:mm:ss");
    nxt_ = millis() + cfg::FLUSH_MS;
    return true;
  }
  void add(uint32_t l, const LapInfo& i, const Snapshot& s) {
    if (!f_) return;
    f_.printf("%lu,%.3f,%.1f,%.1f,%04u/%02u/%02u-%02u:%02u:%02u\n", l, i.sec, i.topSpeed, s.vKmh, s.yr, s.mon, s.day, s.hr, s.min, s.sec);
  }
  void loop() {
    if (f_ && millis() > nxt_) {
      f_.flush();
      nxt_ = millis() + cfg::FLUSH_MS;
    }
  }

 private:
  File f_;
  uint32_t nxt_ = 0;
};

M5Canvas g_buf(&M5.Display);
class Ui {
 public:
  void begin() {
    M5.Display.setBrightness(255);
    g_buf.setColorDepth(8);
    g_buf.createSprite(320, 240);
    g_buf.setTextSize(cfg::TXT);
    g_buf.setTextColor(TFT_WHITE, TFT_BLACK);
    g_buf.fillScreen(TFT_BLACK);
    g_buf.pushSprite(0, 0);
    ok_ = true;
  }
  void draw(const Snapshot& s, const LapEngine& e) {
    auto& g = g_buf;
    if (!ok_) {
      M5.Display.fillScreen(TFT_BLACK);
      return;
    }
    g.fillScreen(TFT_BLACK);
    g.setCursor(0, 0);
    g.setTextSize(1);
    g.printf("%04u/%02u/%02u %02u:%02u:%02u  GPS:%02u\n", s.yr, s.mon, s.day, s.hr, s.min, s.sec, s.sats);
    g.setTextSize(2);
    g.printf("Lap %2lu  Last %.3fs\n", e.lapCount(), e.last(0).sec);
    float diff = e.last(0).sec - e.last(1).sec;
    g.setTextColor(diff > 0 ? TFT_RED : TFT_BLUE, TFT_BLACK);
    g.printf("Diff %+.1fs\n", diff);
    g.setTextColor(TFT_WHITE, TFT_BLACK);
    g.printf("Speed %5.1f km/h  Dist %.1f m\n", s.vKmh, s.distM);
    g.printf("Best %.3fs  Avg %.3fs\n", e.best(), e.lapCount() > 1 ? e.avg() : NAN);
    g.setTextSize(1);
    g.printf("R=%2.0fm BtnA:Zero BtnB:+5m BtnC:Lap\n", e.radius());
    g.pushSprite(0, 0);
  }

 private:
  bool ok_ = false;
};

GpsReader gps;
LapEngine laps;
CsvLogger logger;
Ui ui;

void setup() {
  auto c = M5.config();
  c.output_power = true;
  c.internal_spk = true;
  M5.begin(c);
  M5.Speaker.begin();
  M5.Speaker.stop();
  Serial.begin(cfg::BAUD_PC);
  Serial2.begin(cfg::BAUD_GPS);
  gps.begin(Serial2);
  ui.begin();
  logger.begin();
}

void loop() {
  Snapshot s;
  gps.update(s);
  M5.update();
  if (M5.BtnA.wasPressed()) { gps.setOrigin(s.lat, s.lon); }
  if (M5.BtnB.wasPressed()) {
    float r = laps.radius();
    r = r >= 50 ? 5 : r + 5;
    laps.setRadius(r);
  }
  bool manual = M5.BtnC.isPressed();
  LapInfo li;
  bool cross = laps.update(s, li) || (manual && laps.lapCount() > 0);
  if (cross) logger.add(laps.lapCount(), li, s);
  ui.draw(s, laps);
  logger.loop();
}

// -----------------------------------------------------------------------------
//  Lap Timer (M5Stack Core / Core Gray) – Flicker‑free version (M5Canvas buffer)
//  * Dependencies: M5Unified, TinyGPS++, SD
//  * Uses a single off‑screen M5Canvas (g_buf) for tear‑free UI updates.
//  * No <M5GFX.h> dependency.
// -----------------------------------------------------------------------------
#include <M5Unified.h>
#include <TinyGPS++.h>
#include <SD.h>

/* === 設定値 =============================================================== */
namespace cfg {
constexpr uint32_t  baudPc      = 115200;          // PC serial speed
constexpr uint32_t  baudGps     = 115200;          // GPS module serial speed
constexpr float     originLat   = 35.3698692322f;  // start/finish latitude
constexpr float     originLon   = 138.9336547852f; // start/finish longitude
constexpr float     radiusM     = 15.0f;           // start circle radius [m]
constexpr char      csvPath[]   = "/lap_log.csv";  // CSV file path
constexpr uint8_t   lcdTextSize = 2;               // text size on LCD
constexpr uint32_t  flushMs     = 5000;            // SD flush period [ms]
constexpr bool      enableSdLog = true;            // enable SD logging
} // namespace cfg

/* === Data structures ====================================================== */
struct Snapshot {
    //float    lat   = NAN;
    //float    lon   = NAN;
    //float    alt   = NAN;
    //float    vKmh  = NAN;
    //uint8_t  sats  = 0;
    //uint32_t epochMs = 0;
    float    lat   = 5;
    float    lon   = 6;
    float    alt   = 259;
    float    vKmh  = 192;
    uint8_t  sats  = 5;
    uint32_t epochMs = 98245;
};

struct LapStat {
    uint32_t index   = 0;
    float    seconds = NAN;
};

/* === GPS reader =========================================================== */
class GpsReader {
public:
    void begin(HardwareSerial& s) { serial_ = &s; }
    bool update(Snapshot& out) {
        while (serial_->available()) gps_.encode(serial_->read());
        out.lat   = gps_.location.lat();
        out.lon   = gps_.location.lng();
        out.alt   = gps_.altitude.meters();
        out.vKmh  = gps_.speed.kmph();
        out.sats  = gps_.satellites.value();
        out.epochMs = millis();
        return true;
    }
private:
    HardwareSerial* serial_{};
    TinyGPSPlus gps_;
};

/* === Lap detector (outside→inside→outside, mid‑point) ===================== */
class LapDetector {
public:
    LapDetector(float lat0, float lon0, float r)
        : lat0_(lat0), lon0_(lon0), r_(r) {}

    bool onSnapshot(const Snapshot& s, LapStat& out) {
        const float dist = distanceM(s.lat, s.lon, lat0_, lon0_);
        if (!inside_ && dist <= r_) {
            inside_ = true;
            entryMs_ = s.epochMs;
        } else if (inside_ && dist > r_) {
            inside_ = false;
            const uint32_t exitMs   = s.epochMs;
            const uint32_t centerMs = entryMs_ + (exitMs - entryMs_) / 2;
            out.index   = lapCnt_;
            out.seconds = (lapCnt_ == 0) ? NAN :
                          (centerMs - prevCenterMs_) / 1000.0f;
            prevCenterMs_ = centerMs;
            ++lapCnt_;
            if (lapCnt_ > 1 && (out.seconds < best_ || std::isnan(best_))) {
                best_ = out.seconds;
            }
            return true;
        }
        return false;
    }
    float bestSec() const { return best_; }
private:
    static float distanceM(float la1,float lo1,float la2,float lo2) {
        constexpr float R = 6371000.0f;
        float dLat = radians(la2 - la1);
        float dLon = radians(lo2 - lo1);
        float a = sinf(dLat/2)*sinf(dLat/2) +
                  cosf(radians(la1))*cosf(radians(la2))*sinf(dLon/2)*sinf(dLon/2);
        return R * 2 * atan2f(sqrtf(a), sqrtf(1 - a));
    }
    float     lat0_, lon0_, r_;
    bool      inside_ = false;
    uint32_t  entryMs_ = 0;
    uint32_t  prevCenterMs_ = 0;
    uint32_t  lapCnt_ = 0;
    float     best_ = NAN;
};

/* === CSV logger =========================================================== */
class CsvLogger {
public:
    bool begin() {
        if (!SD.begin()) return false;
        file_ = SD.open(cfg::csvPath, FILE_APPEND);
        if (!file_) return false;
        if (file_.size() == 0) {
            file_.println("index,time(ms),lat,lon,alt,vKmh,sats,lapSec,bestSec");
        }
        nextFlush_ = millis() + cfg::flushMs;
        return true;
    }
    void log(const Snapshot& s, const LapStat& lap, float best) {
        file_.printf("%lu,%lu,%.7f,%.7f,%.1f,%.1f,%u,",
                     lap.index, s.epochMs, s.lat, s.lon, s.alt, s.vKmh, s.sats);
        std::isnan(lap.seconds) ? file_.print("-") : file_.print(lap.seconds, 3);
        file_.print(",");
        std::isnan(best) ? file_.print("-") : file_.print(best, 3);
        file_.println();
        if (millis() >= nextFlush_) {
            file_.flush();
            nextFlush_ = millis() + cfg::flushMs;
        }
    }
private:
    File file_;
    uint32_t nextFlush_ = 0;
};

/* === Off‑screen buffered UI ============================================== */
M5Canvas g_buf(&M5.Display);

class DisplayUi {
public:
    void begin() {
        M5.Display.setBrightness(255);
        g_buf.setColorDepth(8);
        g_buf.createSprite(320, 240);
        g_buf.setTextSize(cfg::lcdTextSize);
        g_buf.setTextColor(TFT_WHITE, TFT_BLACK);
        g_buf.fillScreen(TFT_BLACK);
        g_buf.pushSprite(0, 0);
    }
    void draw(const Snapshot& s, const LapStat& lap, float best) {
        bool changed = (satsPrev_ != s.sats) || (vPrev_ != s.vKmh) ||
                       (lapPrev_ != lap.index) || (lapSecPrev_ != lap.seconds) ||
                       (bestPrev_ != best);
        if (!changed) return;
        g_buf.fillScreen(TFT_BLACK);
        g_buf.setCursor(0, 0);
        g_buf.printf("Sat %2u  Spd %5.1f km/h\n", s.sats, s.vKmh);
        g_buf.printf("Lap %2lu  Last %6.2f s\n", lap.index, lap.seconds);
        g_buf.printf("Best     %6.2f s\n", best);
        g_buf.pushSprite(0,0);
        satsPrev_ = s.sats;
        vPrev_ = s.vKmh;
        lapPrev_ = lap.index;
        lapSecPrev_ = lap.seconds;
        bestPrev_ = best;
    }
private:
    uint8_t  satsPrev_{255};
    float    vPrev_ = NAN;
    float    lapSecPrev_ = NAN;
    float    bestPrev_ = NAN;
    uint32_t lapPrev_ = UINT32_MAX;
};

/* === Application ========================================================== */
class LapTimerApp {
public:
    void setup() {
        auto cfgM5 = M5.config();
        cfgM5.output_power = true;
        cfgM5.internal_spk = true;
        M5.begin(cfgM5);
        M5.Speaker.begin();
        M5.Speaker.stop();
        //M5.Speaker.end();

        Serial.begin(cfg::baudPc);
        Serial2.begin(cfg::baudGps);
        gps_.begin(Serial2);
        ui_.begin();

        if constexpr (cfg::enableSdLog) {
            okSd_ = logger_.begin();
            if (!okSd_) uiWarn("SD not found - logging OFF");
        }
    }
    void loop() {
        Snapshot snap;
        gps_.update(snap);
        LapStat lap;
        bool crossed = detector_.onSnapshot(snap, lap);
        ui_.draw(snap, lap, detector_.bestSec());
        if (crossed && okSd_) logger_.log(snap, lap, detector_.bestSec());
        M5.update();
    }
private:
    void uiWarn(const char* msg) {
        auto& d = M5.Display;
        g_buf.fillRect(0, 0, d.width(), 18, TFT_YELLOW);
        g_buf.setTextColor(TFT_BLACK, TFT_YELLOW);
        g_buf.setTextSize(1);
        g_buf.setCursor(2, 4);
        g_buf.print(msg);
        g_buf.pushSprite(0,0);
        g_buf.setTextColor(TFT_WHITE, TFT_BLACK);
        g_buf.setTextSize(cfg::lcdTextSize);
    }
    GpsReader   gps_;
    LapDetector detector_{cfg::originLat, cfg::originLon, cfg::radiusM};
    CsvLogger   logger_;
    DisplayUi   ui_;
    bool        okSd_ = false;
};

/* === Arduino entry points ================================================ */
LapTimerApp app;
void setup() { app.setup(); }
void loop()  { app.loop(); }

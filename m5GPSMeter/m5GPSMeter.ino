/* --------------------------------------------------------------------------
 *  LapTimerApp for M5Stack + TinyGPS++
 *  （スプライト描画 + “外→内→外” 中点ラップ検出アルゴリズム版）
 *  2025‑05‑24  ChatGPT によりリファクタリング
 *
 *  ■概要
 *    ・TinyGPS++ で取得した現在位置をもとに，原点（cfg::originLat/Lon）中心の半径 cfg::radiusM
 *      の円を“スタート／ゴールライン”とみなしラップタイムを計測。
 *    ・位置が『円の外 → 内 → 外』と遷移した区間の**ちょうど中間時刻**を円中心通過タイムとすることで，
 *      半径分の誤差を減らし，速度が変動しても平均化されるようにした。
 *    ・計測結果は LCD へちらつき無しのスプライト一括描画，SD カードへ CSV 追記。
 * ------------------------------------------------------------------------*/
#include <M5Stack.h>
#include <TinyGPS++.h>
#include <SD.h>
#include <TFT_eSPI.h>        // TFT_eSprite を使用

/* === 設定値（書き換えやすいように名前空間にまとめる） ==================== */
namespace cfg {
constexpr uint32_t  baudPc      = 115200;              // PC とのシリアル速度
constexpr uint32_t  baudGps     = 115200;              // GPS モジュールとのシリアル速度
constexpr float     originLat   = 35.3698692322f;      // 原点緯度
constexpr float     originLon   = 138.9336547852f;     // 原点経度
constexpr float     radiusM     = 5.0f;                // スタート円半径[m]
constexpr char      csvPath[]   = "/lap_log.csv";      // CSV ファイルパス
constexpr uint8_t   lcdTextSize = 2;                   // LCD 文字サイズ
constexpr uint32_t  flushMs     = 5'000;               // SD flush 周期[ms]
}

/* === 値オブジェクト ========================================================== */
struct Snapshot {
    float  lat   = NAN;
    float  lon   = NAN;
    float  alt   = NAN;
    float  vKmh  = NAN;
    uint8_t sats = 0;
    uint32_t epochMs = 0;   // 取得時の millis()
};

struct LapStat {
    uint32_t index   = 0;   // 今回のラップ番号（0,1,2…）
    float    seconds = NAN; // 今回ラップの所要時間[s]
};

/* === GPS 読み取りクラス ====================================================== */
class GpsReader {
public:
    void begin(HardwareSerial& s) { serial_ = &s; }

    /**
     * @brief GPS から最新データを取り込み Snapshot に反映。
     * @return 新しい位置が得られたら true
     */
    bool update(Snapshot& out)
    {
        while (serial_->available()) gps_.encode(serial_->read());
        if (!gps_.location.isUpdated()) return false;   // 緯度経度が更新されていなければ何もしない

        // Snapshot に丸ごとコピー
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
    TinyGPSPlus     gps_;
};

/* === ラップ判定クラス（外→内→外の中点を採用） ============================== */
class LapDetector {
public:
    explicit LapDetector(float lat0, float lon0, float radius)
        : lat0_(lat0), lon0_(lon0), r_(radius) {}

    /**
     * @brief 新しい測位情報を解析し，ラップ完了時に LapStat を返す。
     * @param s      現在位置のスナップショット
     * @param lapOut ラップ完了時のみ有効な情報が入る
     * @retval true  ラップが完了した（lapOut が有効）
     * @retval false ラップは完了していない
     */
    bool onSnapshot(const Snapshot& s, LapStat& lapOut)
    {
        const float dist = distanceM(s.lat, s.lon, lat0_, lon0_);

        // --- 状態遷移 ----------------------------------------------------
        if (!inside_ && dist <= r_) {
            // ★外→内 へ入った瞬間 : entry 時刻保存
            inside_  = true;
            entryMs_ = s.epochMs;
        }
        else if (inside_ && dist > r_) {
            // ★内→外 へ出た瞬間 : exit 時刻取得し center（中点）時刻計算
            inside_ = false;
            const uint32_t exitMs   = s.epochMs;
            const uint32_t centerMs = entryMs_ + (exitMs - entryMs_) / 2; // entry と exit のちょうど中間

            // ラップ情報生成
            lapOut.index   = lapCnt_;
            lapOut.seconds = (lapCnt_ == 0) ? NAN                        // 1周目は基準がないので計測不可
                                           : (centerMs - prevCenterMs_) / 1000.0f;
            prevCenterMs_  = centerMs;
            ++lapCnt_;

            // ベストラップ更新
            if (lapCnt_ > 1 && (lapOut.seconds < best_ || std::isnan(best_))) {
                best_ = lapOut.seconds;
            }
            return true;   // ラップ完了を通知
        }
        return false;      // まだラップ完了していない
    }

    float bestSec() const { return best_; }

private:
    /**
     * @brief ハバースイン距離計算（球面三角法）
     *        精度より速度重視なので float／組込み sinf/cosf を使用。
     */
    static float distanceM(float la1,float lo1,float la2,float lo2)
    {
        constexpr float R = 6371000.0f;         // 地球半径[m]
        float dLat = radians(la2 - la1);
        float dLon = radians(lo2 - lo1);
        float a = sinf(dLat/2)*sinf(dLat/2) +
                  cosf(radians(la1))*cosf(radians(la2))*sinf(dLon/2)*sinf(dLon/2);
        return R * 2 * atan2f(sqrtf(a), sqrtf(1-a));
    }

    /* ---- 設定値 ---- */
    float lat0_, lon0_, r_;

    /* ---- 動的状態 ---- */
    bool     inside_        = false;   // 現在 円内にいるか？
    uint32_t entryMs_       = 0;       // 円に入った時刻（外→内）
    uint32_t prevCenterMs_  = 0;       // 1周前の中心通過時刻
    uint32_t lapCnt_        = 0;       // 完了したラップ数
    float    best_          = NAN;     // ベストラップ
};

/* === CSV へログ出力 ========================================================== */
class CsvLogger {
public:
    bool begin()
    {
        if (!SD.begin()) return false;
        file_ = SD.open(cfg::csvPath, FILE_APPEND);
        if (!file_) return false;

        // 新規作成時のみヘッダ行を書く
        if (file_.size() == 0)
            file_.println("index,time(ms),lat,lon,alt,vKmh,sats,lapSec,bestSec");
        nextFlush_ = millis() + cfg::flushMs;
        return true;
    }

    /**
     * @brief ラップ完了時に 1 行追記。
     */
    void log(const Snapshot& s, const LapStat& lap, float best)
    {
        file_.printf("%lu,%lu,%.7f,%.7f,%.1f,%.1f,%u,",
                     lap.index, s.epochMs,
                     s.lat, s.lon, s.alt, s.vKmh, s.sats);
        std::isnan(lap.seconds) ? file_.print("-") : file_.print(lap.seconds, 3);
        file_.print(",");
        std::isnan(best) ? file_.print("-") : file_.print(best, 3);
        file_.println();

        // 指定周期ごとにまとめて flush して SD 寿命を延ばす
        const uint32_t now = millis();
        if (now >= nextFlush_) {
            file_.flush();
            nextFlush_ = now + cfg::flushMs;
        }
    }
private:
    File     file_;
    uint32_t nextFlush_{0};
};

/* === LCD 表示クラス（スプライトでちらつき防止） ============================== */
class DisplayUi {
public:
    void begin()
    {
        M5.Lcd.setBrightness(255);
        // 画面サイズのスプライトを 1 度だけ生成
        sprite_.createSprite(M5.Lcd.width(), M5.Lcd.height());
        sprite_.setTextSize(cfg::lcdTextSize);
        sprite_.setTextColor(WHITE, BLACK);   // 前景, 背景
        sprite_.fillSprite(BLACK);
        sprite_.pushSprite(0, 0);
    }

    /**
     * @brief ステータス更新。内容が変わったときだけ再描画。
     */
    void draw(const Snapshot& s, const LapStat& lap, float best)
    {
        bool changed = (satsPrev_ != s.sats) ||
                       (vPrev_   != s.vKmh) ||
                       (lapPrev_ != lap.index) ||
                       (lapSecPrev_ != lap.seconds) ||
                       (bestPrev_   != best);
        if (!changed) return;

        sprite_.fillSprite(BLACK);
        sprite_.setCursor(0, 0);
        sprite_.printf("Sat %2u  Spd %5.1f km/h\n", s.sats, s.vKmh);
        sprite_.printf("Lap %2lu  Last %6.2f s\n", lap.index, lap.seconds);
        sprite_.printf("Best     %6.2f s\n", best);
        sprite_.pushSprite(0, 0);

        // 前回値を保存して差分判定に利用
        satsPrev_    = s.sats;
        vPrev_       = s.vKmh;
        lapPrev_     = lap.index;
        lapSecPrev_  = lap.seconds;
        bestPrev_    = best;
    }
private:
    TFT_eSprite sprite_ = TFT_eSprite(&M5.Lcd);

    // 直前に表示した値を覚えておき差分時のみ描画
    uint8_t  satsPrev_{255};
    float    vPrev_ = NAN, lapSecPrev_ = NAN, bestPrev_ = NAN;
    uint32_t lapPrev_{UINT32_MAX};
};

/* === アプリ全体をまとめるクラス ============================================ */
class LapTimerApp {
public:
    void setup()
    {
        Serial.begin(cfg::baudPc);
        Serial2.begin(cfg::baudGps);
        M5.begin();
        M5.Speaker.end();

        gps_.begin(Serial2);
        ui_.begin();
        okSd_ = logger_.begin();
        if (!okSd_) uiError("SD init failed");
    }

    void loop()
    {
        Snapshot snap;
        if (!gps_.update(snap)) return;      // GPS が更新されるまで何もしない

        LapStat lap;
        const bool crossed = detector_.onSnapshot(snap, lap); // ラップ検出
        ui_.draw(snap, lap

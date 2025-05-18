#include <M5Stack.h>
#include <TinyGPS++.h>
#include <SD.h>

// ---------- Constants --------------------------------------------------------
constexpr uint32_t SERIAL_BAUD_PC   = 115200;
constexpr uint32_t SERIAL_BAUD_GPS  = 115200;
constexpr float    ORIGIN_LAT       = 35.3698692322f;
constexpr float    ORIGIN_LON       = 138.9336547852f;
constexpr float    LAP_RADIUS_M     = 5.0f;         // [m]
constexpr char     LOG_FILE[]       = "/lap_log.csv";

// ---------- Types -----------------------------------------------------------
struct GpsSnapshot {
    float   lat        = 0.0f;
    float   lon        = 0.0f;
    float   altitude   = 0.0f;
    float   speedKmh   = 0.0f;
    uint8_t satellites = 0;
    tm      time       = {};
};

struct LapInfo {
    uint32_t index     = 0;
    float    duration  = 0.0f;  // [s]
};

// ---------- Globals ---------------------------------------------------------
TinyGPSPlus gps;
File        logFile;
GpsSnapshot current;
GpsSnapshot previous;

LapInfo lastLap;
float   bestLap = FLT_MAX;

// ---------- Helpers ---------------------------------------------------------
static void openLogFile()
{
    if (logFile) return;

    logFile = SD.open(LOG_FILE, FILE_WRITE);
    if (!logFile) {
        Serial.println("Failed to open log file!");
        M5.Lcd.println("SD open error");
    } else if (logFile.size() == 0) {
        // header
        logFile.println(
            "index,yyyy/mm/dd,hh:mm:ss,lat,lon,alt[m],speed[km/h],sat,lap[s],best[s]");
        logFile.flush();
    }
}

static void logCsv(const LapInfo& lap)
{
    openLogFile();

    // Compose timestamp
    char dateBuf[16];
    snprintf(dateBuf, sizeof(dateBuf), "%04d/%02d/%02d",
             current.time.tm_year + 1900,
             current.time.tm_mon  + 1,
             current.time.tm_mday);
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d",
             current.time.tm_hour,
             current.time.tm_min,
             current.time.tm_sec);

    logFile.printf("%lu,%s,%s,%.7f,%.7f,%.1f,%.1f,%u,",
                   lap.index,
                   dateBuf,
                   timeBuf,
                   current.lat,
                   current.lon,
                   current.altitude,
                   current.speedKmh,
                   current.satellites);

    if (lap.index == 0) {
        logFile.print("-");
    } else {
        logFile.printf("%.3f", lap.duration);
    }
    logFile.print(",");
    if (bestLap < FLT_MAX) {
        logFile.printf("%.3f", bestLap);
    } else {
        logFile.print("-");
    }
    logFile.println();
    logFile.flush();
}

static float haversineDistanceM(float lat1, float lon1, float lat2, float lon2)
{
    // naive *fast enough* haversine
    constexpr float R = 6371000.0f; // Earth radius [m]
    float dLat = radians(lat2 - lat1);
    float dLon = radians(lon2 - lon1);
    float a = sinf(dLat / 2) * sinf(dLat / 2) +
              cosf(radians(lat1)) * cosf(radians(lat2)) *
              sinf(dLon / 2) * sinf(dLon / 2);
    float c = 2 * atan2f(sqrtf(a), sqrtf(1 - a));
    return R * c;
}

static bool crossedLapLine()
{
    static bool inside = false;

    const float dist = haversineDistanceM(current.lat, current.lon,
                                          ORIGIN_LAT, ORIGIN_LON);

    if (!inside && dist <= LAP_RADIUS_M) {
        inside = true;
        return true;    // entering origin circle -> new lap
    }
    if (inside && dist > LAP_RADIUS_M) {
        inside = false; // exited circle
    }

    return false;
}

static void updateDisplay(const LapInfo& lap)
{
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.printf("Sat %2u  Speed %5.1f km/h\n", current.satellites, current.speedKmh);
    M5.Lcd.printf("Lap %2lu  Last %6.2f s  Best %6.2f s\n",
                  lap.index, lap.duration, bestLap);
}

// ---------- Arduino setup / loop --------------------------------------------
void setup()
{
    // Serial
    Serial.begin(SERIAL_BAUD_PC);
    Serial2.begin(SERIAL_BAUD_GPS);

    // Peripherals
    M5.begin();
    M5.Speaker.end();
    M5.Lcd.setBrightness(255);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(2);

    // SD
    if (!SD.begin()) {
        Serial.println("SD mount failed");
        M5.Lcd.println("SD mount failed");
    }

    openLogFile();
}

void loop()
{
    // --- Read GPS bytes -----------------------------------------------------
    while (Serial2.available() > 0) {
        gps.encode(Serial2.read());
    }

    if (gps.location.isUpdated()) {
        // snapshot
        current.lat        = gps.location.lat();
        current.lon        = gps.location.lng();
        current.altitude   = gps.altitude.meters();
        current.speedKmh   = gps.speed.kmph();
        current.satellites = gps.satellites.value();

        if (gps.time.isValid()) {
            current.time.tm_hour = gps.time.hour();
            current.time.tm_min  = gps.time.minute();
            current.time.tm_sec  = gps.time.second();
        }
        if (gps.date.isValid()) {
            current.time.tm_year = gps.date.year() - 1900;
            current.time.tm_mon  = gps.date.month() - 1;
            current.time.tm_mday = gps.date.day();
        }

        // --- Lap detection --------------------------------------------------
        static uint32_t lapCounter = 0;
        static uint32_t lapStartMs = millis();

        LapInfo lap = {lapCounter, 0.0f};

        if (crossedLapLine()) {
            const uint32_t now = millis();
            if (lapCounter > 0) {
                lap.duration = (now - lapStartMs) / 1000.0f;
                if (lap.duration < bestLap) bestLap = lap.duration;
            }
            lap.index = lapCounter;
            lapStartMs = now;
            lapCounter++;
        }

        // --- Output ---------------------------------------------------------
        updateDisplay(lap);
        logCsv(lap);
    }

    // --- Handle button events ----------------------------------------------
    if (M5.BtnB.wasReleased()) {
        Serial.println("Button B pressed");
    }
    if (M5.BtnC.wasReleased()) {
        Serial.println("Button C pressed");
    }

    M5.update();
}

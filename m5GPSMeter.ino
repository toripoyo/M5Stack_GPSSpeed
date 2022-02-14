#include <M5Stack.h>
#include <TinyGPS++.h>

#include "image.c"

// Global variable
TinyGPSPlus tGPS;                                    // TinyGPS++ object
HardwareSerial hSerial(2);                           // NEO-M8N serial
String g_gps_log_data = "";                          // SD write buffer(fixed size reserved)
bool g_gps_log_data_semaphore = true;                // SD write buffer semaphore
static const uint32_t kWriteBufferThreshold = 2000;  // SD write buffer threshold
TFT_eSprite g_TFTBuf = TFT_eSprite(&M5.Lcd);         // Display Buffer

// Task define
#define LCDTASK_INTERVAL 500
#define LCDTASK_PRIORITY 10  // High
#define SDTASK_INTERVAL 3000
#define SDTASK_PRIORITY 5  // Low

// Task Update Flag
static bool g_isWritingSD = false;

void setup() {
    M5.begin();
    g_gps_log_data.reserve(kWriteBufferThreshold * 3);

    // Display init
    g_TFTBuf.setColorDepth(8);
    g_TFTBuf.createSprite(320, 240);
    g_TFTBuf.setTextFont(4);
    g_TFTBuf.setTextSize(2);
    g_TFTBuf.setTextColor(TFT_WHITE);

    // Button init
    pinMode(BUTTON_A_PIN, INPUT_PULLUP);
    pinMode(BUTTON_B_PIN, INPUT_PULLUP);
    pinMode(BUTTON_C_PIN, INPUT_PULLUP);

    // Speaker Noise Reduce
    M5.Speaker.mute();
    pinMode(25, OUTPUT);
    digitalWrite(25, LOW);

    M5.Power.begin();
    M5.Power.setPowerVin(true);

    // Opening Display
    for (int i = 0; i < 200; i += 3) {
        M5.Lcd.setBrightness(i);
        M5.Lcd.pushImage(0, 0, 320, 240, image_data_Image);
    }
    delay(1000);
    for (int i = 200; i > 0; i -= 5) {
        M5.Lcd.setBrightness(i);
        delay(10);
    }

    // Mode Select Display
    g_TFTBuf.setTextFont(4);
    g_TFTBuf.setTextSize(1);
    g_TFTBuf.fillSprite(TFT_BLACK);
    g_TFTBuf.drawString("> Select NEO-M8N baud to", 0, 30);
    g_TFTBuf.drawString("    enter serial through mode", 0, 60);
    g_TFTBuf.drawString("9600", 30, 200);
    g_TFTBuf.drawString("115200", 220, 200);
    g_TFTBuf.pushSprite(0, 0);
    for (int i = 0; i < 200; i += 5) {
        M5.Lcd.setBrightness(i);
        delay(10);
    }
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
    for (int i = 200; i > 0; i -= 5) {
        M5.Lcd.setBrightness(i);
        delay(10);
    }

    // Initial Black Screen
    g_TFTBuf.fillSprite(TFT_BLACK);
    g_TFTBuf.pushSprite(0, 0);
    for (int i = 200; i > 0; i -= 5) {
        M5.Lcd.setBrightness(i);
        delay(10);
    }

    // You need configure Baud of the GPS module before use
    hSerial.begin(115200);  // set 9600bps when you use GPS moudle in default settings

    // Start Display task
    xTaskCreatePinnedToCore(LCDUpdateTask, "LCDUpdateTask", 8192, NULL, LCDTASK_PRIORITY, NULL, 1);
    // Start SD Card write task
    xTaskCreatePinnedToCore(SDWriteTask, "SDWriteTask", 8192, NULL, SDTASK_PRIORITY, NULL, 1);
}

// [Task Pri=1] Get GPS Data (no wait)
void loop() {
    static char temp_chr;

    // Receive GPS Data and update TGPS object
    while (hSerial.available() > 0 && g_gps_log_data_semaphore) {
        temp_chr = hSerial.read();
        tGPS.encode(temp_chr);
        g_gps_log_data += String(temp_chr);
    }
}

// [Task Pri=5] Update Screen Info
void LCDUpdateTask(void* args) {
    static int nowAlt, nowSats, nowSpeed, nowHeading;

    while (1) {
        nowAlt = (int)(tGPS.altitude.meters());
        nowSats = tGPS.satellites.value();
        nowSpeed = (int)(tGPS.speed.kmph() - 0.5);
        nowHeading = (int)(tGPS.course.deg());

        g_TFTBuf.fillSprite(TFT_BLACK);
        g_TFTBuf.setTextColor(TFT_WHITE);

        if (nowSats >= 3) {
            g_TFTBuf.setTextSize(2);
            g_TFTBuf.drawString(String(nowAlt) + "m    ", 0, 0);
            g_TFTBuf.setTextSize(2);
            g_TFTBuf.drawString(String(nowSpeed), 0, 50, 8);
            updateDirection(nowHeading);
            updateBrightness();
        } else {
            g_TFTBuf.setTextSize(2);
            g_TFTBuf.drawString("-----", 15, 50, 8);
        }
        g_TFTBuf.setTextSize(1);
        g_TFTBuf.setTextColor(TFT_WHITE);
        g_TFTBuf.drawString("km/h", 250, 210);
        g_TFTBuf.setTextColor(TFT_LIGHTGREY);
        g_TFTBuf.drawString("sats: " + String(nowSats), 0, 210);
        //g_TFTBuf.pushSprite(0, 0);

        // Update Task indicator
        //g_TFTBuf.fillCircle(315, 235, 3, GREEN);
        g_TFTBuf.fillCircle(305, 235, 3, g_isWritingSD ? RED : BLACK);
        g_TFTBuf.pushSprite(0, 0);
        delay(LCDTASK_INTERVAL);
    }
}

// [Task Pri=10] Update SD Card Data
void SDWriteTask(void* args) {
    static File sd_f;
    static String fname;

    while (1) {
        if (tGPS.satellites.value() > 3) {
            if (!sd_f) {
                fname = "/" + String(tGPS.date.year()) + "-" + String(tGPS.date.month()) + "-" + String((tGPS.time.hour() + 9 > 24) ? tGPS.date.day() + 1 : tGPS.date.day()) +
                        "_" + String(tGPS.time.hour() + 9) + "-" + String(tGPS.time.minute()) + "-" + String(tGPS.time.second()) + ".txt";
                sd_f = SD.open(fname, FILE_APPEND);
            }
            if (g_gps_log_data.length() > kWriteBufferThreshold) {
                if (sd_f) {
                    g_isWritingSD = true;              // update flag
                    g_gps_log_data_semaphore = false;  // Stop write new data to this string
                    sd_f.print(g_gps_log_data);
                    sd_f.flush();
                    g_gps_log_data.clear();
                    g_gps_log_data_semaphore = true;  // Allow write new data to this string
                    g_isWritingSD = false;
                }
            }
        }
        delay(SDTASK_INTERVAL);
    }
}

// Adjust LCD Brightness [on LCDUpdateTask]
void updateBrightness(void) {
    static bool isNight, temp;

    // Change Night Mode to Adjust Brightness
    temp = tGPS.time.hour() + 9 >= 18 || tGPS.time.hour() + 9 <= 6;
    if (isNight != temp) {
        isNight = temp;
        if (isNight) {
            M5.Lcd.setBrightness(60);
        } else {
            M5.Lcd.setBrightness(200);
        }
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

#include <M5GFX.h>
#include <M5Unified.h>
#include <TinyGPS++.h>

#include "SD.h"
TinyGPSPlus gps;

File file;

// 保存するファイル名
String fname = "/LAP_log.csv";

int YEAR, MONTH, DAY, HOUR, MINUTE, SECOND, LapCount, SatVal, BestLapNum;

// LAT0、LONG0は起動時の初期原点です(サンプルはFSWカートコースです)

// 左のボタンで原点を現在位置に設定可能
float LAT0 = 35.3698692322, LONG0 = 138.9336547852, LAT, LONG, KMPH, TopSpeed, ALTITUDE, distanceToMeter0, BeforeTime, LAP, LAP1, LAP2, LAP3, LAP4, LAP5, BestLap = 99999, AverageLap, Sprit;
boolean LAPCOUNTNOW, LAPRADchange;

float LAPRAD = 5;  // ラップ計測のトリガー(原点からの半径)距離

long lastdulation;

void setup() {
  SD.begin();
  Serial.begin(115200);

  // GPSモジュールのボーレートに合わせてください(購入時は多分9600bps)
  Serial2.begin(115200);
  M5.begin();
  M5.Speaker.end();
  M5.Lcd.setBrightness(255);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.print("Start");
  LapCount = 0;

  file = SD.open(fname, FILE_APPEND);
  file.println("LAPCount,LapTime,TopSpeed,YYYY/MM/DD/Hour:Minute:Second");
  file.close();
}

void loop() {
  //**********GPSから値を読み込み代入
  ReadGPS();

  //**********条件によるラップ計測
  CountLAP();

  //**********M5Stackへ描画
  showvalue(1000);
}

void ReadGPS() {
  //**********GPS形式をライブラリで扱えるよう変換
  while (Serial2.available()) {
    char c = Serial2.read();
    gps.encode(c);
    Serial.print(c);
  }
  //**********GPSデーターをもとに各値へ変換
  LAT = gps.location.lat();
  LONG = gps.location.lng();
  YEAR = gps.date.year();
  MONTH = gps.date.month();
  DAY = gps.date.day();
  HOUR = gps.time.hour();
  MINUTE = gps.time.minute();
  SECOND = gps.time.second();
  KMPH = gps.speed.kmph();
  ALTITUDE = gps.altitude.meters();
  distanceToMeter0 = gps.distanceBetween(gps.location.lat(), gps.location.lng(), LAT0, LONG0);
  SatVal = gps.satellites.value();

  //**********ラップ計測中の最高速度を代入
  if (TopSpeed < KMPH) {
    TopSpeed = KMPH;
  }

  //**********日本標準時(JST)へ変換
  HOUR += 9;
  if (HOUR >= 24) {
    DAY += HOUR / 24;
    HOUR = HOUR % 24;
  }

  //**********相対距離原点設定
  M5.update();
  if (M5.BtnA.isPressed()) {
    LAT0 = LAT;
    LONG0 = LONG;
    distanceToMeter0 = gps.distanceBetween(gps.location.lat(), gps.location.lng(), LAT0, LONG0);
  }

  if (M5.BtnB.isPressed() == false && LAPRADchange == true) {
    LAPRADchange = false;
  }

  if (M5.BtnB.isPressed() == true && LAPRADchange == false) {
    if (LAPRAD == 50) {
      LAPRAD = 0;
    }
    LAPRAD += 5;
    LAPRADchange = true;
    // delay(200);
  }
}

void CountLAP() {
  if (distanceToMeter0 >= LAPRAD && M5.BtnC.isPressed() == false && LAPCOUNTNOW == true) {
    LAPCOUNTNOW = false;
  }

  //**********4ラップ計測
  if (((distanceToMeter0 != 0 && distanceToMeter0 <= LAPRAD) || M5.BtnC.isPressed() == true) && LAPCOUNTNOW == false && ((millis() - BeforeTime) / 1000) > 10) {
    if (LapCount > 0) {
      LAP5 = LAP4;
      LAP4 = LAP3;
      LAP3 = LAP2;
      LAP2 = LAP1;
      LAP1 = LAP;

      LAP = (millis() - BeforeTime) / 1000;
      BeforeTime = millis();
      if (LAP < BestLap) {
        BestLap = LAP;
        BestLapNum = LapCount + 1;
      }
      writeData();

      Sprit += LAP;
      if (LapCount > 1) {
        AverageLap = Sprit / LapCount;
      }
    } else {
      BeforeTime = millis();
    }

    LapCount++;
    LAPCOUNTNOW = true;
  }
}

void showvalue(int dulation) {
  if (millis() > lastdulation + dulation) {
    lastdulation = millis();
    //**********ボタン説明
    M5.Lcd.clear();
    M5.Lcd.setTextColor(ORANGE);
    M5.Lcd.setTextSize(1);

    M5.Lcd.setCursor(15, 228);
    M5.Lcd.setTextColor(ORANGE);
    M5.Lcd.print("SET ");
    M5.Lcd.setTextColor(CYAN);
    M5.Lcd.print("Zero");
    M5.Lcd.setTextColor(ORANGE);
    M5.Lcd.print("-Point");

    M5.Lcd.setTextColor(ORANGE);
    M5.Lcd.setCursor(120, 228);
    M5.Lcd.print("Rad= ");
    M5.Lcd.setTextColor(47072);
    M5.Lcd.print(LAPRAD);
    M5.Lcd.print(" m");

    M5.Lcd.setTextColor(ORANGE);
    M5.Lcd.setCursor(230, 228);
    M5.Lcd.print("Lap Count");

    //**********時刻表示
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(5, 0);
    M5.Lcd.print(YEAR);
    M5.Lcd.print("/");
    M5.Lcd.print(MONTH);
    M5.Lcd.print("/");
    M5.Lcd.print(DAY);
    M5.Lcd.print(" ");
    M5.Lcd.print(HOUR);
    M5.Lcd.print(":");
    M5.Lcd.print(MINUTE);
    M5.Lcd.print(":");
    M5.Lcd.println(SECOND);

    //**********衛星数表示
    M5.Lcd.setTextColor(CYAN);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(245, 5);
    M5.Lcd.print("G P S:");
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(285, 1);
    M5.Lcd.print(SatVal);

    //**********前ラップ表示
    M5.Lcd.fillRect(0, 20, 320, 59, YELLOW);  // 黄色ベタ表示
    M5.Lcd.setTextColor(BLACK);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(15, 30);
    M5.Lcd.print(LapCount);
    M5.Lcd.print(">");
    M5.Lcd.setTextSize(6);
    M5.Lcd.print(LAP, 3);

    //**********タイム差表示
    // M5.Lcd.drawRect(0, 80, 170, 50, WHITE); //枠だけ

    if (LAP - LAP1 <= 0) {
      M5.Lcd.fillRect(1, 80, 178, 50, BLUE);
    }
    if (LAP - LAP1 > 0) {
      M5.Lcd.fillRect(1, 80, 178, 50, RED);
    }

    M5.Lcd.setTextColor(BLACK);
    M5.Lcd.setCursor(10, 92);
    M5.Lcd.setTextSize(4);
    if (LAP - LAP1 > 0) {
      M5.Lcd.print("+");
    }
    M5.Lcd.print(LAP - LAP1, 1);

    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setCursor(8, 90);
    M5.Lcd.setTextSize(4);
    if (LAP - LAP1 > 0) {
      M5.Lcd.print("+");
    }
    M5.Lcd.print(LAP - LAP1, 1);

    //**********計測時間表示
    M5.Lcd.drawRoundRect(180, 80, 140, 50, 10, WHITE);  // 枠だけ

    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setCursor(190, 90);
    M5.Lcd.setTextSize(4);
    M5.Lcd.print((millis() - BeforeTime) / 1000, 0);

    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(300, 110);
    M5.Lcd.print("s");

    //**********ベストラップ表示
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(20, 145);
    M5.Lcd.print("Best(");
    M5.Lcd.print(BestLapNum);
    M5.Lcd.print(")");
    if (BestLap != 99999) {
      M5.Lcd.setCursor(120, 140);
      M5.Lcd.setTextSize(3);
      M5.Lcd.print("> ");
      M5.Lcd.print(BestLap);
    }

    //**********平均タイム表示
    M5.Lcd.setTextColor(CYAN);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(20, 175);
    M5.Lcd.print("Average");
    if (AverageLap != 0) {
      M5.Lcd.setCursor(120, 170);
      M5.Lcd.setTextSize(3);
      M5.Lcd.print("> ");
      M5.Lcd.print(AverageLap);
    }

    //**********平均ラップとの差表示
    M5.Lcd.fillRect(10, 200, 300 * ((AverageLap - (millis() - BeforeTime) / 1000) / AverageLap), 25, PINK);
    M5.Lcd.drawRect(10, 200, 300, 25, WHITE);  // 枠だけ

    //**********時速表示
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setCursor(20, 205);
    M5.Lcd.setTextSize(2);
    M5.Lcd.print(KMPH, 1);
    M5.Lcd.setTextSize(2);
    M5.Lcd.print(" km/h");

    //**********原点との相対距離表示
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(1);
    // M5.Lcd.setCursor(140, 205);
    // M5.Lcd.print("Distance=");
    M5.Lcd.setCursor(160, 205);
    M5.Lcd.setTextSize(2);
    M5.Lcd.print(distanceToMeter0, 1);
    M5.Lcd.print(" m");
  }
}


/*
void writeData() {
  // SDカードへの書き込み処理（ファイル追加モード）
  file = SD.open(fname, FILE_APPEND);

  file.print((String)LapCount + ",");
  file.print((String)LAP + ",");
  file.print((String)TopSpeed + ",");
  file.println((String)YEAR + "/" + (String)MONTH + "/" + (String)DAY + "-" + (String)HOUR + ":" + (String)MINUTE + ":" + (String)SECOND + ",");
  file.close();
  TopSpeed = 0;  // 最高速度をリセット
}
*/

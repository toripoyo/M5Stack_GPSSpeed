#include <M5Stack.h>
#include <TinyGPS++.h>

TinyGPSPlus tGPS;
HardwareSerial hSerial(2);

void serialThroughMode();
void updateScreen(TinyGPSPlus);
void updateDirection(int);

void setup()
{
  M5.begin();
  M5.Lcd.setTextSize(2);
  M5.Lcd.drawString("Select Baud To Connect PC", 0, 90);
  M5.Lcd.drawString("9600", 30, 200);
  M5.Lcd.drawString("115200", 220, 200);
  M5.Lcd.setTextFont(4);
  Serial.begin(115200);
  pinMode(BUTTON_A_PIN, INPUT_PULLUP);
  pinMode(BUTTON_B_PIN, INPUT_PULLUP);
  pinMode(BUTTON_C_PIN, INPUT_PULLUP);

  // Speaker Noise Reduce
  M5.Speaker.mute();
  pinMode(25, OUTPUT);
  digitalWrite(25, LOW);

  M5.Power.begin();
  M5.Power.setPowerVin(false);
  delay(2000);

  // Serial Through Mode (To Setup Module)
  M5.update();
  if (M5.BtnA.isPressed())
  {
    hSerial.begin(9600);
    serialThroughMode();
  }
  if (M5.BtnC.isPressed())
  {
    hSerial.begin(115200);
    serialThroughMode();
  }

  // You need configure Baud of the GPS module before use
  hSerial.begin(115200);
  delay(1000);
  M5.Lcd.fillScreen(TFT_BLACK);
  //M5.Lcd.setRotation(5);
}

void loop()
{
  // Receive GPS Data
  while (hSerial.available() > 0)
  {
    tGPS.encode(hSerial.read());
  }

  // Change Night Mode to Adjust Brightness
  bool isNight = tGPS.time.hour() + 9 >= 18 || tGPS.time.hour() + 9 <= 6;
  if (isNight)
  {
    M5.Lcd.setBrightness(60);
  }
  else
  {
    M5.Lcd.setBrightness(200);
  }

  // Draw Information
  updateScreen(&tGPS);
}

// Update Screen Info
unsigned int oldAlt = 0;
unsigned int oldSpeed = 0;
unsigned int oldHeading = 0;
void updateScreen(TinyGPSPlus *gps)
{
  int nowAlt = (int)(gps->altitude.meters());
  int nowSats = gps->satellites.value();
  int nowSpeed = (int)(gps->speed.kmph() - 0.5);
  int nowHeading = (int)(gps->course.deg());

  M5.Lcd.setTextColor(TFT_WHITE);
  if (nowSats >= 3)
  {
    if (nowAlt != oldAlt)
    {
      M5.Lcd.setTextSize(2);
      M5.Lcd.fillRect(0, 0, 230, 47, TFT_BLACK);
      M5.Lcd.drawString(String(nowAlt) + "m    ", 0, 0);
    }
    if (nowSpeed != oldSpeed)
    {
      M5.Lcd.setTextSize(3);
      M5.Lcd.fillRect(15, 60, 300, 150, TFT_BLACK);
      M5.Lcd.drawString(String(nowSpeed), 15, 60, 7);
    }
    if (nowHeading != oldHeading)
    {
      updateDirection(nowHeading);
    }
  }
  else
  {
    M5.Lcd.setTextSize(2);
    M5.Lcd.fillRect(10, 60, 300, 150, TFT_BLACK);
    M5.Lcd.drawString("-----", 5, 60, 7);
    delay(1000);
  }
  M5.Lcd.setTextSize(1);
  M5.Lcd.drawString("km/h", 260, 210);
  //M5.Lcd.drawString(String(nowSats), 280, 0);

  oldAlt = nowAlt;
  oldSpeed = nowSpeed;
  oldHeading = nowHeading;
}

// Update Direction Info
void updateDirection(int nowCourse)
{
  const int width = (int)(45.0 / 2.0 + 1.0);
  String dispStr;
  uint16_t strColor;

  if (nowCourse > 360 || nowCourse < 0)
  {
    dispStr = "-- ";
  }
  else if (nowCourse >= 360 - width || nowCourse <= 0 + width)
  {
    dispStr = "N  ";
    strColor = TFT_RED;
  }
  else if (45 - width <= nowCourse && nowCourse <= 45 + width)
  {
    dispStr = "NE ";
    strColor = TFT_ORANGE;
  }
  else if (90 - width <= nowCourse && nowCourse <= 90 + width)
  {
    dispStr = "E  ";
    strColor = TFT_YELLOW;
  }
  else if (135 - width <= nowCourse && nowCourse <= 135 + width)
  {
    dispStr = "SE ";
    strColor = TFT_GREENYELLOW;
  }
  else if (180 - width <= nowCourse && nowCourse <= 180 + width)
  {
    dispStr = "S  ";
    strColor = TFT_GREEN;
  }
  else if (225 - width <= nowCourse && nowCourse <= 225 + width)
  {
    dispStr = "SW ";
    strColor = TFT_CYAN;
  }
  else if (270 - width <= nowCourse && nowCourse <= 270 + width)
  {
    dispStr = "W  ";
    strColor = TFT_DARKCYAN;
  }
  else if (315 - width <= nowCourse && nowCourse <= 315 + width)
  {
    dispStr = "NW ";
    strColor = TFT_PURPLE;
  }

  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(strColor);
  M5.Lcd.fillRect(230, 0, 90, 47, TFT_BLACK);
  M5.Lcd.drawString(dispStr, 240, 0);
}

// -----------------------------------------------------------
// Serial Through Mode to Configure GPS Module from PC
void serialThroughMode()
{
  M5.Lcd.drawString("Serial Through Mode", 0, 120);
  while (1)
  {
    while (hSerial.available() > 0)
    {
      Serial.write(hSerial.read());
    }
    while (Serial.available() > 0)
    {
      hSerial.write(Serial.read());
    }
  }
}

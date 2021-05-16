#include <M5Stack.h>
#include <TinyGPS++.h>

#include "image.c"

TinyGPSPlus tGPS;
HardwareSerial hSerial(2);  // NEO-M8N
static uint32_t g_connect_baud = 0;

void serialThroughMode();
void updateScreen(TinyGPSPlus);
void updateDirection(int);

// Display Double Buffer
TFT_eSprite g_TFTBuf = TFT_eSprite(&M5.Lcd);

void setup()
{
  M5.begin();

  // display init
  g_TFTBuf.setColorDepth(8);
  g_TFTBuf.createSprite(320, 240);
  g_TFTBuf.setTextFont(4);
  g_TFTBuf.setTextSize(2);
  g_TFTBuf.setTextColor(TFT_WHITE);

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

  // Opening
  for(int i=0;i<200;i+=3)
  {
    M5.Lcd.setBrightness(i);
    M5.Lcd.pushImage(0,0,320,240,image_data_Image);
  }
  delay(1000);
  for(int i=200;i>0;i-=5){M5.Lcd.setBrightness(i);delay(10);}
  
  // Mode Select
  g_TFTBuf.setTextFont(4);
  g_TFTBuf.setTextSize(1);
  g_TFTBuf.fillSprite(TFT_BLACK);
  g_TFTBuf.drawString("> Select NEO-M8N baud to", 0, 30);
  g_TFTBuf.drawString("    enter serial through mode", 0, 60);
  g_TFTBuf.drawString("9600", 30, 200);
  g_TFTBuf.drawString("115200", 220, 200);
  g_TFTBuf.pushSprite(0, 0);
  for(int i=0;i<200;i+=5){M5.Lcd.setBrightness(i);delay(10);}
  delay(2000);

  M5.update();
  if (M5.BtnA.isPressed())
  {
    hSerial.begin(9600);
    g_connect_baud = 9600;
    serialThroughMode();
  }
  if (M5.BtnC.isPressed())
  {
    hSerial.begin(115200);
    g_connect_baud = 115200;
    serialThroughMode();
  }

  // You need configure Baud of the GPS module before use
  //hSerial.begin(9600);    // use GPS moudle in default settings
  hSerial.begin(115200);
  
  for(int i=200;i>0;i-=5){M5.Lcd.setBrightness(i);delay(10);}
  g_TFTBuf.fillSprite(TFT_BLACK);
  g_TFTBuf.pushSprite(0, 0);
  for(int i=200;i>0;i-=5){M5.Lcd.setBrightness(i);delay(10);}
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
  }else{
    M5.Lcd.setBrightness(200);
  }

  // Draw Information
  updateScreen(&tGPS);
  delay(500);
}

// Update Screen Info
void updateScreen(TinyGPSPlus *gps)
{
  int nowAlt = (int)(gps->altitude.meters());
  int nowSats = gps->satellites.value();
  int nowSpeed = (int)(gps->speed.kmph() - 0.5);
  int nowHeading = (int)(gps->course.deg());

  g_TFTBuf.fillSprite(TFT_BLACK);
  g_TFTBuf.setTextColor(TFT_WHITE);

  if (nowSats >= 3)
  {
      g_TFTBuf.setTextSize(2);
      g_TFTBuf.drawString(String(nowAlt) + "m    ", 0, 0);
      g_TFTBuf.setTextSize(2);
      g_TFTBuf.drawString(String(nowSpeed), 15, 50, 8);
      updateDirection(nowHeading);
  }
  else
  {
    g_TFTBuf.setTextSize(2);
    g_TFTBuf.drawString("-----", 15, 50, 8);
  }
  g_TFTBuf.setTextSize(1);
  g_TFTBuf.setTextColor(TFT_WHITE);
  g_TFTBuf.drawString("km/h", 250, 210);
  g_TFTBuf.setTextColor(TFT_LIGHTGREY);
  g_TFTBuf.drawString("sats: " + String(nowSats), 0, 210);
  g_TFTBuf.pushSprite(0, 0);
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

  g_TFTBuf.setTextSize(2);
  g_TFTBuf.setTextColor(strColor);
  g_TFTBuf.drawString(dispStr, 240, 0);
}

// -----------------------------------------------------------
// Serial Through Mode to Configure GPS Module from PC
void serialThroughMode()
{ 

  g_TFTBuf.fillSprite(TFT_BLACK);
  g_TFTBuf.setTextColor(TFT_RED);
  g_TFTBuf.drawString("Serial Through Mode", 0, 0);
  g_TFTBuf.setTextColor(TFT_WHITE);
  
  g_TFTBuf.drawString("NEO-M8N (GPS Module):", 0, 60);
  g_TFTBuf.drawString(String(g_connect_baud), 140, 90);
  g_TFTBuf.drawString("(bps)", 240, 90);
  
  g_TFTBuf.drawString("USB Serial (PC):", 0, 150);
  g_TFTBuf.drawString("(bps)", 240, 180);
  g_TFTBuf.drawString("115200", 140, 180);
  
  g_TFTBuf.pushSprite(0, 0);
 
  while (1)
  {
    // GPS module to PC
    while (hSerial.available() > 0)
    {
      Serial.write(hSerial.read());
    }
   
    // PC to GPS module
    while (Serial.available() > 0) 
    {
      hSerial.write(Serial.read());
    }
  }
}

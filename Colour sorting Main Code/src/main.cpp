#include <Wire.h>
#include <Arduino.h>
#include <Servo.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_GFX.h>
#include "functions.h"
#include <EEPROM.h>

#define BAUDRATE 230400

#define S2 A1
#define S3 A2
#define PIEZO 11
#define SR1 9
#define RLYSIG 5
#define COUT 4
#define NEO 12

#define BUTTONS 5 // Its a pain to count enums
#define BTNTOL 3

#define BTN A3 // The pin we use for buttons
enum BUTTON
{
BTNSELECT = 0,
BTNUP = 32,
BTNDOWN = 207,
BTNBACK = 311,
BTNNONE = 1023
};
BUTTON lastBtn;

bool btnACK;
bool btnCLEAR;
short btnDebounce = 50; // how long before we take action
long btnLastDebounce;
long lastBtnVal;
volatile int sensorValue; // force the arduino to always check the value

static volatile unsigned int RGBC[4] = {0, 0, 0, 0};
unsigned int RGBCAverage[4] = {0, 0, 0, 0};
void (*functionPointer)(void);

#define EEPROM_CALIB_DATA_ADDR 0

const char colourNames[9][8] = {"Red", "Green", "Blue","Black", "Yellow", "Purple", "Orange","White","Calibr"};
struct SaveData
{
  unsigned short saveVersion = 0;
  unsigned short colours[9][4] = {{0, 0, 0, 0},{0, 0, 0, 0},{0, 0, 0, 0},{0, 0, 0, 0},{0, 0, 0, 0},{0, 0, 0, 0},{0, 0, 0, 0},{0, 0, 0, 0},{0, 0, 0, 0}}; //black, white, red, green, blue, yellow, purple,orange
};

#define WIPEDATA 0
SaveData saveData;
SaveData saveDataDefault;

Servo cServo;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);              // Width, Height, I2C, NoReset
//Adafruit_NeoPixel strip(5, NEO, NEO_GRB + NEO_KHZ800); // Amount of LEDS in use, Pin, Colour Type, Neopixle speed

enum Tones //insted of having to call multiple functions to handle each tone i can use 1 function with meaningfull names to chose which tone to play
{
TONEACTIVATED = 0,
TONEALERT = 1,
TONEWARN = 2,
TONEACCEPT = 4,
TONEDENY = 5,
TONECHIRP
};
void servoMove(bool Accept){
  if (Accept)
  {
    cServo.write(0);
    delay(500);
  } else
  {
    cServo.write(180);
    delay(500);
  }

  cServo.write(90);
  delay(300);


}

void soundTone(Tones Play)
{
  switch (Play)
  {
  case TONEACTIVATED:
    tone(PIEZO, 33);
    delay(100);
    tone(PIEZO, 65);
    delay(100);
    tone(PIEZO, 131);
    delay(100);
    noTone(PIEZO);
    break;
  case TONEALERT:
    tone(PIEZO, 500);
    delay(100);
    tone(PIEZO, 431);
    delay(100);
    noTone(PIEZO);
    break;
  

  case TONEACCEPT:
    tone(PIEZO, 250);
    delay(50);
    tone(PIEZO, 441);
    delay(50);
    noTone(PIEZO);
    break;

  case TONEDENY:
    tone(PIEZO, 441);
    delay(50);
    tone(PIEZO, 250);
    delay(50);
    noTone(PIEZO);
    break;
  case TONECHIRP:
    tone(PIEZO, 441);
    delay(50);
    noTone(PIEZO);
    break;

  default:
    break;
  }
}

int getPulse(int timeout)
{
  if (pulseInLong(COUT, LOW, timeout * 1000)) // if we get a pulse we ignore the first pulse for stability //100 because we want milli insted of micro seconds
  
    return pulseInLong(COUT, LOW); // we read the next pulse and return it immidietly
  
  else
    return -1;
}

void getRGBC()
{
  digitalWrite(S2, LOW);
  digitalWrite(S3, LOW);
  RGBC[0] = getPulse(1000);

  digitalWrite(S2, HIGH);
  digitalWrite(S3, HIGH);
  RGBC[1] = getPulse(1000);

  digitalWrite(S2, LOW);
  digitalWrite(S3, HIGH);
  RGBC[2] = getPulse(1000);

  digitalWrite(S2, HIGH);
  digitalWrite(S3, LOW);
  RGBC[3] = getPulse(1000);
};

void getAverage(int samples, unsigned short *outR, unsigned short *outG, unsigned short *outB, unsigned short *outC, void (*callback)(int, int))
{
  long sumR = 0, sumG = 0, sumB = 0, sumC = 0;

  for (int i = 0; i < samples; i++)
  {
    getRGBC(); // read sensor values

    sumR += RGBC[0];
    sumG += RGBC[1];
    sumB += RGBC[2];
    sumC += RGBC[3];

    
    if (callback != NULL) {
      callback(i, samples); // Call the callback function with the current iteration value and when it will finish
    }

    *outR = sumR / samples;
    *outG = sumG / samples;
    *outB = sumB / samples;
    *outC = sumC / samples;
  }
}

bool range(long value, long setValue, long margin)
{ // The range function returns a boolean depending if the given value is within a margen of the setValue
  if (value >= (setValue - margin) && value <= (setValue + margin))
  {
    return true;
  }
  return false;
}

short btnPressed(){
  short temp = lastBtn;
  return temp;
}

bool btnCheck(BUTTON btnSelect)// manages button presses when called
{            
  sensorValue = analogRead(BTN);
  lastBtnVal = sensorValue;
  lastBtn = btnSelect;

  if (lastBtnVal != sensorValue)// if there is ANY change to the analog pin make note
  {
    btnLastDebounce = millis(); // make note of the current millis we have
    Serial.print(F("btnCheck|I|Button Value Changed: "));
    Serial.print(sensorValue);
    Serial.print(F(" Value was: "));
    Serial.println(lastBtnVal);
  }

  if (range(sensorValue, BTNNONE, 1)) // check if the user has let go. we throw conformation about the user letting go incase they
  {
    btnACK = false; // we set the button acknowlage to false since nothing has happend
    btnLastDebounce = millis();
    return false;
  }
  if ((millis() - btnLastDebounce) > btnDebounce && !btnACK)
  {
    if (!btnACK) // check we havent just looped or another button has just been pressed/Acknowlaged
    {
      if (range(sensorValue, btnSelect, BTNTOL)) // we check to see the button we are pressing is in tolorance of what we want to see
      {
        btnLastDebounce = millis();
        btnACK = true; // we make sure we cant come back and repeatedly return true. once the function is run we wait untill nothing is pressed
        return true;
      }
    }
  }

  return false;
}

// void colorWipe(uint32_t color)
// {
//   for (int i = 0; i < strip.numPixels(); i++)
//   {                                // For each pixel in strip...
//     strip.setPixelColor(i, color); //  Set pixel's color (in RAM)
//     strip.show();                  //  Update strip to match
//   }
// }

void initServo()
{
  cServo.detach();
  cServo.attach(SR1);
}

void drawProgressBar(int x, int y, int width, int progress, int maxProgress) {

  // Draw the outline of the progress bar
  oled.drawRoundRect(x, y, width, 8, 2, SSD1306_WHITE);

  // Calculate the width of the filled rectangle
  int fillWidth = map(progress, 0, maxProgress, 0, width);

  // Draw the filled rectangle
  if (fillWidth > 0) {
    oled.drawRoundRect(x + 1, y + 1, fillWidth+1, 8 - 2,2, SSD1306_WHITE);
  }

  // Update the display to show the progress bar
  oled.display();
}

void oledPrintText(int x, int y, int size, bool inverted, bool center, const char* print){
  if (center)
  {
    x = x + (strlen_P(reinterpret_cast<PGM_P>(print)) * (size*6))/2;
  y = y-6*size;
  }
  
  oled.setCursor(x,y);
  oled.setTextSize(size);
  if (inverted)
  {
    oled.setTextColor(BLACK,WHITE);
  } else oled.setTextColor(WHITE,BLACK);
  
  oled.print(print);
}

void oledPrintText(int x, int y, int size, bool inverted, bool center, const __FlashStringHelper *print ){
  if (center)
  {
    x = x - (strlen_P(reinterpret_cast<PGM_P>(print))/2 * (size*8));
  y = y-6*size;
  }
  oled.setCursor(x,y);
  oled.setTextSize(size);
  if (inverted)
  {
    oled.setTextColor(BLACK,WHITE);
  } else oled.setTextColor(WHITE,BLACK);
  
  oled.print(print);
}

void calibrationProggress(int proggress, int maxProgress){
  drawProgressBar(0,32,120,proggress, maxProgress);
}


void viewData(){
  SaveData saveData;
  
  oled.setTextWrap(false);
  oled.setTextColor(BLACK,WHITE);
  oled.setTextSize(1);
  for (unsigned short col = 0; col < 8; col++)
  {
    oled.setCursor(24 * col, 6);
    oled.print(colourNames[col][0]);
    Serial.print(colourNames[col]);

    

    for (unsigned short row = 1; row < 5; row++)
    {
        Serial.print(F(","));
        Serial.print(saveData.colours[col][row]);
        oled.setCursor(16 * col, 24 + 6 * row);
        oled.print(map(saveData.colours[col][row - 1], saveData.colours[0][row - 1], saveData.colours[1][row - 1], 0, 255),2);
    }
    Serial.println();
    oled.display();
  }
  Serial.println(F("maped values"));
  for (unsigned short col = 0; col < 8; col++)
  {
    Serial.print(colourNames[col]);
    for (unsigned short i = 1; i < 5; i++)
    {
        Serial.print(F(","));
        Serial.print(map(saveData.colours[col][i - 1], saveData.colours[1][i - 1], saveData.colours[0][i - 1], 0, 255),2);
    }
    Serial.println();
  }
  
}

bool calibrateColours(bool allowedExit){
  SaveData tempSaveData = saveData;

  for (unsigned short i = 0; i < 8; i++)
  {
    oled.clearDisplay();
    oled.setTextWrap(false);
    oledPrintText(32, 32, 1, true, false, F("Insert token"));
    oledPrintText(32, 48, 1, true, false, colourNames[i]);
    oled.display();

    Serial.println(F("To calibrate token Insert and press SELECT"));
    while (!btnCheck(BTNSELECT))
    {
        if (btnCheck(BTNBACK) && allowedExit)
        {
        oled.clearDisplay();
        oledPrintText(64, 32, 2, true, true, F("Cancled"));
        oled.display();
        Serial.println(F("Calibration cancled"));
        delay(2000);
        return false;
        }
    }
    oled.clearDisplay();
    oled.println(F("Reading"));
    getRGBC();
    oled.display();
    if (i <= 3)
    {
      tempSaveData.colours[9][i] = RGBC[i];
      Serial.print(F("Default Saved value:"));
      Serial.print(RGBC[i]);
      Serial.println(colourNames[i]);
    } else {

    }
    tempSaveData.colours[i][0] = RGBC[0];
    tempSaveData.colours[i][1] = RGBC[1];
    tempSaveData.colours[i][2] = RGBC[2];
    tempSaveData.colours[i][3] = RGBC[3];

    servoMove(true);
  }

  // Save calibration data to EEPROM
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.println(F("Save Data?"));
  oled.setCursor(0, 8);
  for (int col = 0; col < 8; col++)
  {
    oled.setCursor(24 * col, 8);
    oled.print(colourNames[col]);
    Serial.print(colourNames[col]);
    for (int i = 1; i < 5; i++)
    {
        Serial.print(F(","));
        Serial.print(tempSaveData.colours[col][i]);
    }
    Serial.println();

    for (unsigned short row = 1; row < 4; row++)
    {
        oled.setCursor(16 * col, 24 + 6 * row);
        oled.print(map(tempSaveData.colours[col][row - 1], tempSaveData.colours[8][row - 1], tempSaveData.colours[8][3], 255, 0));
    }
    oled.display();
  }


  while (!(btnCheck(BTNBACK) || btnCheck(BTNSELECT)))
  {
    yield();
    ;
  }

  if (btnPressed() == BTNBACK && allowedExit)
  {
    oled.clearDisplay();
    oledPrintText(64, 32, 2, true, true, F("Setting"));
    oledPrintText(64, 50, 2, true, true, F("Discarded"));
    oled.display();
    Serial.println(F("Calibration data saved"));
    delay(2000);
    return false;
  }
  oled.clearDisplay();
  oledPrintText(64, 32, 1, true, true, F("Setting"));
  oledPrintText(64, 50, 2, true, true, F("Saved"));
  oled.setCursor(0, 0);
  oled.println(tempSaveData.saveVersion);
  oled.display();
  delay(2000);

    
  oled.display();

  EEPROM.put(EEPROM_CALIB_DATA_ADDR, tempSaveData);
  checkSavedData(false);
  Serial.println(F("Calibration data saved"));
  return true;

}


bool checkSavedData(bool wipe){
  EEPROM.get(EEPROM_CALIB_DATA_ADDR, saveData);
  if(wipe){
    
    Serial.print(saveData.saveVersion);
    Serial.println(F(": Version was wiped!"));
    saveData = saveDataDefault;
    EEPROM.put(EEPROM_CALIB_DATA_ADDR, saveData);
    return false;
  } else {
    if(saveData.saveVersion > 0){
    Serial.print(saveData.saveVersion);
    Serial.println(F(": Version was found"));
    return true;
  }
  }

  return false;
}

void dumpcolour(){

  getRGBC();
  Serial.print(F(" R = "));
  Serial.println(RGBC[0]);
  Serial.print(F(" G = "));
  Serial.println(RGBC[1]);
  Serial.print(F(" B = "));
  Serial.println(RGBC[2]);
  Serial.print(F(" C = "));
  Serial.println(RGBC[3]);
}

void setup()
{
  pinMode(RLYSIG, OUTPUT);
  digitalWrite(RLYSIG,HIGH);
  pinMode(S2, OUTPUT);  // Colour Selection Pin
  pinMode(S3, OUTPUT);  // Colour Selection Pin
  pinMode(COUT, INPUT); // Colour Reccive Pin
  initServo();          // We make a function to use when servo power is cut
  cServo.write(90);

  // strip.begin();        // Blast LED To max brightness by initalising the LEDS
  // strip.setBrightness(10);
  // strip.clear(); // Incase the arduino resets and the power is still clear all lights

  Serial.begin(BAUDRATE); // Start Serial with baud 115200.
  while (!Serial)
    ;                      // Wait for serial since we need it for debugging.
  Serial.println(F("Ready")); // Tell us serial is ready.

  

  


  oled.clearDisplay();
  oled.setTextSize(1); // Draw 2X-scale text
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0,0);
  oled.println(F("TEST"));
  oled.display();

  if (!checkSavedData(WIPEDATA)){
    Serial.println(F("System is wiped! starting config"));



  }
}

void loop()
{
  if (btnCheck(BTNSELECT))
  {
    Serial.println(F("BTNSELECT Pressed"));
    soundTone(TONEACCEPT);
    servoMove(true);
    soundTone(TONEDENY);
    servoMove(false);

  }

  if (btnCheck(BTNUP))
  {
    Serial.println("BTNUP Pressed");
    soundTone(TONEACCEPT);
    dumpcolour();

  }

  if (btnCheck(BTNDOWN))
  {
    Serial.println("BTNDOWN Pressed");
    soundTone(TONEDENY);
    checkSavedData(false);
    viewData();
  }

  if (btnCheck(BTNBACK))
  {
    Serial.println("BTNDOWN Pressed");
    oled.clearDisplay();
    calibrateColours(true);
    oled.display();
    }
  }


  




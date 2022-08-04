
#define nodeMCU

#define LED_PIN 4 // led data in connected to GPIO_12 (d6/nodeMCU)

#define LED_PWR_LIMIT 1250 // 750mA - Power limit in mA (voltage is set in setup() to 5v)

#define LED_DIGITS 4

#define LED_PER_DIGIT_STRIP 56   // each digit is using 56 leds (7 segments / 8 leds each)
#define SPACING_LEDS 2           // 2 unused leds between digits/center dots
#define LED_PER_CENTER_MODULE 12 // 12 leds inside center module (6 upper dot/6 lower dot)
// Using all of the above we can calculate total led count:
#define LED_COUNT LED_DIGITS *LED_PER_DIGIT_STRIP + (LED_DIGITS / 2) * (SPACING_LEDS * 2) + (LED_DIGITS / 3) * LED_PER_CENTER_MODULE + (LED_DIGITS / 3) * (SPACING_LEDS * 4)

#include <FastLED.h>
#include <TimeLib.h>
#include <BLEDevice.h>
#include <BLEServer.h>

#define SERVICE_UUID "6f436a20-4054-4546-8b8b-20322a77f4f6"
#define BRIGHTNESS_CHAR_UUID "a1069a62-66a2-404f-bc91-535cf0c148c3"
#define THEME_CHAR_UUID "ae43f9b8-3731-40f6-b523-d6231f65a1a1"
#define TIME_CHAR_UUID "a2be1fb5-3193-400f-9b0e-4e234487c2c7"

BLECharacteristic *brightnessCharacteristics;
BLECharacteristic *themeCharacteristics;
BLECharacteristic *timeCharacteristics;

uint8_t gCurrentPatternNumber = 0;
uint8_t gHue = 0;
int timeOffset = 60;

CRGB leds[LED_COUNT];

CRGBPalette16 currentPalette;

const bool dbg = false; // debug, true = enable serial input/output - set to false to save memory
bool state = true;
byte brightness = 100;                   // default brightness if none saved to eeprom yet / first run
byte brightnessLevels[3]{100, 150, 230}; // 0 - 255, brightness Levels (min, med, max) - index (0-2) will get stored to eeprom
                                         // Note: With brightnessAuto = 1 this will be the maximum brightness setting used!
byte brightnessAuto = 1;                 // 1 = enable brightness corrections using a photo resistor/readLDR();
byte upperLimitLDR = 140;                // everything above this value will cause max brightness to be used (if it's higher than this)
byte lowerLimitLDR = 40;                 // everything below this value will cause minBrightness to be used
byte minBrightness = 15;                 // anything below this avgLDR value will be ignored
float factorLDR = 1.0;                   // try 0.5 - 2.0, compensation value for avgLDR. Set dbgLDR & dbg to true and watch serial console. Looking...
const bool dbgLDR = false;               // ...for values in the range of 120-160 (medium room light), 40-80 (low light) and 0 - 20 in the dark
int pinLDR = 35;
byte intervalLDR = 60;              // read value from LDR every 60ms (most LDRs have a minimum of about 30ms - 50ms)
unsigned long valueLDRLastRead = 0; // time when we did the last readout
int avgLDR = 0;
byte rawLDR; // we will average this value somehow somewhere in readLDR();
int lastAvgLDR = 0;

byte startColor = 0;            // "index" for the palette color used for drawing
byte displayMode = 1;           // 0 = 12h, 1 = 24h (will be saved to EEPROM once set using buttons)
byte colorOffset = 32;          // default distance between colors on the color palette used between digits/leds (in overlayMode)
int colorChangeInterval = 1500; // interval (ms) to change colors when not in overlayMode (per pixel/led coloring uses overlayInterval)
byte overlayMode = 0;           // switch on/off (1/0) to use void colorOverlay(); (will be saved to EEPROM once set using buttons)
int overlayInterval = 200;      // interval (ms) to change colors in overlayMode
int currentColorTheme = 0;
int maxColorTheme = 7;

byte lastSecond = 0;
unsigned long lastLoop = 0;
unsigned long lastColorChange = 0;

typedef void (*SimplePatternList[])();

void rainbow();
void rainbowWithGlitter();
void confetti();
void sinelon();
void juggle();
void bpm();
SimplePatternList gPatterns = {rainbow, rainbowWithGlitter, confetti, sinelon, juggle, bpm};

/* these values will be stored to the EEPROM:
  0 = index for selectedPalette / switchPalette();
  1 = index for brightnessLevels / switchBrightness();
  2 = displayMode (when set using the buttons)
  3 = overlayMode (when set using the buttons)
*/

const uint16_t segGroups[7][4] PROGMEM = {
    // Each segment has 1-x led(s). So lets assign them in a way we get the first digit (right one when seen from the front) completely.
    {4, 7, 16, 19},       // top, a
    {0, 3, 20, 23},       // top right, b
    {232, 235, 236, 239}, // bottom right, c
    {228, 231, 240, 243}, // bottom, d
    {224, 227, 244, 247}, // bottom left, e
    {8, 11, 12, 15},      // top left, f
    {24, 27, 248, 251},   // center, g
};
// All other digits/segments can be calculated based on this and the definitions on top of the sketch
// Note: The first number always has to be the lower one as they're subtracted later on... (fix by using abs()? ^^)

const byte digits[14][7] PROGMEM = {
    // Lets define 10 numbers (0-9) with 7 segments each, 1 = segment is on, 0 = segment is off
    {1, 1, 1, 1, 1, 1, 0}, // 0 -> Show segments a - f, don't show g (center one)
    {0, 1, 1, 0, 0, 0, 0}, // 1 -> Show segments b + c (top and bottom right), nothing else
    {1, 1, 0, 1, 1, 0, 1}, // 2 -> and so on...
    {1, 1, 1, 1, 0, 0, 1}, // 3
    {0, 1, 1, 0, 0, 1, 1}, // 4
    {1, 0, 1, 1, 0, 1, 1}, // 5
    {1, 0, 1, 1, 1, 1, 1}, // 6
    {1, 1, 1, 0, 0, 0, 0}, // 7
    {1, 1, 1, 1, 1, 1, 1}, // 8
    {1, 1, 1, 1, 0, 1, 1}, // 9
    {0, 0, 0, 1, 1, 1, 1}, // t -> some letters from here on (index 10-13, so this won't interfere with using digits 0-9 by using index 0-9
    {0, 0, 0, 0, 1, 0, 1}, // r
    {0, 1, 1, 1, 0, 1, 1}, // y
    {0, 1, 1, 1, 1, 0, 1}  // d
};

void setupClock();

void cycleTheme();
void switchPalette(int palette);
void updateDisplay(byte color, byte colorSpacing);
void showDigit(byte digit, byte color, byte pos);
void showDots(byte dots, byte color);
void readLDR();
void displayTime(time_t t, byte color, byte colorSpacing);

class ThemeSetCallback : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    std::string value = pCharacteristic->getValue();
    Serial.println(value.c_str());
    if (strcmp(value.c_str(), "CYCLE") == 0)
    {
      cycleTheme();
    }
    else
    {
      int palette = atoi(value.c_str());
      switchPalette(palette);
    }
  }
};

class TimeSetCallback : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *characteristics)
  {
    std::string value = characteristics->getValue();
    if (value.length() == 4)
    {
      std::string hourStr = value.substr(0, 2);
      std::string minuteStr = value.substr(2, 2);
      tmElements_t setupTime; // Create a time element which will be used. Using the current time would
      setupTime.Hour = atoi(hourStr.c_str());    // give some problems (like time still running while setting hours/minutes)
      setupTime.Minute = atoi(minuteStr.c_str());   // Setup starts at 12 (12 pm)
      setupTime.Second = 1;   // 1 because displayTime() will always display both dots at even seconds
      setupTime.Day = 15;     // not really neccessary as day/month aren't used but who cares ^^
      setupTime.Month = 5;    // see above
      setupTime.Year = 2020 - 1970;

      setTime(makeTime(setupTime));
      FastLED.clear();
      FastLED.show();
    }
  }
};

void setup()
{
  Serial.begin(9600);
  Serial.println("Starting BLE Server");
  BLEDevice::init("LazyClock");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  brightnessCharacteristics = pService->createCharacteristic(BRIGHTNESS_CHAR_UUID,
                                                             BLECharacteristic::PROPERTY_READ);

  themeCharacteristics = pService->createCharacteristic(THEME_CHAR_UUID,
                                                        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);

  themeCharacteristics->setCallbacks(new ThemeSetCallback());

  timeCharacteristics = pService->createCharacteristic(TIME_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
  timeCharacteristics->setCallbacks(new TimeSetCallback());
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);

  BLEDevice::startAdvertising();
  Serial.println("Advertising BLE service!");
  if (brightnessAuto == 1)
    pinMode(pinLDR, OUTPUT);

  if (dbg)
  {
    Serial.println();
    Serial.println(F("Lazy 7 / One starting up..."));
    Serial.print(F("Configured for: "));
    Serial.print(LED_COUNT);
    Serial.println(F(" leds"));
    Serial.print(F("Power limited to (mA): "));
    Serial.print(LED_PWR_LIMIT);
    Serial.println(F(" mA"));
    Serial.println();
  }
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT).setCorrection(TypicalSMD5050).setTemperature(DirectSunlight).setDither(1);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, LED_PWR_LIMIT);
  FastLED.clear();
  FastLED.show();

  switchPalette(0);

  setupClock();
}

void loop()
{
  if ((lastLoop - lastColorChange >= colorChangeInterval) && (overlayMode == 0) // if colorChangeInterval has been reached and overlayMode is disabled...
      || (lastLoop - lastColorChange >= overlayInterval) && (overlayMode == 1))
  {               // ...or if overlayInterval has been reached and overlayMode is enabled...
    startColor++; // increase startColor to "move" colors slowly across the digits/leds
    updateDisplay(startColor, colorOffset);
    lastColorChange = millis();
  }
  if (lastSecond != second())
  {                                         // if current second is different from last second drawn...
    updateDisplay(startColor, colorOffset); // lastSecond will be set in displayTime() and will be used for
    lastSecond = second();                  // redrawing regardless the digits count (HH:MM or HH:MM:SS)
  }

  if ((lastLoop - valueLDRLastRead >= intervalLDR) && (brightnessAuto == 1))
  {            // if LDR is enabled and sample interval has been reached...
    readLDR(); // ...call readLDR();
    if (abs(avgLDR - lastAvgLDR) >= 5)
    { // only adjust current brightness if avgLDR has changed for more than +/- 5.
      updateDisplay(startColor, colorOffset);
      lastAvgLDR = avgLDR;
      if (dbg)
      {
        Serial.print(F("Updated display with avgLDR of: "));
        Serial.println(avgLDR);
      }

      char message[16];
      itoa(rawLDR, message, 10);
      brightnessCharacteristics->setValue(message);
      // mqttClient.publish("lazy-clock/brightness/state", message);
    }
    valueLDRLastRead = millis();
  }

  if (minute() % 10 == 0 && second() == 0)
  {
    Serial.println("Time to cycle the theme");
    cycleTheme();
  }

  /*if ( ( hour() == 3 || hour() == 9 || hour() == 15 || hour() == 21 ) &&                    // if hour is 3, 9, 15 or 21 and...
        ( minute() == 3 && second() == 0 ) ) {                                               // minute is 3 and second is 0....
    if ( dbg ) Serial.print(F("Current time: ")); Serial.println(now());
    //syncNTP();                                                                            // ...either sync using ntp or...
    if ( dbg ) Serial.print(F("New time: ")); Serial.println(now());
  }*/
  FastLED.show();
  lastLoop = millis(); // if dbg = true this will read serial input/keys
}

void readLDR()
{ // read LDR value 5 times and write average to avgLDR for use in updateDisplay();
  static byte runCounter = 1;
  static int tmp = 0;
  int rawValue = analogRead(pinLDR);
  rawLDR = map(rawValue, 0, 4095, 0, 100);
  byte readOut = map(rawValue, 0, 4095, 0, 250);

  tmp += readOut;
  if (runCounter == 5)
  {
    avgLDR = (tmp / 5) * factorLDR;
    tmp = 0;
    runCounter = 0;
    if (dbg && dbgLDR)
    {
      Serial.print(F("avgLDR value: "));
      Serial.print(avgLDR);
    }
    avgLDR = max(avgLDR, int(minBrightness));
    avgLDR = min(avgLDR, int(brightness)); // this keeps avgLDR in a range between minBrightness and maximum current brightness
    if (avgLDR >= upperLimitLDR && avgLDR < brightness)
      avgLDR = brightness; // if avgLDR is above upperLimitLDR switch to max current brightness
    if (avgLDR <= lowerLimitLDR)
      avgLDR = minBrightness; // if avgLDR is below lowerLimitLDR switch to minBrightness
    if (dbg && dbgLDR)
    {
      Serial.print(F(" - adjusted to: "));
      Serial.println(avgLDR);
    }
  }

  runCounter++;
}

void colorOverlay()
{ // This "projects" colors on already drawn leds before showing leds in updateDisplay();
  for (int i = 0; i < LED_COUNT; i++)
  {                                                                                            // check each led...
    if (leds[i])                                                                               // ...and if it is lit...
      leds[i] = ColorFromPalette(currentPalette, startColor + i * 2, brightness, LINEARBLEND); // ...assign increasing color from current palette
  }
}

void updateDisplay(byte color, byte colorSpacing)
{ // this is what redraws the "screen"
  FastLED.clear();
  if (!state)
  {
    return;
  }                                        // clear whatever the leds might have assigned currently...
  displayTime(now(), color, colorSpacing); // ...set leds to display the time...
  if (overlayMode == 1)
    colorOverlay(); // ...and if using overlayMode = 1 draw custom colors over single leds
  if (brightnessAuto == 1)
  {                                // If brightness is adjusted automatically by using readLDR()...
    FastLED.setBrightness(avgLDR); // ...set brightness to avgLDR
  }
  else
  {                                    // ...otherwise...
    FastLED.setBrightness(brightness); // ...assign currently selected brightness
  }
}

void displayTime(time_t t, byte color, byte colorSpacing)
{
  time_t utc_time = t;
  // t = middleEurope.toLocal(utc_time);

  byte posOffset = 0; // this offset will be used to move hours and minutes...
  if (LED_DIGITS / 2 > 2)
    posOffset = 2; // ... to the left so we have room for the seconds when there's 6 digits available
  if (displayMode == 0)
  { // if 12h mode is selected...
    if (hourFormat12(t) >= 10)
      showDigit(1, color + colorSpacing * 2, 3 + posOffset);                    // ...and hour > 10, display 1 at position 3
    showDigit((hourFormat12(t) % 10), color + colorSpacing * 3, 2 + posOffset); // display 2nd digit of HH anyways
  }
  else if (displayMode == 1)
  { // if 24h mode is selected...
    if (hour(t) > 9)
      showDigit(hour(t) / 10, color + colorSpacing * 2, 3 + posOffset); // ...and hour > 9, show 1st digit at position 3 (this is to avoid a leading 0 from 0:00 - 9:00 in 24h mode)
    showDigit(hour(t) % 10, color + colorSpacing * 3, 2 + posOffset);   // again, display 2nd digit of HH anyways
  }
  showDigit((minute(t) / 10), color + colorSpacing * 4, 1 + posOffset); // minutes thankfully don't differ between 12h/24h, so this can be outside the above if/else
  showDigit((minute(t) % 10), color + colorSpacing * 5, 0 + posOffset); // each digit is drawn with an increasing color (*2, *3, *4, *5) (*6 and *7 for seconds only in HH:MM:SS)
  if (posOffset > 0)
  {
    showDigit((second(t) / 10), color + colorSpacing * 6, 1);
    showDigit((second(t) % 10), color + colorSpacing * 7, 0);
  }
  if (second(t) % 2 == 0)
    showDots(2, second(t) * 4.25); // show : between hours and minutes on even seconds with the color cycling through the palette once per minute
  lastSecond = second(t);
}

void showSegment(byte segment, byte color, byte segDisplay)
{
  // This shows the segments from top of the sketch on a given position (segDisplay).
  // pos 0 is the most right one (seen from the front) where data in is connected
  int startLED = pgm_read_word_near(&segGroups[segment][0]);
  int endLED = pgm_read_word_near(&segGroups[segment][1]);
  int offsetLED = 0;
  if (segDisplay > 0)
    offsetLED = segDisplay * LED_PER_DIGIT_STRIP / 2 + segDisplay * SPACING_LEDS; // if position/display is greater 0 we add half the leds of a digit
  if (segDisplay >= 2)
    offsetLED += segDisplay / 2 * LED_PER_CENTER_MODULE / 2 + segDisplay / 2 * SPACING_LEDS; // if position/display is greater 1 we have to add offsets for the dots
  for (int i = startLED; i <= endLED; i++)
  { // light up group 1 (1st and 2nd value inside segGroups array)
    if (segment == 0 || segment == 1 || segment == 5)
      leds[i + offsetLED] = ColorFromPalette(currentPalette, color, brightness, LINEARBLEND); // upper 3 segments
    if (segment == 2 || segment == 3 || segment == 4)
      leds[i - offsetLED] = ColorFromPalette(currentPalette, color, brightness, LINEARBLEND); // lower 3 segments
    if (segment == 6)
      leds[i + offsetLED] = ColorFromPalette(currentPalette, color, brightness, LINEARBLEND); // upper part of center segment
  }
  startLED = pgm_read_word_near(&segGroups[segment][2]);
  endLED = pgm_read_word_near(&segGroups[segment][3]);
  for (int i = startLED; i <= endLED; i++)
  { // light up group 2 (3rd and 4th value inside segment array)
    if (segment == 0 || segment == 1 || segment == 5)
      leds[i + offsetLED] = ColorFromPalette(currentPalette, color, brightness, LINEARBLEND); // upper 3 segments
    if (segment == 2 || segment == 3 || segment == 4)
      leds[i - offsetLED] = ColorFromPalette(currentPalette, color, brightness, LINEARBLEND); // lower 3 segments
    if (segment == 6)
      leds[i - offsetLED] = ColorFromPalette(currentPalette, color, brightness, LINEARBLEND); // lower part of center segment
  }
}

void showDigit(byte digit, byte color, byte pos)
{
  // This draws numbers using the according segments as defined on top of the sketch (0 - 9)
  for (byte i = 0; i < 7; i++)
  {
    if (pgm_read_byte_near(&digits[digit][i]) != 0)
      showSegment(i, color, pos);
  }
}

void showDots(byte dots, byte color)
{
  // in 12h mode while in setup single/upper dot(s) resemble(s) AM, both/all dots resemble PM
  int startPos = LED_PER_DIGIT_STRIP + SPACING_LEDS * 2;
  byte distance = LED_PER_CENTER_MODULE / 2;
  for (byte i = 0; i < distance; i++) // right upper dot
    leds[startPos + i] = ColorFromPalette(currentPalette, color + (i + 4) * colorOffset, brightness, LINEARBLEND);
  if (LED_DIGITS > 4)
  {
    startPos += LED_PER_DIGIT_STRIP + SPACING_LEDS * 6; // left upper dot
    for (byte i = 0; i < distance; i++)
      leds[startPos + i] = ColorFromPalette(currentPalette, color + (i + 4) * colorOffset, brightness, LINEARBLEND);
  }
  if (dots == 2)
  {
    startPos = LED_COUNT - 1 - (LED_PER_DIGIT_STRIP + SPACING_LEDS * 2); // right lower dot
    for (byte i = 0; i < distance; i++)
      leds[startPos - i] = ColorFromPalette(currentPalette, color + (i + 4) * colorOffset, brightness, LINEARBLEND);
    if (LED_DIGITS > 4)
    {
      startPos -= LED_PER_DIGIT_STRIP + SPACING_LEDS * 8 + 1; // left lower dot
      for (byte i = 0; i < distance; i++)
        leds[startPos + i] = ColorFromPalette(currentPalette, color + (i + 4) * colorOffset, brightness, LINEARBLEND);
    }
  }
}

void switchPalette(int palette)
{
  Serial.print("Setting palette to ");
  Serial.println(palette);
  themeCharacteristics->setValue(palette);
  if (palette == 0)
  {
    currentColorTheme = 0;
    currentPalette = CRGBPalette16(CRGB(224, 0, 32),
                                   CRGB(0, 0, 244),
                                   CRGB(128, 0, 128),
                                   CRGB(224, 0, 64));
  }
  if (palette == 1)
  {
    currentColorTheme = 1;
    currentPalette = CRGBPalette16(CRGB(224, 16, 0),
                                   CRGB(192, 64, 0),
                                   CRGB(128, 128, 0),
                                   CRGB(224, 32, 0));
  }
  if (palette == 2)
  {
    currentColorTheme = 2;
    currentPalette = CRGBPalette16(CRGB::Aquamarine,
                                   CRGB::Turquoise,
                                   CRGB::Blue,
                                   CRGB::DeepSkyBlue);
  }
  if (palette == 3)
  {
    currentColorTheme = 3;
    currentPalette = RainbowColors_p;
  }
  if (palette == 4)
  {
    currentColorTheme = 4;
    currentPalette = PartyColors_p;
  }
  if (palette == 5)
  {
    currentColorTheme = 5;
    currentPalette = CRGBPalette16(CRGB::White);
  }
  if (palette == 6)
  {
    currentColorTheme = 6;
    currentPalette = CRGBPalette16(CRGB::LawnGreen);
  }
}

DEFINE_GRADIENT_PALETTE(setupColors_gp){                // this color palette will only be used while in setup
                                        0, 240, 240, 0, // unset values = red, current value = yellow, set values = green
                                        64, 240, 240, 0,
                                        96, 240, 0, 0,
                                        160, 240, 0, 0,
                                        224, 0, 240, 0,
                                        255, 0, 240, 0};

void setupClock()
{
  // finally not using a custom displayTime routine for setup, improvising a bit and using the setupColor-Palette defined on top of the sketch
  if (dbg)
    Serial.println(F("Entering setup mode..."));
  byte prevBrightness = brightness; // store current brightness and switch back after setup
  brightness = brightnessLevels[1]; // select medium brightness level
  currentPalette = setupColors_gp;  // use setupColors_gp palette while in setup
  tmElements_t setupTime;           // Create a time element which will be used. Using the current time would
  setupTime.Hour = 12;              // give some problems (like time still running while setting hours/minutes)
  setupTime.Minute = 0;             // Setup starts at 12 (12 pm)
  setupTime.Second = 1;             // 1 because displayTime() will always display both dots at even seconds
  setupTime.Day = 15;               // not really neccessary as day/month aren't used but who cares ^^
  setupTime.Month = 5;              // see above
  setupTime.Year = 2020 - 1970;
  FastLED.clear();
  FastLED.show();

  setTime(makeTime(setupTime));
  FastLED.clear();
  displayTime(makeTime(setupTime), 95, 0);
  FastLED.show();
  brightness = prevBrightness;
  switchPalette(0);
  delay(500); // short delay followed by fading all leds to black
  for (byte i = 0; i < 255; i++)
  {
    for (int x = 0; x < LED_COUNT; x++)
      leds[x]--;
    FastLED.show();
    delay(2);
  }
  if (dbg)
    Serial.println(F("Setup done..."));
}

// stuff below will only be used when compiled for nodeMCU _AND_ using WiFi
/*
void syncNTP() {                                                                            // gets time from ntp and sets internal time accordingly, will return when no connection is established
  if ( dbg ) Serial.println(F("Entering syncNTP()..."));
  if ( WiFi.status() != WL_CONNECTED ) {
    if ( dbg ) Serial.println(F("No active WiFi connection!"));
    return;
  }                                                                                         // Sometimes the connection doesn't work right away although status is WL_CONNECTED...
  delay(1500);                                                                              // ...so we'll wait a moment before causing network traffic
  timeClient.update();
  setTime(timeClient.getEpochTime());
  if ( dbg ) {
    Serial.print(F("nodemcu time: ")); Serial.println(now());
    Serial.print(F("ntp time    : ")); Serial.println(timeClient.getEpochTime());
    Serial.println(F("syncNTP() done..."));
  }
}*/
/*
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("Got message");
  Serial.println(topic);

  char message[length + 1];

  sprintf(message, "%c", (char)payload[0]);
  for (int i = 1; i < length; i++)
  {
    sprintf(message, "%s%c", message, (char)payload[i]);
  }

  Serial.println(message);

  if (strcmp(topic, "lazy-clock/light/switch") == 0) {
    if (strcmp(message, "ON") == 0) {
      state = true;
    } else {
      state = false;
    }
    //mqttClient.publish("lazy-clock/light/state", state ? "ON" : "OFF", true);
  }
  if (strcmp(topic, "lazy-clock/palette/set") == 0) {
    switchPalette(message);
    //mqttClient.publish("lazy-clock/palette/state", message, true);
  }
  if (strcmp(topic, "lazy-clock/palette/effect") == 0) {
    cycleTheme();
  }
}*/

void cycleTheme()
{
  if (!state)
    return;
  for (int i = 0; i < 200; i++)
  {
    gPatterns[gCurrentPatternNumber]();
    FastLED.show();
    FastLED.delay(1000 / 120);
    gHue++;
  }

  gCurrentPatternNumber = (gCurrentPatternNumber + 1) % 6;
  currentColorTheme = (currentColorTheme + 1) % maxColorTheme;
  switchPalette(currentColorTheme);
}

// Effects
void rainbow()
{
  // FastLED's built-in rainbow generator
  fill_rainbow(leds, LED_COUNT, gHue, 7);
}

void addGlitter(fract8 chanceOfGlitter);

void rainbowWithGlitter()
{
  // built-in FastLED rainbow, plus some random sparkly glitter
  rainbow();
  addGlitter(80);
}

void addGlitter(fract8 chanceOfGlitter)
{
  if (random8() < chanceOfGlitter)
  {
    leds[random16(LED_COUNT)] += CRGB::White;
  }
}

void confetti()
{
  // random colored speckles that blink in and fade smoothly
  fadeToBlackBy(leds, LED_COUNT, 10);
  int pos = random16(LED_COUNT);
  leds[pos] += CHSV(gHue + random8(64), 200, 255);
}

void sinelon()
{
  // a colored dot sweeping back and forth, with fading trails
  fadeToBlackBy(leds, LED_COUNT, 20);
  int pos = beatsin16(13, 0, LED_COUNT - 1);
  leds[pos] += CHSV(gHue, 255, 192);
}

void bpm()
{
  // colored stripes pulsing at a defined Beats-Per-Minute (BPM)
  uint8_t BeatsPerMinute = 62;
  CRGBPalette16 palette = PartyColors_p;
  uint8_t beat = beatsin8(BeatsPerMinute, 64, 255);
  for (int i = 0; i < LED_COUNT; i++)
  { // 9948
    leds[i] = ColorFromPalette(palette, gHue + (i * 2), beat - gHue + (i * 10));
  }
}

void juggle()
{
  // eight colored dots, weaving in and out of sync with each other
  fadeToBlackBy(leds, LED_COUNT, 20);
  byte dothue = 0;
  for (int i = 0; i < 8; i++)
  {
    leds[beatsin16(i + 7, 0, LED_COUNT - 1)] |= CHSV(dothue, 200, 255);
    dothue += 32;
  }
}

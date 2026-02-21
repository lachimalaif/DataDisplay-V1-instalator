// fixed forecast unit for the second day
// fixed nameday font size increase after midnight
// fixed invalid coordinates during autosync (0,0 coordinates)
// fixed timezones worldwide for any location input
// fixed unit conversion between °C and °F in forecast
// fixed US timezones and added top 15 US cities
// added Africa/Cairo and 20+ other African timezones
// updated code version display function in serial monitor
// fixed bug for incorrect password in Wi-Fi settings
// replaced day abbreviations with full names

#include <WiFi.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include "time.h"
#include <XPT2046_Touchscreen.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <TzDbLookup.h>

// ================= GLOBÁLNÍ NASTAVENÍ (Musí být PRVNÍ) =================
TFT_eSPI tft = TFT_eSPI();
Preferences prefs;
bool isWhiteTheme = false;  // TEĎ JE TO TADY, TAKŽE TO VŠICHNI VIDÍ
// ================= NOVÉ PROMĚNNÉ PRO HODINY =================
bool isDigitalClock = false; // false = Analog, true = Digital
bool is12hFormat = false;    // false = 24h, true = 12h
bool invertColors = false;  // NOVÁ PROMĚNNÁ: Invertování barev pro CYD desky s invertovaným displejem

// ================= OTA UPDATE GLOBALS =================
const char* FIRMWARE_VERSION = "1.4";  // AKTUÁLNÍ VERZE
const char* VERSION_CHECK_URL = "https://raw.githubusercontent.com/lachimalaif/DataDisplay-V1-instalator/main/version.json";
const char* FIRMWARE_URL = "https://github.com/lachimalaif/DataDisplay-V1-instalator/releases/latest/download/DataDisplayCYD.ino.bin";

String availableVersion = "";  // Dostupná verze z GitHubu
String downloadURL = "";       // URL pro stažení firmware (z version.json)
bool updateAvailable = false;  // Je k dispozici aktualizace?
int otaInstallMode = 1;  // 0=Auto, 1=By user, 2=Manual
unsigned long lastVersionCheck = 0;
const unsigned long VERSION_CHECK_INTERVAL = 86400000;  // 24 hodin (pro testování změň na 30000 = 30s)

bool isUpdating = false;  // Probíhá aktualizace?
int updateProgress = 0;   // Progress 0-100%
String updateStatus = ""; // Status zpráva

// ================= TEMA NASTAVENI =================
int themeMode = 0; // 0 = BLACK, 1 = WHITE, 2 = BLUE, 3 = YELLOW
// POZN: U tématech BLACK a WHITE určuje isWhiteTheme: false=BLACK, true=WHITE
// U tématech BLUE a YELLOW je isWhiteTheme ignorován (pevné barvy)

float themeTransition = 0.0f; // Průběh přechodu (0.0 - 1.0)

// Barvy s přechody
uint16_t blueLight = 0x07FF;    // Světle modrá
uint16_t blueDark = 0x0010;     // Tmavě modrá
uint16_t yellowLight = 0xFFE0;  // Světle žlutá
uint16_t yellowDark = 0xCC00;   // Tmavě žlutá


// ================= WEATHER GLOBALS =================
String weatherCity = "Plzen"; 
float currentTemp = 0.0;
int currentHumidity = 0;
float currentWindSpeed = 0.0;
int currentWindDirection = 0;
int currentPressure = 0; 
int weatherCode = 0;
float lat = 0;
float lon = 0;
bool weatherUnitF = false; 
bool weatherUnitMph = false;  // false = km/h, true = mph
unsigned long lastWeatherUpdate = 0;
bool initialWeatherFetched = false;

struct ForecastData {
  int code;
  float tempMax;
  float tempMin;
};
ForecastData forecast[2]; 
// Proměnné pro dny předpovědi
String forecastDay1Name = "Monday";    // Zítra
String forecastDay2Name = "Tuesday";   // Pozítří

int moonPhaseVal = 0; 

// ================= SLUNCE A AUTO DIM (NOVÉ Z control.txt) =================
String sunriseTime = "--:--";
String sunsetTime = "--:--";
// ================= AUTODIM UI - NASTAVENÍ V MENU =================
int autoDimEditMode = 0;  // 0=none, 1=editing start, 2=editing end, 3=editing level
int autoDimTempStart = 22;
int autoDimTempEnd = 6;
int autoDimTempLevel = 20;
unsigned long lastBrightnessUpdate = 0;  // Aby se jas neměnil při každém loopu

bool autoDimEnabled = false;
int autoDimStart = 22; 
int autoDimEnd = 6;    
int autoDimLevel = 20; 
bool isDimmed = false; 

// Ikony Slunce
const unsigned char icon_sunrise[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x09, 0x90, 0x05, 0xa0, 0x03, 0xc0,
  0x01, 0x80, 0x7f, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
const unsigned char icon_sunset[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xfe, 0x01, 0x80, 0x03, 0xc0, 0x05, 0xa0,
  0x09, 0x90, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ================= MODERNÍ VEKTOROVÉ IKONY =================

void drawCloudVector(int x, int y, uint32_t color) {
  tft.fillCircle(x + 10, y + 15, 8, color);
  tft.fillCircle(x + 18, y + 10, 10, color);
  tft.fillCircle(x + 28, y + 15, 8, color);
  tft.fillRoundRect(x + 10, y + 15, 20, 8, 4, color);
}

void drawWeatherIconVector(int code, int x, int y) {
  // Barvy ikon se adaptují na téma
  uint16_t cloudCol = TFT_SILVER; 
  uint16_t shadowCol = isWhiteTheme ? 0x8410 : 0x4208; // Stín v modrém/žlutém tématu
  
  switch (code) {
    case 0: // Jasno
      // Sluníčko s odstínem
      tft.fillCircle(x + 16, y + 16, 10, TFT_YELLOW);
      tft.drawCircle(x + 16, y + 16, 11, shadowCol); // Stín
      for (int i = 0; i < 360; i += 45) {
        float rad = i * 0.01745;
        tft.drawLine(x+16+cos(rad)*11, y+16+sin(rad)*11, x+16+cos(rad)*16, y+16+sin(rad)*16, TFT_YELLOW);
      }
      break;
    
    case 1: case 2: case 3: // Polojasno
      tft.fillCircle(x + 22, y + 10, 8, TFT_YELLOW);
      tft.drawCircle(x + 22, y + 10, 9, shadowCol); // Stín
      drawCloudVector(x, y + 5, cloudCol);
      break;
    
    case 45: case 48: // Mlha
      for(int i=0; i<3; i++) {
        tft.fillRoundRect(x+4, y+12+(i*6), 24, 3, 2, TFT_SILVER);
        tft.drawRoundRect(x+4, y+12+(i*6), 24, 3, 2, shadowCol); // Stín
      }
      break;
    
    case 51: case 53: case 55: case 61: case 63: case 65: // Déšť
      drawCloudVector(x, y + 2, TFT_SILVER);
      for(int i=0; i<3; i++) {
        tft.fillRoundRect(x+10+(i*6), y+22, 2, 6, 1, TFT_BLUE);
        tft.drawRoundRect(x+10+(i*6), y+22, 2, 6, 1, shadowCol); // Stín kapky
      }
      break;
    
    case 71: case 73: case 75: case 77: // Sníh
      drawCloudVector(x, y + 2, cloudCol);
      tft.setTextColor(TFT_SKYBLUE);
      tft.drawString("*", x + 12, y + 22); 
      tft.drawString("*", x + 22, y + 22);
      break;
    
    case 80: case 81: case 82: // Přeháňky
      tft.fillCircle(x + 22, y + 10, 7, TFT_YELLOW);
      tft.drawCircle(x + 22, y + 10, 8, shadowCol); // Stín
      drawCloudVector(x, y + 2, TFT_SILVER);
      tft.fillRoundRect(x + 16, y + 22, 2, 6, 1, TFT_BLUE);
      break;
    
    case 95: case 96: case 99: // Bouřka
      drawCloudVector(x, y + 2, shadowCol); // Tmavý mrak
      tft.drawLine(x+18, y+20, x+14, y+28, TFT_YELLOW);
      tft.drawLine(x+14, y+28, x+20, y+28, TFT_YELLOW);
      tft.drawLine(x+20, y+28, x+16, y+36, TFT_YELLOW);
      break;
    
    default:
      drawCloudVector(x, y + 5, TFT_SILVER);
      break;
  }
}

// ============================================
// NOVÁ FUNKCE: Zmenšené ikony pro forecast
// ============================================

void drawWeatherIconVectorSmall(int code, int x, int y) {
  // Zmenšená verze pro forecast, ale s lepšími proporcemi
  // Některé prvky zůstávají proporčnější
  
  uint16_t cloudCol = TFT_SILVER; 
  uint16_t shadowCol = isWhiteTheme ? 0x8410 : 0x4208;
  
  switch (code) {
    case 0: // Jasno
      // Slunko - stejná velikost jako v normální verzi (malý radius)
      tft.fillCircle(x + 16, y + 16, 9, TFT_YELLOW);
      tft.drawCircle(x + 16, y + 16, 10, shadowCol);
      for (int i = 0; i < 360; i += 45) {
        float rad = i * 0.01745;
        tft.drawLine(x+16+cos(rad)*10, y+16+sin(rad)*10, x+16+cos(rad)*14, y+16+sin(rad)*14, TFT_YELLOW);
      }
      break;
    
    case 1: case 2: case 3: // Polojasno - SLUNKO + MRAK
      // Slunko - větší, viditelné
      tft.fillCircle(x + 20, y + 10, 7, TFT_YELLOW);
      tft.drawCircle(x + 20, y + 10, 8, shadowCol);
      // Mrak - menší proporce
      tft.fillCircle(x + 8, y + 14, 6, cloudCol);
      tft.fillCircle(x + 14, y + 11, 7, cloudCol);
      tft.fillCircle(x + 20, y + 14, 5, cloudCol);
      tft.fillRoundRect(x + 8, y + 14, 15, 5, 2, cloudCol);
      break;
    
    case 45: case 48: // Mlha
      for(int i=0; i<3; i++) {
        tft.fillRoundRect(x+4, y+12+(i*5), 20, 2, 1, TFT_SILVER);
        tft.drawRoundRect(x+4, y+12+(i*5), 20, 2, 1, shadowCol);
      }
      break;
    
    case 51: case 53: case 55: case 61: case 63: case 65: // Déšť
      // Mrak - 80% velikosti
      tft.fillCircle(x + 9, y + 13, 6, cloudCol);
      tft.fillCircle(x + 15, y + 10, 8, cloudCol);
      tft.fillCircle(x + 22, y + 13, 6, cloudCol);
      tft.fillRoundRect(x + 9, y + 13, 16, 6, 3, cloudCol);
      // Kapky deště - 80% velikosti
      for(int i=0; i<3; i++) {
        tft.fillRoundRect(x+10+(i*5), y+21, 2, 5, 1, TFT_BLUE);
        tft.drawRoundRect(x+10+(i*5), y+21, 2, 5, 1, shadowCol);
      }
      break;
    
    case 71: case 73: case 75: case 77: // Sníh
      // Mrak - 80% velikosti
      tft.fillCircle(x + 9, y + 13, 6, cloudCol);
      tft.fillCircle(x + 15, y + 10, 8, cloudCol);
      tft.fillCircle(x + 22, y + 13, 6, cloudCol);
      tft.fillRoundRect(x + 9, y + 13, 16, 6, 3, cloudCol);
      // Sníh - stejné hvězdičky
      tft.setTextColor(TFT_SKYBLUE);
      tft.drawString("*", x + 11, y + 21); 
      tft.drawString("*", x + 19, y + 21);
      break;
    
    case 80: case 81: case 82: // Přeháňky - SLUNKO + MRAK
      // Slunko - viditelné
      tft.fillCircle(x + 20, y + 10, 7, TFT_YELLOW);
      tft.drawCircle(x + 20, y + 10, 8, shadowCol);
      // Mrak - menší
      tft.fillCircle(x + 8, y + 14, 6, cloudCol);
      tft.fillCircle(x + 14, y + 11, 7, cloudCol);
      tft.fillCircle(x + 20, y + 14, 5, cloudCol);
      tft.fillRoundRect(x + 8, y + 14, 15, 5, 2, cloudCol);
      // Kapka - jednotlivá
      tft.fillRoundRect(x + 14, y + 21, 2, 5, 1, TFT_BLUE);
      break;
    
    case 95: case 96: case 99: // Bouřka
      // Tmavý mrak - 80% velikosti
      tft.fillCircle(x + 9, y + 13, 6, shadowCol);
      tft.fillCircle(x + 15, y + 10, 8, shadowCol);
      tft.fillCircle(x + 22, y + 13, 6, shadowCol);
      tft.fillRoundRect(x + 9, y + 13, 16, 6, 3, shadowCol);
      // Blesk - 80% velikosti
      tft.drawLine(x+15, y+20, x+12, y+27, TFT_YELLOW);
      tft.drawLine(x+12, y+27, x+17, y+27, TFT_YELLOW);
      tft.drawLine(x+17, y+27, x+14, y+34, TFT_YELLOW);
      break;
    
    default:
      // Mrak - 80% velikosti
      tft.fillCircle(x + 9, y + 13, 6, cloudCol);
      tft.fillCircle(x + 15, y + 10, 8, cloudCol);
      tft.fillCircle(x + 22, y + 13, 6, cloudCol);
      tft.fillRoundRect(x + 9, y + 13, 16, 6, 3, cloudCol);
      break;
  }
}


// ============================================
// ✅ NOVÁ FUNKCE PRO SPRÁVNÉ KRESLENÍ MĚSÍČNÍ FÁZE
// ============================================
// Vykresluje měsíční fázi (0-7) jako správnou grafiku
// Použije správnou geometrii pro každou fázi

void drawMoonPhaseIcon(int mx, int my, int r, int phase, uint16_t textColor, uint16_t bgColor) {
  
  // Barva pozadí kruhu
  uint16_t moonBg = (themeMode == 2) ? blueDark : (themeMode == 3) ? yellowDark : (isWhiteTheme ? 0xDEDB : 0x3186);
  uint16_t moonColor = TFT_YELLOW;
  uint16_t shadowColor = moonBg;
  
  // Obrys kruhu
  tft.drawCircle(mx, my, r, textColor);
  
  // Vyplnění podle fáze
  switch(phase) {
    
    case 0: {
      // NOV (New Moon) - Pouze obrys, vnitřek tmavý
      tft.fillCircle(mx, my, r - 1, shadowColor);
      break;
    }
    
    case 1: {
      // WAXING CRESCENT - Srpek z PRAVÉ strany (křivá hranice)
      tft.fillCircle(mx, my, r - 1, shadowColor);
      // Srpek se vytváří průnikem dvou kruhů - jeden je střed měsíce, druhý je posunutý
      int offset = r / 3;
      for (int dy = -r; dy <= r; dy++) {
        int dx_max = sqrt(r*r - dy*dy);
        // Hranice srpku je druhý kruh posunutý vpravo
        int light_boundary = sqrt(r*r - dy*dy - offset*offset) - offset;
        if (light_boundary < 0) light_boundary = 0;
        for (int dx = light_boundary; dx <= dx_max; dx++) {
          tft.drawPixel(mx + dx, my + dy, moonColor);
        }
      }
      break;
    }
    
    case 2: {
      // FIRST QUARTER - PRAVÁ polovina osvětlena
      tft.fillCircle(mx, my, r - 1, shadowColor);
      for (int dy = -r; dy <= r; dy++) {
        int dx_max = sqrt(r*r - dy*dy);
        for (int dx = 0; dx <= dx_max; dx++) {
          tft.drawPixel(mx + dx, my + dy, moonColor);
        }
      }
      break;
    }
    
    case 3: {
      // WAXING GIBBOUS - Skoro plný, srpek stínu zleva (křivá hranice)
      tft.fillCircle(mx, my, r - 1, moonColor);
      // Stín se vytváří průnikem dvou kruhů - jeden posunutý vlevo
      int offset = r / 3;
      for (int dy = -r; dy <= r; dy++) {
        int dx_max = sqrt(r*r - dy*dy);
        // Hranice stínu je druhý kruh posunutý vlevo
        int shadow_boundary = -(sqrt(r*r - dy*dy - offset*offset) - offset);
        if (shadow_boundary > 0) shadow_boundary = 0;
        for (int dx = -dx_max; dx <= shadow_boundary; dx++) {
          tft.drawPixel(mx + dx, my + dy, shadowColor);
        }
      }
      break;
    }
    
    case 4: {
      // FULL MOON - Zcela osvětlen
      tft.fillCircle(mx, my, r - 1, moonColor);
      break;
    }
    
    case 5: {
      // WANING GIBBOUS - Skoro plný, srpek stínu zprava (křivá hranice)
      tft.fillCircle(mx, my, r - 1, moonColor);
      // Stín se vytváří průnikem dvou kruhů - jeden posunutý vpravo
      int offset = r / 3;
      for (int dy = -r; dy <= r; dy++) {
        int dx_max = sqrt(r*r - dy*dy);
        // Hranice stínu je druhý kruh posunutý vpravo
        int shadow_boundary = sqrt(r*r - dy*dy - offset*offset) - offset;
        if (shadow_boundary < 0) shadow_boundary = 0;
        for (int dx = shadow_boundary; dx <= dx_max; dx++) {
          tft.drawPixel(mx + dx, my + dy, shadowColor);
        }
      }
      break;
    }
    
    case 6: {
      // LAST QUARTER - LEVÁ polovina osvětlena
      tft.fillCircle(mx, my, r - 1, shadowColor);
      for (int dy = -r; dy <= r; dy++) {
        int dx_max = sqrt(r*r - dy*dy);
        for (int dx = -dx_max; dx <= 0; dx++) {
          tft.drawPixel(mx + dx, my + dy, moonColor);
        }
      }
      break;
    }
    
    case 7: {
      // WANING CRESCENT - Srpek z LEVÉ strany (křivá hranice)
      tft.fillCircle(mx, my, r - 1, shadowColor);
      // Srpek se vytváří průnikem dvou kruhů - jeden je střed měsíce, druhý je posunutý
      int offset = r / 3;
      for (int dy = -r; dy <= r; dy++) {
        int dx_max = sqrt(r*r - dy*dy);
        // Hranice srpku je druhý kruh posunutý vlevo
        int light_boundary = -(sqrt(r*r - dy*dy - offset*offset) - offset);
        if (light_boundary > 0) light_boundary = 0;
        for (int dx = -dx_max; dx <= light_boundary; dx++) {
          tft.drawPixel(mx + dx, my + dy, moonColor);
        }
      }
      break;
    }
    
    default: {
      tft.drawCircle(mx, my, r, textColor);
      break;
    }
  }
}




int getMoonPhase(int y, int m, int d) {
  // Přesnější výpočet měsíční fáze
  // Založeno na astronomickém algoritmu s přesností na dny
  
  // Výpočet Julian Date Number
  if (m < 3) {
    y--;
    m += 12;
  }
  
  int a = y / 100;
  int b = 2 - a + (a / 4);
  
  long jd = (long)(365.25 * (y + 4716)) + 
            (long)(30.6001 * (m + 1)) + 
            d + b - 1524;
  
  // Výpočet fáze měsíce
  // Referenční nov: 6. ledna 2000, 18:14 UTC (JD 2451550.26)
  double daysSinceNew = jd - 2451550.1;
  
  // Lunární cyklus je 29.53058867 dní
  double lunationCycle = 29.53058867;
  double currentLunation = daysSinceNew / lunationCycle;
  
  // Získáme pozici v aktuálním cyklu (0.0 - 1.0)
  double phasePosition = currentLunation - floor(currentLunation);
  
  // Konverze na 8 fází (0-7) s přesnými hranicemi
  // Každá fáze zabírá 1/8 cyklu, hranice jsou uprostřed přechodů
  int phase;
  if (phasePosition < 0.0625) phase = 0;       // New Moon (0.000 - 0.062)
  else if (phasePosition < 0.1875) phase = 1;  // Waxing Crescent (0.062 - 0.188)
  else if (phasePosition < 0.3125) phase = 2;  // First Quarter (0.188 - 0.312)
  else if (phasePosition < 0.4375) phase = 3;  // Waxing Gibbous (0.312 - 0.438)
  else if (phasePosition < 0.5625) phase = 4;  // Full Moon (0.438 - 0.562)
  else if (phasePosition < 0.6875) phase = 5;  // Waning Gibbous (0.562 - 0.688)
  else if (phasePosition < 0.8125) phase = 6;  // Last Quarter (0.688 - 0.812)
  else if (phasePosition < 0.9375) phase = 7;  // Waning Crescent (0.812 - 0.938)
  else phase = 0;                               // New Moon (0.938 - 1.000)
  
  return phase;
}

String todayNameday = "--";
int lastNamedayDay = -1;
int lastNamedayHour = -1;
bool namedayValid = false;

#define T_CS 33
#define T_IRQ 36
#define T_CLK 25
#define T_DIN 32
#define T_DOUT 39
#define LCD_BL_PIN 21

String countryToISO(String country) {
  country.toLowerCase();
  if (country.indexOf("czech") >= 0) return "CZ";
  if (country.indexOf("slovak") >= 0) return "SK";
  if (country.indexOf("german") >= 0) return "DE";
  if (country.indexOf("austria") >= 0) return "AT";
  if (country.indexOf("poland") >= 0) return "PL";
  if (country.indexOf("france") >= 0) return "FR";
  if (country.indexOf("italy") >= 0) return "IT";
  if (country.indexOf("spain") >= 0) return "ES";
  if (country.indexOf("united states") >= 0) return "US";
  if (country.indexOf("united kingdom") >= 0) return "GB";
  return "US";
}

String removeDiacritics(String input) {
  String output = input;
  // Malá písmena
  output.replace("á", "a"); output.replace("č", "c"); output.replace("ď", "d");
  output.replace("é", "e"); output.replace("ě", "e"); output.replace("í", "i");
  output.replace("ľ", "l"); output.replace("ĺ", "l"); output.replace("ň", "n");
  output.replace("ó", "o"); output.replace("ô", "o"); output.replace("ř", "r");
  output.replace("š", "s"); output.replace("ť", "t"); output.replace("ú", "u");
  output.replace("ů", "u"); output.replace("ý", "y"); output.replace("ž", "z");
  
  // Velká písmena
  output.replace("Á", "A"); output.replace("Č", "C"); output.replace("Ď", "D");
  output.replace("É", "E"); output.replace("Ě", "E"); output.replace("Í", "I");
  output.replace("Ľ", "L"); output.replace("Ĺ", "L"); output.replace("Ň", "N");
  output.replace("Ó", "O"); output.replace("Ô", "O"); output.replace("Ř", "R");
  output.replace("Š", "S"); output.replace("Ť", "T"); output.replace("Ú", "U");
  output.replace("Ů", "U"); output.replace("Ý", "Y"); output.replace("Ž", "Z");
  
  return output;
}

XPT2046_Touchscreen ts(T_CS, T_IRQ);

const char* ntpServer = "pool.ntp.org";
long gmtOffset_sec = 3600;
int daylightOffset_sec = 0;

const int clockX = 230;
const int clockY = 85;
const int radius = 67;
int lastHour = -1, lastMin = -1, lastSec = -1, lastDay = -1;
int brightness = 255;
String cityName = "Plzen";
unsigned long lastWifiStatusCheck = 0;
int lastWifiStatus = -1;
bool forceClockRedraw = false;

enum ScreenState {
  CLOCK, SETTINGS, WIFICONFIG, KEYBOARD, WEATHERCONFIG, REGIONALCONFIG, GRAPHICSCONFIG, FIRMWARE_SETTINGS, COUNTRYSELECT, CITYSELECT, LOCATIONCONFIRM, CUSTOMCITYINPUT, CUSTOMCOUNTRYINPUT, COUNTRYLOOKUPCONFIRM, CITYLOOKUPCONFIRM
};
ScreenState currentState = CLOCK;

bool regionAutoMode = true;
String selectedCountry = "Czech Republic";
String selectedCity;
String selectedTimezone;
String customCityInput;
String customCountryInput;
String lookupCountry;
String lookupCity;
String lookupTimezone;
String countryName = "Czech Republic"; 
String timezoneName = "Europe/Prague"; 
int lookupGmtOffset = 3600;
int lookupDstOffset = 3600;
String posixTZ = "CET-1CEST,M3.5.0,M10.5.0/3";

#define MAX_RECENT_CITIES 10
struct RecentCity {
  String city;
  String country;
  String timezone;
  int gmtOffset;
  int dstOffset;
};
RecentCity recentCities[MAX_RECENT_CITIES];
int recentCount = 0;

unsigned long lastTouchTime = 0;
int menuOffset = 0;
int countryOffset = 0;
int cityOffset = 0;
const int MENU_BASE_Y = 70;
const int MENU_ITEM_HEIGHT = 35;
const int MENU_ITEM_GAP = 8;
const int MENU_ITEM_SPACING = MENU_ITEM_HEIGHT + MENU_ITEM_GAP;

String ssid, password, selectedSSID, passwordBuffer;
const int MAX_NETWORKS = 20;
String wifiSSIDs[MAX_NETWORKS];
int wifiCount = 0, wifiOffset = 0;
bool keyboardNumbers = false;
bool keyboardShift = false;
bool showPassword = false; // Výchozí stav: heslo je skryté (hvězdičky)

const int TOUCH_X_MIN = 200;
const int TOUCH_X_MAX = 3900;
const int TOUCH_Y_MIN = 200;
const int TOUCH_Y_MAX = 3900;
const int SCREEN_WIDTH = 320;
const int SCREEN_HEIGHT = 240;

struct CityEntry {
  const char* name;
  const char* timezone;
  int gmtOffset;
  int dstOffset;
};

const CityEntry czechCities[] = {
  {"Brno", "Europe/Prague", 3600, 3600},
  {"Ceska Budejovice", "Europe/Prague", 3600, 3600},
  {"Jihlava", "Europe/Prague", 3600, 3600},
  {"Karlovy Vary", "Europe/Prague", 3600, 3600},
  {"Liberec", "Europe/Prague", 3600, 3600},
  {"Olomouc", "Europe/Prague", 3600, 3600},
  {"Ostrava", "Europe/Prague", 3600, 3600},
  {"Pardubice", "Europe/Prague", 3600, 3600},
  {"Plzen", "Europe/Prague", 3600, 3600},
  {"Praha", "Europe/Prague", 3600, 3600},
};

const CityEntry slovakCities[] = {
  {"Banska Bystrica", "Europe/Bratislava", 3600, 3600},
  {"Bardejov", "Europe/Bratislava", 3600, 3600},
  {"Bratislava", "Europe/Bratislava", 3600, 3600},
  {"Kosice", "Europe/Bratislava", 3600, 3600},
  {"Liptovsky Mikulas", "Europe/Bratislava", 3600, 3600},
  {"Lucenec", "Europe/Bratislava", 3600, 3600},
  {"Nitra", "Europe/Bratislava", 3600, 3600},
  {"Poprad", "Europe/Bratislava", 3600, 3600},
  {"Presov", "Europe/Bratislava", 3600, 3600},
  {"Zilina", "Europe/Bratislava", 3600, 3600},
};

const CityEntry germanyCities[] = {
  {"Aachen", "Europe/Berlin", 3600, 3600},
  {"Berlin", "Europe/Berlin", 3600, 3600},
  {"Cologne", "Europe/Berlin", 3600, 3600},
  {"Dortmund", "Europe/Berlin", 3600, 3600},
  {"Dresden", "Europe/Berlin", 3600, 3600},
  {"Dusseldorf", "Europe/Berlin", 3600, 3600},
  {"Essen", "Europe/Berlin", 3600, 3600},
  {"Frankfurt", "Europe/Berlin", 3600, 3600},
  {"Hamburg", "Europe/Berlin", 3600, 3600},
  {"Munich", "Europe/Berlin", 3600, 3600},
};

const CityEntry austriaCities[] = {
  {"Dornbirn", "Europe/Vienna", 3600, 3600},
  {"Graz", "Europe/Vienna", 3600, 3600},
  {"Hallein", "Europe/Vienna", 3600, 3600},
  {"Innsbruck", "Europe/Vienna", 3600, 3600},
  {"Klagenfurt", "Europe/Vienna", 3600, 3600},
  {"Linz", "Europe/Vienna", 3600, 3600},
  {"Salzburg", "Europe/Vienna", 3600, 3600},
  {"Sankt Polten", "Europe/Vienna", 3600, 3600},
  {"Vienna", "Europe/Vienna", 3600, 3600},
  {"Wels", "Europe/Vienna", 3600, 3600},
};

const CityEntry polonyCities[] = {
  {"Bialystok", "Europe/Warsaw", 3600, 3600},
  {"Bydgoszcz", "Europe/Warsaw", 3600, 3600},
  {"Cracow", "Europe/Warsaw", 3600, 3600},
  {"Gdansk", "Europe/Warsaw", 3600, 3600},
  {"Gdynia", "Europe/Warsaw", 3600, 3600},
  {"Katowice", "Europe/Warsaw", 3600, 3600},
  {"Krakow", "Europe/Warsaw", 3600, 3600},
  {"Poznan", "Europe/Warsaw", 3600, 3600},
  {"Szczecin", "Europe/Warsaw", 3600, 3600},
  {"Warsaw", "Europe/Warsaw", 3600, 3600},
};

const CityEntry franceCities[] = {
  {"Amiens", "Europe/Paris", 3600, 3600},
  {"Bordeaux", "Europe/Paris", 3600, 3600},
  {"Brest", "Europe/Paris", 3600, 3600},
  {"Dijon", "Europe/Paris", 3600, 3600},
  {"Grenoble", "Europe/Paris", 3600, 3600},
  {"Lille", "Europe/Paris", 3600, 3600},
  {"Lyon", "Europe/Paris", 3600, 3600},
  {"Marseille", "Europe/Paris", 3600, 3600},
  {"Paris", "Europe/Paris", 3600, 3600},
  {"Toulouse", "Europe/Paris", 3600, 3600},
};

const CityEntry unitedStatesCities[] = {
  {"Atlanta", "America/New_York", -18000, 3600},
  {"Boston", "America/New_York", -18000, 3600},
  {"Charlotte", "America/New_York", -18000, 3600},
  {"Chicago", "America/Chicago", -21600, 3600},
  {"Dallas", "America/Chicago", -21600, 3600},
  {"Denver", "America/Denver", -25200, 3600},
  {"Detroit", "America/New_York", -18000, 3600},
  {"Houston", "America/Chicago", -21600, 3600},
  {"Los Angeles", "America/Los_Angeles", -28800, 3600},
  {"Miami", "America/New_York", -18000, 3600},
  {"New York", "America/New_York", -18000, 3600},
  {"Philadelphia", "America/New_York", -18000, 3600},
  {"Phoenix", "America/Phoenix", -25200, 0},
  {"San Francisco", "America/Los_Angeles", -28800, 3600},
  {"Seattle", "America/Los_Angeles", -28800, 3600},
};

const CityEntry unitedKingdomCities[] = {
  {"Bath", "Europe/London", 0, 3600},
  {"Belfast", "Europe/London", 0, 3600},
  {"Birmingham", "Europe/London", 0, 3600},
  {"Bristol", "Europe/London", 0, 3600},
  {"Cardiff", "Europe/London", 0, 3600},
  {"Edinburgh", "Europe/London", 0, 3600},
  {"Leeds", "Europe/London", 0, 3600},
  {"Liverpool", "Europe/London", 0, 3600},
  {"London", "Europe/London", 0, 3600},
  {"Manchester", "Europe/London", 0, 3600},
};

const CityEntry japanCities[] = {
  {"Aomori", "Asia/Tokyo", 32400, 0},
  {"Fukuoka", "Asia/Tokyo", 32400, 0},
  {"Hiroshima", "Asia/Tokyo", 32400, 0},
  {"Kobe", "Asia/Tokyo", 32400, 0},
  {"Kyoto", "Asia/Tokyo", 32400, 0},
  {"Nagoya", "Asia/Tokyo", 32400, 0},
  {"Osaka", "Asia/Tokyo", 32400, 0},
  {"Sapporo", "Asia/Tokyo", 32400, 0},
  {"Tokyo", "Asia/Tokyo", 32400, 0},
  {"Yokohama", "Asia/Tokyo", 32400, 0},
};

const CityEntry australiaCities[] = {
  {"Adelaide", "Australia/Adelaide", 34200, 3600},
  {"Brisbane", "Australia/Brisbane", 36000, 0},
  {"Canberra", "Australia/Sydney", 36000, 3600},
  {"Darwin", "Australia/Darwin", 34200, 0},
  {"Hobart", "Australia/Hobart", 36000, 3600},
  {"Melbourne", "Australia/Melbourne", 36000, 3600},
  {"Perth", "Australia/Perth", 28800, 0},
  {"Sydney", "Australia/Sydney", 36000, 3600},
  {"Townsville", "Australia/Brisbane", 36000, 0},
  {"Wollongong", "Australia/Sydney", 36000, 3600},
};

const CityEntry chinaCities[] = {
  {"Beijing", "Asia/Shanghai", 28800, 0},
  {"Chongqing", "Asia/Shanghai", 28800, 0},
  {"Guangzhou", "Asia/Shanghai", 28800, 0},
  {"Hangzhou", "Asia/Shanghai", 28800, 0},
  {"Hong Kong", "Asia/Hong_Kong", 28800, 0},
  {"Shanghai", "Asia/Shanghai", 28800, 0},
  {"Shenzhen", "Asia/Shanghai", 28800, 0},
  {"Tianjin", "Asia/Shanghai", 28800, 0},
  {"Wuhan", "Asia/Shanghai", 28800, 0},
  {"Xian", "Asia/Shanghai", 28800, 0},
};

struct CountryEntry {
  const char* code;
  const char* name;
  const CityEntry* cities;
  int cityCount;
};

const CountryEntry countries[] = {
  {"AT", "Austria", austriaCities, 10},
  {"AU", "Australia", australiaCities, 10},
  {"CN", "China", chinaCities, 10},
  {"CZ", "Czech Republic", czechCities, 10},
  {"DE", "Germany", germanyCities, 10},
  {"FR", "France", franceCities, 10},
  {"GB", "United Kingdom", unitedKingdomCities, 10},
  {"JP", "Japan", japanCities, 10},
  {"PL", "Poland", polonyCities, 10},
  {"SK", "Slovakia", slovakCities, 10},
  {"US", "United States", unitedStatesCities, 15},
};
const int COUNTRIES_COUNT = 11;

uint16_t getBgColor() { 
  if (themeMode == 0) return isWhiteTheme ? TFT_WHITE : TFT_BLACK;
  if (themeMode == 1) return isWhiteTheme ? TFT_WHITE : TFT_BLACK;
  if (themeMode == 2) return blueDark; // MODRÁ - tmavé pozadí
  if (themeMode == 3) return yellowDark; // ŽLUTÁ - tmavé pozadí
  return TFT_BLACK;
}

uint16_t getTextColor() { 
  if (themeMode == 0) return isWhiteTheme ? TFT_BLACK : TFT_WHITE;
  if (themeMode == 1) return isWhiteTheme ? TFT_BLACK : TFT_WHITE;
  if (themeMode == 2) return blueLight; // MODRÁ - světlý text
  if (themeMode == 3) return yellowLight; // ŽLUTÁ - světlý text
  return TFT_WHITE;
}

uint16_t getSecHandColor() { 
  if (themeMode == 2) return yellowLight;   // Sekundová ručička v modrém tématu = žlutá
  if (themeMode == 3) return blueLight;     // Sekundová ručička v žlutém tématu = modrá
  return isWhiteTheme ? TFT_RED : TFT_YELLOW; 
}


void drawWifiIndicator() {
  int wifiStatus = WiFi.status();
  uint16_t color = wifiStatus == WL_CONNECTED ? TFT_GREEN : TFT_RED;
  tft.fillCircle(300, 20, 6, color);
}

// Ikona dostupné aktualizace (zelená šipka vedle WiFi)
void drawUpdateIndicator() {
  if (!updateAvailable) return;
  
  int iconX = 310;  // Vedle WiFi ikony
  int iconY = 12;
  
  // Zelená šipka dolů (download symbol)
  tft.fillTriangle(iconX, iconY + 8, iconX + 4, iconY, iconX + 8, iconY + 8, TFT_GREEN);
  tft.fillRect(iconX + 2, iconY + 8, 4, 6, TFT_GREEN);
  tft.fillRect(iconX, iconY + 14, 8, 2, TFT_GREEN);
}

void loadRecentCities() {
  prefs.begin("sys", false);
  for (int i = 0; i < MAX_RECENT_CITIES; i++) {
    String prefix = "recent" + String(i);
    String city = prefs.getString((prefix + "c").c_str(), "");
    if (city.length() == 0) break;
    recentCities[i].city = city;
    recentCities[i].country = prefs.getString((prefix + "co").c_str(), "");
    recentCities[i].timezone = prefs.getString((prefix + "tz").c_str(), "");
    recentCities[i].gmtOffset = prefs.getInt((prefix + "go").c_str(), 3600);
    recentCities[i].dstOffset = prefs.getInt((prefix + "do").c_str(), 3600);
    recentCount++;
  }
  prefs.end();
}

void addToRecentCities(String city, String country, String timezone, int gmtOffset, int dstOffset) {
  for (int i = 0; i < recentCount; i++) {
    if (recentCities[i].city == city && recentCities[i].country == country) {
      RecentCity temp = recentCities[i];
      for (int j = i; j > 0; j--) recentCities[j] = recentCities[j - 1];
      recentCities[0] = temp;
      return;
    }
  }
  if (recentCount < MAX_RECENT_CITIES) {
    for (int i = recentCount - 1; i >= 0; i--) recentCities[i + 1] = recentCities[i];
  } else {
    recentCount = MAX_RECENT_CITIES - 1;
    for (int i = recentCount - 1; i >= 0; i--) recentCities[i + 1] = recentCities[i];
  }
  recentCities[0].city = city;
  recentCities[0].country = country;
  recentCities[0].timezone = timezone;
  recentCities[0].gmtOffset = gmtOffset;
  recentCities[0].dstOffset = dstOffset;

  prefs.begin("sys", false);
  for (int i = 0; i < recentCount; i++) {
    String prefix = "recent" + String(i);
    prefs.putString((prefix + "c").c_str(), recentCities[i].city);
    prefs.putString((prefix + "co").c_str(), recentCities[i].country);
    prefs.putString((prefix + "tz").c_str(), recentCities[i].timezone);
    prefs.putInt((prefix + "go").c_str(), recentCities[i].gmtOffset);
    prefs.putInt((prefix + "do").c_str(), recentCities[i].dstOffset);
  }
  prefs.end();
}


String toTitleCase(String input) {
  input.toLowerCase();
  if (input.length() > 0) input[0] = toupper(input[0]);
  for (int i = 1; i < input.length(); i++) {
    if (input[i - 1] == ' ') input[i] = toupper(input[i]);
  }
  return input;
}

bool fuzzyMatch(String input, String target) {
  String inp = input;
  String tgt = target;
  inp.toLowerCase();
  tgt.toLowerCase();
  if (inp == tgt) return true;
  if (tgt.indexOf(inp) >= 0) return true;
  if (inp.length() >= 3 && tgt.indexOf(inp.substring(0, 3)) >= 0) return true;
  return false;
}

bool lookupCountryRESTAPI(String countryName) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[LOOKUP-REST] WiFi not connected");
    return false;
  }
  countryName = toTitleCase(countryName);
  Serial.println("[LOOKUP-REST] Searching REST API " + countryName);

  HTTPClient http;
  http.setTimeout(8000);

  String searchName = countryName;
  searchName.replace(" ", "%20");
  String url = "https://restcountries.com/v3.1/name/" + searchName + "?fullText=false";
  Serial.println("[LOOKUP-REST] URL " + url);

  http.begin(url);
  http.setUserAgent("ESP32");

  int httpCode = http.GET();
  Serial.println("[LOOKUP-REST] HTTP Code " + String(httpCode));

  if (httpCode != 200) {
    Serial.print("[LOOKUP-REST] HTTP Error ");
    Serial.println(httpCode);
    http.end();
    return false;
  }

  String response = http.getString();
  
  StaticJsonDocument<2000> doc;
  DeserializationError error = deserializeJson(doc, response);
  if (error) {
    Serial.println("[LOOKUP-REST] JSON error " + String(error.c_str()));
    http.end();
    return false;
  }

  if (doc.is<JsonArray>()) {
    JsonArray arr = doc.as<JsonArray>();
    if (arr.size() > 0) {
      JsonObject first = arr[0];
      if (first["name"].is<JsonObject>()) {
        JsonObject nameObj = first["name"];
        if (nameObj["common"].is<const char*>()) {
          lookupCountry = nameObj["common"].as<String>();
          Serial.println("[LOOKUP-REST] FOUND " + lookupCountry);
          http.end();
          return true;
        }
      }
    }
  }

  Serial.println("[LOOKUP-REST] HTTP Error " + String(httpCode));
  http.end();
  return false;
}

bool lookupCountryEmbedded(String countryName) {
  countryName = toTitleCase(countryName);
  Serial.println("[LOOKUP-EMB] Searching embedded " + countryName);
  for (int i = 0; i < COUNTRIES_COUNT; i++) {
    if (fuzzyMatch(countryName, String(countries[i].name))) {
      lookupCountry = String(countries[i].name);
      Serial.println("[LOOKUP-EMB] FOUND " + lookupCountry);
      return true;
    }
  }
  return false;
}

bool lookupCountryGeonames(String countryName) {
  if (lookupCountryEmbedded(countryName)) return true;
  if (WiFi.status() == WL_CONNECTED) {
    if (lookupCountryRESTAPI(countryName)) return true;
  }
  return false;
}

// ============================================
// OPRAVA 1: Získání Timezone z API (pro celý svět)
// ============================================
void detectTimezoneFromCoords(float lat, float lon, String countryHint) {
  if (WiFi.status() != WL_CONNECTED) {
    // Fallback pokud není wifi, ale to by se při lookupu nemělo stát
    lookupTimezone = "Europe/Prague";
    lookupGmtOffset = 3600;
    lookupDstOffset = 3600;
    return;
  }

  Serial.println("[TZ-AUTO] Detecting timezone from API for: " + String(lat,4) + ", " + String(lon,4));
  
  HTTPClient http;
  // Použijeme Open-Meteo, které vrací "utc_offset_seconds" a "timezone"
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(lat, 4) + "&longitude=" + String(lon, 4) + "&timezone=auto&daily=weather_code&foreground_days=1";
  
  http.setTimeout(8000);
  http.begin(url);
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048); // Zvětšený buffer
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      // 1. Získat název zóny
      if (doc.containsKey("timezone")) {
        lookupTimezone = doc["timezone"].as<String>();
      } else {
        lookupTimezone = "UTC";
      }

      // 2. Získat offset v sekundách
      if (doc.containsKey("utc_offset_seconds")) {
        int totalOffset = doc["utc_offset_seconds"].as<int>();
        
        // Nastavíme GMT offset na aktuální posun a DST na 0 
        // (protože API vrací už sečtený offset včetně letního času)
        lookupGmtOffset = totalOffset;
        lookupDstOffset = 0; 
        
        Serial.println("[TZ-AUTO] API Found: " + lookupTimezone + " Offset: " + String(totalOffset));
        http.end();
        return;
      }
    } else {
      Serial.println("[TZ-AUTO] JSON Error");
    }
  } else {
    Serial.println("[TZ-AUTO] HTTP Error: " + String(httpCode));
  }
  http.end();

  // Fallback pokud API selže - alespoň zkusíme základní regiony podle hintu
  Serial.println("[TZ-AUTO] API Failed, using basic fallback");
  if (countryHint == "United Kingdom" || countryHint == "Ireland" || countryHint == "Portugal") {
     lookupTimezone = "Europe/London"; lookupGmtOffset = 0; lookupDstOffset = 3600;
  } else if (countryHint == "China") {
     lookupTimezone = "Asia/Shanghai"; lookupGmtOffset = 28800; lookupDstOffset = 0;
  } else if (countryHint == "Japan") {
     lookupTimezone = "Asia/Tokyo"; lookupGmtOffset = 32400; lookupDstOffset = 0;
  } else if (countryHint.indexOf("America") >= 0 || countryHint == "Canada" || countryHint == "USA") {
     // Hrubý odhad pro Ameriku pokud API selže
     lookupTimezone = "America/New_York"; lookupGmtOffset = -18000; lookupDstOffset = 3600;
  } else {
     lookupTimezone = "Europe/Prague"; lookupGmtOffset = 3600; lookupDstOffset = 3600;
  }
}

// ============================================
// OPRAVA 2: Ukládání do GLOBÁLNÍCH souřadnic
// ============================================
bool lookupCityNominatim(String cityName, String countryHint) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[LOOKUP-CITY-NOM] WiFi not connected");
    return false;
  }
  cityName = toTitleCase(cityName);
  Serial.println("[LOOKUP-CITY-NOM] Searching " + cityName + " in " + countryHint);

  HTTPClient http;
  http.setTimeout(12000);

  String searchCity = cityName;
  searchCity.replace(" ", "%20");
  String searchCountry = countryHint;
  searchCountry.replace(" ", "%20");
  String url = "https://nominatim.openstreetmap.org/search?format=json&addressdetails=1&limit=1&q=" + searchCity + "%2C" + searchCountry;
  Serial.println("[LOOKUP-CITY-NOM] URL " + url);

  http.begin(url);
  http.addHeader("User-Agent", "ESP32-DataDisplay/1.0"); // Nominatim vyžaduje User-Agent
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String response = http.getString();
    StaticJsonDocument<4000> doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
      Serial.println("[LOOKUP-CITY-NOM] JSON error");
      http.end(); return false;
    }

    if (doc.is<JsonArray>()) {
      JsonArray arr = doc.as<JsonArray>();
      if (arr.size() > 0) {
        JsonObject first = arr[0];
        if (first["name"].is<const char*>() && first["lat"].is<const char*>() && first["lon"].is<const char*>()) {
          String apiName = first["name"].as<String>();
          // Nominatim může vrátit název v lokální abecedě (arabsky, čínsky...)
          // Pokud obsahuje non-ASCII znaky, použijeme jméno zadané uživatelem
          bool isAscii = true;
          for (int ci = 0; ci < (int)apiName.length(); ci++) {
            if ((unsigned char)apiName[ci] > 127) { isAscii = false; break; }
          }
          lookupCity = isAscii ? apiName : cityName;
          
          // ZDE BYLA CHYBA: odstraněno "float" před lat/lon, aby se zapsalo do globálních proměnných
          lat = atof(first["lat"].as<const char*>());
          lon = atof(first["lon"].as<const char*>());
          
          Serial.println("[LOOKUP-CITY-NOM] FOUND " + lookupCity + " Lat " + String(lat, 4) + ", Lon " + String(lon, 4));
          
          // Zavoláme detekci zóny s nalezenými souřadnicemi
          detectTimezoneFromCoords(lat, lon, countryHint);
          
          Serial.println("[LOOKUP-CITY-NOM] Timezone set " + lookupTimezone);
          http.end();
          return true;
        }
      }
    }
  }
  http.end();
  return false;
}

bool lookupCityGeonames(String cityName, String countryHint) {
  cityName = toTitleCase(cityName);
  Serial.println("[LOOKUP-CITY] Searching " + cityName + " in " + countryHint);

  for (int i = 0; i < COUNTRIES_COUNT; i++) {
    if (String(countries[i].name) == countryHint || String(countries[i].code) == countryHint) {
      for (int j = 0; j < countries[i].cityCount; j++) {
        if (fuzzyMatch(cityName, countries[i].cities[j].name)) {
          lookupCity = countries[i].cities[j].name;
          lookupTimezone = countries[i].cities[j].timezone;
          lookupGmtOffset = countries[i].cities[j].gmtOffset;
          lookupDstOffset = countries[i].cities[j].dstOffset;
          Serial.println("[LOOKUP-CITY] FOUND in embedded " + lookupCity);
          return true;
        }
      }
    }
  }

  Serial.println("[LOOKUP-CITY] NOT in embedded DB, trying Nominatim API...");
  if (WiFi.status() == WL_CONNECTED) {
    if (lookupCityNominatim(cityName, countryHint)) {
      Serial.println("[LOOKUP-CITY] FOUND via Nominatim");
      return true;
    } else {
      Serial.println("[LOOKUP-CITY] WiFi not connected, cannot use Nominatim");
    }
  }
  Serial.println("[LOOKUP-CITY] NOT FOUND anywhere");
  return false;
}

void getCountryCities(String countryName, String cities[], int &count) {
  count = 0;
  for (int i = 0; i < COUNTRIES_COUNT; i++) {
    if (String(countries[i].name) == countryName) {
      count = countries[i].cityCount;
      for (int j = 0; j < count; j++) {
        cities[j] = countries[i].cities[j].name;
      }
      return;
    }
  }
}

bool getTimezoneForCity(String countryName, String city, String &timezone, int &gmt, int &dst) {
  for (int i = 0; i < COUNTRIES_COUNT; i++) {
    if (String(countries[i].name) == countryName) {
      for (int j = 0; j < countries[i].cityCount; j++) {
        if (String(countries[i].cities[j].name) == city) {
          timezone = countries[i].cities[j].timezone;
          gmt = countries[i].cities[j].gmtOffset;
          dst = countries[i].cities[j].dstOffset;
          return true;
        }
      }
    }
  }
  return false;
}

void drawClockFace();
void drawClockStatic();
void drawDateAndWeek(const struct tm *ti);
void updateHands(int h, int m, int s);
void drawSettingsIcon(uint16_t color);
void drawSettingsScreen();
void drawWeatherScreen();
void drawRegionalScreen();
void drawGraphicsScreen();
void drawInitialSetup();
void drawKeyboardScreen();
void drawCountrySelection();
void drawCitySelection();
void drawLocationConfirm();
void drawCustomCityInput();
void drawCustomCountryInput();
void drawCountryLookupConfirm();
void drawCityLookupConfirm();
void scanWifiNetworks();
void drawArrowBack(int x, int y, uint16_t color);
void drawArrowDown(int x, int y, uint16_t color);
void drawArrowUp(int x, int y, uint16_t color);
void showWifiConnectingScreen(String ssid);
void showWifiResultScreen(bool success);
void handleNamedayUpdate();


int getMenuItemY(int itemIndex) {
  return MENU_BASE_Y + itemIndex * MENU_ITEM_SPACING;
}

bool isTouchInMenuItem(int y, int itemIndex) {
  int yPos = getMenuItemY(itemIndex);
  return (y >= yPos && y <= yPos + MENU_ITEM_HEIGHT);
}

void drawSettingsIcon(uint16_t color) {
  int ix = 300, iy = 220;
  int rIn = 3, rMid = 6, rOut = 8;
  tft.fillCircle(ix, iy, rMid, color);
  tft.fillCircle(ix, iy, rIn, getBgColor());
  #define DEGTORAD (PI / 180.0)
  for (int i = 0; i < 8; i++) {
    float a = i * 45 * DEGTORAD;
    float aL = a - 0.2;
    float aR = a + 0.2;
    tft.fillTriangle(ix + cos(aL) * rMid, iy + sin(aL) * rMid, ix + cos(aR) * rMid, iy + sin(aR) * rMid, ix + cos(a) * rOut, iy + sin(a) * rOut, color);
  }
}

void drawArrowBack(int x, int y, uint16_t color) {
  tft.drawRoundRect(x, y, 50, 50, 4, color);
  tft.drawLine(x + 35, y + 15, x + 20, y + 25, color);
  tft.drawLine(x + 35, y + 35, x + 20, y + 25, color);
  tft.drawLine(x + 34, y + 15, x + 19, y + 25, color);
  tft.drawLine(x + 34, y + 35, x + 19, y + 25, color);
}

void syncRegion() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println("[AUTO] Syncing region...");
  
  HTTPClient http;
  http.setTimeout(5000);
http.begin("http://ip-api.com/json?fields=status,city,timezone,lat,lon");

  int httpCode = http.GET();
  if (httpCode == 200) {
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, http.getString());
    
    if (!error && doc["status"] == "success") {
      // 1. Získat data z API do pomocných proměnných
      String detectedCity = doc["city"].as<String>();
      String detectedTimezone = doc["timezone"].as<String>();
      float detectedLat = doc["lat"].as<float>();
      float detectedLon = doc["lon"].as<float>();
      
      Serial.println("[AUTO] Detected: " + detectedCity + ", TZ: " + detectedTimezone);
      Serial.println("[AUTO] Coordinates from IP: " + String(detectedLat, 4) + ", " + String(detectedLon, 4));

      // 2. Nastavit globální 'selected' proměnné pro applyLocation
      selectedCity = detectedCity;
      selectedTimezone = detectedTimezone;
      
// Detekce země podle časové zóny
      if (detectedTimezone.indexOf("Prague") >= 0) {
        selectedCountry = "Czech Republic";
      } else if (detectedTimezone.indexOf("Berlin") >= 0) {
        selectedCountry = "Germany";
      } else if (detectedTimezone.indexOf("Warsaw") >= 0) {
        selectedCountry = "Poland";
      } else if (detectedTimezone.indexOf("Bratislava") >= 0) {
        selectedCountry = "Slovakia";
      } else if (detectedTimezone.indexOf("Paris") >= 0) {
        selectedCountry = "France";
      } else if (detectedTimezone.indexOf("London") >= 0) {
        selectedCountry = "United Kingdom";
      } else if (detectedTimezone.indexOf("New_York") >= 0 || detectedTimezone.indexOf("Chicago") >= 0 ||
                 detectedTimezone.indexOf("Denver") >= 0 || detectedTimezone.indexOf("Los_Angeles") >= 0) {
        selectedCountry = "United States";
      } else if (detectedTimezone.indexOf("Tokyo") >= 0) {
        selectedCountry = "Japan";
      } else if (detectedTimezone.indexOf("Shanghai") >= 0 || detectedTimezone.indexOf("Hong_Kong") >= 0) {
        selectedCountry = "China";
      } else if (detectedTimezone.indexOf("Sydney") >= 0 || detectedTimezone.indexOf("Melbourne") >= 0 ||
                 detectedTimezone.indexOf("Brisbane") >= 0 || detectedTimezone.indexOf("Adelaide") >= 0 ||
                 detectedTimezone.indexOf("Perth") >= 0) {
        selectedCountry = "Australia";
      } else {
        // Fallback pokud neznáme zónu
        if (selectedCountry == "") {
           selectedCountry = "Czech Republic";
        }
      }
      // Odvození POSIX TZ ze zjištěné IANA timezone (správně řeší letní/zimní čas)
      posixTZ = ianaToPostfixTZ(detectedTimezone);
      Serial.println("[AUTO] POSIX TZ derived: " + posixTZ);
      
      Serial.println("[AUTO] SelectedCountry set to: " + selectedCountry);

      // 3. APLIKOVAT ZMĚNY (Uloží, nastaví čas – interně resetuje lat/lon na 0.0)
      applyLocation();

      // 4. PŘEPSAT NULY skutečnými souřadnicemi z IP geolokace
      //    applyLocation() záměrně resetuje lat/lon, proto je musíme dosadit až po něm
      if (detectedLat != 0.0 || detectedLon != 0.0) {
        lat = detectedLat;
        lon = detectedLon;
        prefs.begin("sys", false);
        prefs.putFloat("lat", lat);
        prefs.putFloat("lon", lon);
        prefs.end();
        Serial.println("[AUTO] Coordinates saved: " + String(lat, 4) + ", " + String(lon, 4));
      }
      
    } else {
      Serial.println("[AUTO] JSON Parsing error or status not success");
    }
  } else {
    Serial.println("[AUTO] HTTP Error: " + String(httpCode));
  }
  http.end();
}

void drawArrowDown(int x, int y, uint16_t color) {
  tft.drawRoundRect(x, y, 50, 50, 4, color);
  tft.drawLine(x + 15, y + 20, x + 25, y + 35, color);
  tft.drawLine(x + 35, y + 20, x + 25, y + 35, color);
  tft.drawLine(x + 15, y + 21, x + 25, y + 36, color);
  tft.drawLine(x + 35, y + 21, x + 25, y + 36, color);
}

void drawArrowUp(int x, int y, uint16_t color) {
  tft.drawRoundRect(x, y, 50, 50, 4, color);
  tft.drawLine(x + 15, y + 35, x + 25, y + 20, color);
  tft.drawLine(x + 35, y + 35, x + 25, y + 20, color);
  tft.drawLine(x + 15, y + 34, x + 25, y + 19, color);
  tft.drawLine(x + 35, y + 34, x + 25, y + 19, color);
}

void showWifiConnectingScreen(String ssid) {
  tft.fillScreen(getBgColor());
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Connecting to", 160, 80, 2);
  tft.setTextColor(TFT_SKYBLUE);
  tft.drawString(ssid, 160, 110, 2);
  tft.setTextColor(getTextColor());
  tft.drawString("Please wait...", 160, 150, 2);
}

void showWifiResultScreen(bool success) {
  tft.fillScreen(getBgColor());
  if (success) {
    tft.setTextColor(TFT_GREEN);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Connection Successful!", 160, 100, 2);
  } else {
    tft.setTextColor(TFT_RED);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Connection FAILED", 160, 100, 2);
  }
  delay(2000);
}

void scanWifiNetworks() {
  tft.fillScreen(getBgColor());
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Scanning WiFi...", 160, 120, 2);

  WiFi.mode(WIFI_STA);
  if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false);

  int n = WiFi.scanNetworks();
  wifiCount = (n > 0) ? min(n, MAX_NETWORKS) : 0;

  for (int i = 0; i < wifiCount; i++) {
    wifiSSIDs[i] = WiFi.SSID(i);
  }
  Serial.println("[WIFI] Scan complete. Found " + String(wifiCount) + " networks");
}

void drawSettingsScreen()
{
  tft.fillScreen(getBgColor());
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("SETTINGS", 160, 30, 4);

  String menuItems[] = {"WiFi Setup", "Weather", "Regional", "Graphics", "Firmware"};  // PŘIDÁNO Firmware
  uint16_t colors[] = {TFT_BLUE, TFT_BLUE, TFT_BLUE, TFT_BLUE, TFT_BLUE};  // Přidána 5. barva

  int totalItems = 5;  // ZMĚNĚNO z 4 na 5
  int visibleItems = 4;  // Kolik se vejde na obrazovku najednou

  for (int i = 0; i < totalItems; i++) {
    if (i >= menuOffset && i < menuOffset + visibleItems) {
      int yPos = getMenuItemY(i - menuOffset);
      tft.drawRoundRect(40, yPos, 180, MENU_ITEM_HEIGHT, 6, colors[i]);
      tft.drawRoundRect(39, yPos-1, 182, MENU_ITEM_HEIGHT+2, 6, colors[i]);  // Silný rámeček!
      tft.fillRoundRect(41, yPos+1, 178, MENU_ITEM_HEIGHT-2, 5, getBgColor());  // Výplň
      tft.drawString(menuItems[i], 130, yPos + 17, 2);
    }
  }

  // Šipka nahoru (pokud nejsme na začátku)
  if (menuOffset > 0) {
    tft.drawRoundRect(230, 70, 50, 50, 4, TFT_BLUE);
    drawArrowUp(230, 70, TFT_BLUE);
  }

  // Tlačítko ZPĚT
  tft.drawRoundRect(230, 125, 50, 50, 4, TFT_RED);
  drawArrowBack(230, 125, TFT_RED);

  // Šipka dolů (pokud je více než 4 položky)
  if (menuOffset < (totalItems - visibleItems)) {
    tft.drawRoundRect(230, 180, 50, 50, 4, TFT_BLUE);
    drawArrowDown(230, 180, TFT_BLUE);
  }
}

void drawWeatherScreen() {
  uint16_t bg = getBgColor();
  uint16_t txt = getTextColor();
  tft.fillScreen(bg);

  tft.setFreeFont(&FreeSans12pt7b);
  tft.setTextColor(TFT_ORANGE, bg);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Weather Settings", 160, 20);

  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(txt, bg);
  tft.drawString("Current City:", 160, 70);
  
  tft.setTextColor(TFT_SKYBLUE, bg);
  tft.drawString(cityName == "" ? "Not set (Use Regional)" : cityName, 160, 100);

  // ========== LEVÁ STRANA: TEMPERATURE ==========
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(txt, bg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Temperature", 65, 130);  // Levá polovina
  
  int tempToggleX = 65;
  int tempToggleY = 150;
  
  if (!weatherUnitF) {
    // °C je vybrán
    tft.fillRoundRect(tempToggleX - 50, tempToggleY, 40, 25, 4, TFT_GREEN);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("°C", tempToggleX - 30, tempToggleY + 8, 1);
    tft.drawRoundRect(tempToggleX + 10, tempToggleY, 40, 25, 4, TFT_BLUE);
    tft.setTextColor(txt, bg);
    tft.drawString("°F", tempToggleX + 30, tempToggleY + 8, 1);
  } else {
    // °F je vybrán
    tft.drawRoundRect(tempToggleX - 50, tempToggleY, 40, 25, 4, TFT_BLUE);
    tft.setTextColor(txt, bg);
    tft.drawString("°C", tempToggleX - 30, tempToggleY + 8, 1);
    tft.fillRoundRect(tempToggleX + 10, tempToggleY, 40, 25, 4, TFT_GREEN);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("°F", tempToggleX + 30, tempToggleY + 8, 1);
  }

  // ========== PRAVÁ STRANA: WIND SPEED ==========
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(txt, bg);
  tft.drawString("Wind speed", 230, 130);  // Pravá polovina
  
  int windToggleX = 230;
  int windToggleY = 150;
  
  if (!weatherUnitMph) {
    // km/h je vybrán
    tft.fillRoundRect(windToggleX - 55, windToggleY, 50, 25, 4, TFT_GREEN);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("km/h", windToggleX - 30, windToggleY + 8, 1);
    tft.drawRoundRect(windToggleX + 5, windToggleY, 50, 25, 4, TFT_BLUE);
    tft.setTextColor(txt, bg);
    tft.drawString("mph", windToggleX + 30, windToggleY + 8, 1);
  } else {
    // mph je vybrán
    tft.drawRoundRect(windToggleX - 55, windToggleY, 50, 25, 4, TFT_BLUE);
    tft.setTextColor(txt, bg);
    tft.drawString("km/h", windToggleX - 30, windToggleY + 8, 1);
    tft.fillRoundRect(windToggleX + 5, windToggleY, 50, 25, 4, TFT_GREEN);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("mph", windToggleX + 30, windToggleY + 8, 1);
  }

  tft.setFreeFont(NULL);
  tft.setTextColor(txt, bg);
  tft.drawString("Weather updates every 30 mins", 160, 190);
  tft.drawString("Coordinates: " + String(lat, 2) + ", " + String(lon, 2), 160, 210);

  // Back button
  tft.fillRoundRect(40, 220, 240, 15, 5, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("BACK TO SETTINGS", 160, 225);
}

void drawRegionalScreen() {
  tft.fillScreen(getBgColor());
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("REGIONAL SETUP", 160, 30, 4);

  int toggleX = 160;
  int toggleY = 60;

  if (regionAutoMode) {
    tft.fillRoundRect(toggleX - 55, toggleY - 15, 50, 30, 4, TFT_GREEN);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("AUTO", toggleX - 30, toggleY, 2);
    tft.drawRoundRect(toggleX + 5, toggleY - 15, 50, 30, 4, TFT_BLUE);
    tft.setTextColor(getTextColor());
    tft.drawString("MANUAL", toggleX + 30, toggleY, 2);
  } else {
    tft.drawRoundRect(toggleX - 55, toggleY - 15, 50, 30, 4, TFT_BLUE);
    tft.setTextColor(getTextColor());
    tft.drawString("AUTO", toggleX - 30, toggleY, 2);
    tft.fillRoundRect(toggleX + 5, toggleY - 15, 50, 30, 4, TFT_GREEN);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("MANUAL", toggleX + 30, toggleY, 2);
  }

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(getTextColor());
  tft.drawString("City", 40, 110, 2);
  tft.setTextColor(TFT_SKYBLUE);
  tft.drawString(cityName != "" ? cityName : "---", 40, 130, 2);

  tft.setTextColor(getTextColor());
  tft.drawString("Timezone", 40, 160, 2);
  tft.setTextColor(TFT_SKYBLUE);
  tft.drawString(String(gmtOffset_sec / 3600) + "h", 40, 180, 2);

  tft.setTextDatum(MC_DATUM);
  if (regionAutoMode) {
    tft.drawRoundRect(40, 205, 105, 30, 6, TFT_GREEN);
    tft.drawString("SYNC", 92, 220, 2);
  } else {
    tft.drawRoundRect(40, 205, 105, 30, 6, TFT_GREEN);
    tft.drawString("EDIT", 92, 220, 2);
  }

  tft.drawRoundRect(155, 205, 105, 30, 6, TFT_RED);
  tft.drawString("Back", 207, 220, 2);
}

void drawCountrySelection() {
  tft.fillScreen(getBgColor());
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("SELECT COUNTRY", 160, 30, 4);

  tft.setTextDatum(ML_DATUM);

  int itemsPerScreen = 5;
  for (int i = countryOffset; i < countryOffset + itemsPerScreen && i < COUNTRIES_COUNT; i++) {
    int idx = i - countryOffset;
    int yPos = 70 + idx * 30;
    String txt = String(countries[i].name);
    if (txt.length() > 20) txt = txt.substring(0, 17) + "...";
    tft.drawString(txt, 15, yPos, 2);
    tft.drawFastHLine(10, yPos + 20, 240, TFT_DARKGREY);
  }

  tft.drawString("Custom lookup", 15, 70 + 5 * 30, 2);

  if (countryOffset > 0) drawArrowUp(265, 45, (themeMode == 2) ? yellowLight : TFT_BLUE);
  if (countryOffset + 5 < COUNTRIES_COUNT) drawArrowDown(265, 180, (themeMode == 2) ? yellowLight : TFT_BLUE);
  drawArrowBack(265, 110, TFT_RED);
}

void drawCitySelection() {
  tft.fillScreen(getBgColor());
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.drawString(selectedCountry, 160, 15, 2);
  tft.drawString("SELECT CITY", 160, 35, 4);

  String cities[20];
  int cityCount = 0;
  getCountryCities(selectedCountry, cities, cityCount);

  tft.setTextDatum(ML_DATUM);

  int itemsPerScreen = 5;
  for (int i = cityOffset; i < cityOffset + itemsPerScreen && i < cityCount; i++) {
    int idx = i - cityOffset;
    int yPos = 70 + idx * 30;
    String txt = cities[i];
    if (txt.length() > 20) txt = txt.substring(0, 17) + "...";
    tft.drawString(txt, 15, yPos, 2);
    tft.drawFastHLine(10, yPos + 20, 240, TFT_DARKGREY);
  }

  tft.drawString("Custom lookup", 15, 70 + 5 * 30, 2);

  if (cityOffset > 0) drawArrowUp(265, 45, TFT_BLUE);
  if (cityOffset + 5 < cityCount) drawArrowDown(265, 180, TFT_BLUE);
  drawArrowBack(265, 110, TFT_RED);
}

void drawLocationConfirm() {
  tft.fillScreen(getBgColor());
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("CONFIRM LOCATION", 160, 30, 4);

  tft.setTextDatum(ML_DATUM);
  tft.drawString("City", 40, 80, 2);
  tft.setTextColor(TFT_SKYBLUE);
  tft.drawString(selectedCity, 40, 100, 2);
  tft.setTextColor(getTextColor());
  tft.drawString("Country", 40, 130, 2);
  tft.setTextColor(TFT_SKYBLUE);
  tft.drawString(selectedCountry, 40, 150, 2);
  tft.setTextColor(getTextColor());
  tft.drawString("Timezone", 40, 180, 2);
  tft.setTextColor(TFT_SKYBLUE);
  tft.drawString(selectedTimezone, 40, 200, 2);

  tft.setTextDatum(MC_DATUM);
  tft.drawRoundRect(40, 205, 105, 30, 6, TFT_GREEN);
  tft.drawString("SAVE", 92, 220, 2);
  tft.drawRoundRect(155, 205, 105, 30, 6, TFT_RED);
  tft.drawString("Back", 207, 220, 2);
}

void drawCountryLookupConfirm() {
  tft.fillScreen(getBgColor());
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("COUNTRY FOUND", 160, 30, 4);

  tft.setTextDatum(ML_DATUM);
  tft.drawString("Country", 40, 80, 2);
  tft.setTextColor(TFT_SKYBLUE);
  tft.drawString(lookupCountry, 40, 100, 2);
  tft.setTextColor(getTextColor());
  tft.drawString("Status", 40, 140, 2);
  tft.setTextColor(TFT_GREEN);
  tft.drawString("CONFIRMED", 40, 160, 2);

  tft.setTextDatum(MC_DATUM);
  tft.drawRoundRect(40, 205, 105, 30, 6, TFT_GREEN);
  tft.drawString("NEXT", 92, 220, 2);
  tft.drawRoundRect(155, 205, 105, 30, 6, TFT_RED);
  tft.drawString("Back", 207, 220, 2);
}

void drawCityLookupConfirm() {
  tft.fillScreen(getBgColor());
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("CITY FOUND", 160, 30, 4);

  tft.setTextDatum(ML_DATUM);
  tft.drawString("City", 40, 80, 2);
  tft.setTextColor(TFT_SKYBLUE);
  tft.drawString(lookupCity, 40, 100, 2);
  tft.setTextColor(getTextColor());
  tft.drawString("Timezone", 40, 130, 2);
  tft.setTextColor(TFT_SKYBLUE);
  tft.drawString(lookupTimezone, 40, 150, 2);
  tft.setTextColor(getTextColor());
  tft.drawString("Status", 40, 180, 2);
  tft.setTextColor(TFT_GREEN);
  tft.drawString("CONFIRMED", 40, 200, 2);

  tft.setTextDatum(MC_DATUM);
  tft.drawRoundRect(40, 205, 105, 30, 6, TFT_GREEN);
  tft.drawString("SAVE", 92, 220, 2);
  tft.drawRoundRect(155, 205, 105, 30, 6, TFT_RED);
  tft.drawString("Back", 207, 220, 2);
}

void drawCustomCityInput() {
  tft.fillScreen(getBgColor());
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.drawString("Enter City Name", 160, 20);

  // Input field - design shodný s WiFi
  tft.drawRect(10, 40, 300, 30, isWhiteTheme ? TFT_BLACK : TFT_WHITE);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(customCityInput, 20, 55);

  // Klávesnice - shodný design s WiFi
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(MC_DATUM);
  
  const char *rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
  if (keyboardNumbers) {
    rows[0] = "1234567890";
    rows[1] = "!@#$%^&*(/";
    rows[2] = ")-_+=.,?";
  }

  for (int r = 0; r < 3; r++) {
    int len = strlen(rows[r]);
    for (int i = 0; i < len; i++) {
      int btnX = i * 29 + 2;
      int btnY = 80 + r * 30;
      // Použití menších čtverců a barev dle tématu (shodně s WiFi)
      tft.drawRect(btnX, btnY, 26, 26, isWhiteTheme ? TFT_DARKGREY : TFT_WHITE);
      char ch = rows[r][i];
      if (keyboardShift && !keyboardNumbers) ch = toupper(ch);
      tft.drawString(String(ch), btnX + 13, btnY + 15);
    }
  }

  // Mezerník
  tft.drawRect(2, 170, 316, 25, isWhiteTheme ? TFT_DARKGREY : TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Space", 160, 183);

  // Funkční tlačítka
  int bw = 64; 
  int by = 198; 
  int bh = 35;
  tft.setTextColor(isWhiteTheme ? TFT_BLACK : TFT_WHITE);

  // 1. Shift/CAP
  tft.drawRect(0 * bw + 2, by, bw - 4, bh, isWhiteTheme ? TFT_DARKGREY : TFT_WHITE);
  tft.drawString("Shift", 0 * bw + bw / 2, by + 18);
  
  // 2. 123
  tft.drawRect(1 * bw + 2, by, bw - 4, bh, isWhiteTheme ? TFT_DARKGREY : TFT_WHITE);
  tft.drawString("123", 1 * bw + bw / 2, by + 18);
  
  // 3. Del
  tft.drawRect(2 * bw + 2, by, bw - 4, bh, isWhiteTheme ? TFT_DARKGREY : TFT_WHITE);
  tft.drawString("Del", 2 * bw + bw / 2, by + 18);
  
  // 4. LOOKUP (Zelené - specifické pro City)
  tft.setTextColor(TFT_GREEN);
  tft.drawRect(3 * bw + 2, by, bw - 4, bh, TFT_GREEN);
  tft.drawString("SRCH", 3 * bw + bw / 2, by + 18);
  
  // 5. BACK (Červené/Oranžové - specifické pro City)
  tft.setTextColor(TFT_ORANGE);
  tft.drawRect(4 * bw + 2, by, bw - 4, bh, TFT_ORANGE);
  tft.drawString("BACK", 4 * bw + bw / 2, by + 18);
  
  tft.setTextColor(isWhiteTheme ? TFT_BLACK : TFT_WHITE); // Reset barvy
}

void drawCustomCountryInput() {
  tft.fillScreen(getBgColor());
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.drawString("Enter Country Name", 160, 20);

  // Input field - design shodný s WiFi
  tft.drawRect(10, 40, 300, 30, isWhiteTheme ? TFT_BLACK : TFT_WHITE);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(customCountryInput, 20, 55);

  // Klávesnice - shodný design s WiFi
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(MC_DATUM);
  
  const char *rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
  if (keyboardNumbers) {
    rows[0] = "1234567890";
    rows[1] = "!@#$%^&*(/";
    rows[2] = ")-_+=.,?";
  }

  for (int r = 0; r < 3; r++) {
    int len = strlen(rows[r]);
    for (int i = 0; i < len; i++) {
      int btnX = i * 29 + 2;
      int btnY = 80 + r * 30;
      tft.drawRect(btnX, btnY, 26, 26, isWhiteTheme ? TFT_DARKGREY : TFT_WHITE);
      char ch = rows[r][i];
      if (keyboardShift && !keyboardNumbers) ch = toupper(ch);
      tft.drawString(String(ch), btnX + 13, btnY + 15);
    }
  }

  // Mezerník
  tft.drawRect(2, 170, 316, 25, isWhiteTheme ? TFT_DARKGREY : TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Space", 160, 183);

  // Funkční tlačítka
  int bw = 64; 
  int by = 198; 
  int bh = 35;
  tft.setTextColor(isWhiteTheme ? TFT_BLACK : TFT_WHITE);

  // 1. Shift/CAP
  tft.drawRect(0 * bw + 2, by, bw - 4, bh, isWhiteTheme ? TFT_DARKGREY : TFT_WHITE);
  tft.drawString("Shift", 0 * bw + bw / 2, by + 18);
  
  // 2. 123
  tft.drawRect(1 * bw + 2, by, bw - 4, bh, isWhiteTheme ? TFT_DARKGREY : TFT_WHITE);
  tft.drawString("123", 1 * bw + bw / 2, by + 18);
  
  // 3. Del
  tft.drawRect(2 * bw + 2, by, bw - 4, bh, isWhiteTheme ? TFT_DARKGREY : TFT_WHITE);
  tft.drawString("Del", 2 * bw + bw / 2, by + 18);
  
  // 4. SEARCH (Zelené - specifické pro Country)
  tft.setTextColor(TFT_GREEN);
  tft.drawRect(3 * bw + 2, by, bw - 4, bh, TFT_GREEN);
  tft.drawString("SRCH", 3 * bw + bw / 2, by + 18);
  
  // 5. BACK (Červené/Oranžové - specifické pro Country)
  tft.setTextColor(TFT_ORANGE);
  tft.drawRect(4 * bw + 2, by, bw - 4, bh, TFT_ORANGE);
  tft.drawString("BACK", 4 * bw + bw / 2, by + 18);
  
  tft.setTextColor(isWhiteTheme ? TFT_BLACK : TFT_WHITE); // Reset barvy
}

// ================= FIRMWARE SETTINGS SCREEN =================

void drawFirmwareScreen() {
  tft.fillScreen(getBgColor());
  
  if (themeMode == 2) fillGradientVertical(0, 0, 320, 240, blueDark, blueLight);
  else if (themeMode == 3) fillGradientVertical(0, 0, 320, 240, yellowDark, yellowLight);
  
  // Nadpis
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("FIRMWARE", 160, 30, 4);
  
  // Nastavíme datum pro levý sloupec
  tft.setTextDatum(ML_DATUM);
  
  int yPos = 60;
  
  // Current version
  tft.setTextColor(getTextColor());
  tft.drawString("Current version:", 10, yPos, 2);
  tft.setTextColor(TFT_GREEN);
  tft.drawString(String(FIRMWARE_VERSION), 160, yPos, 2);
  
  yPos += 25;
  
  // Available version
  tft.setTextColor(getTextColor());
  tft.drawString("Available:", 10, yPos, 2);
  if (availableVersion == "" || !updateAvailable) {
    tft.setTextColor(TFT_DARKGREY);
    tft.drawString("-", 160, yPos, 2);
  } else {
    tft.setTextColor(TFT_ORANGE);
    tft.drawString(availableVersion, 160, yPos, 2);
  }
  
  yPos += 35;
  
  // Install mode nadpis
  tft.setTextColor(getTextColor());
  tft.drawString("Install mode:", 10, yPos, 2);
  
  yPos += 25;  // yPos je nyní 145
  
  // Radio buttons pro režim instalace (JEN Auto a By user)
  const char* modes[2] = {"Auto", "By user"};
  for (int i = 0; i < 2; i++) {
    int btnY = yPos + (i * 25);  // 145, 170
    
    // Radio button - kruh má střed na btnY
    tft.drawCircle(20, btnY, 6, getTextColor());
    if (otaInstallMode == i) {
      tft.fillCircle(20, btnY, 4, TFT_GREEN);
    }
    
    // Text - SPRÁVNĚ zarovnán s kruhem
    // ML_DATUM = Middle Left, takže y je vertikální střed textu
    // Kruh má střed na btnY, text má taky střed na btnY
    tft.setTextColor(getTextColor());
    tft.drawString(modes[i], 35, btnY, 2);
  }
  
  // Reset text datum na centrování pro tlačítka
  tft.setTextDatum(MC_DATUM);
  
  // Tlačítko Check Now / Install
  int btnY = 190;
  if (updateAvailable) {
    tft.fillRoundRect(10, btnY, 140, 30, 5, TFT_GREEN);
    tft.setTextColor(TFT_BLACK);
    tft.drawString("INSTALL", 80, btnY + 15, 2);
  } else {
    tft.fillRoundRect(10, btnY, 140, 30, 5, TFT_BLUE);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("CHECK NOW", 80, btnY + 15, 2);
  }
  
  // Tlačítko ZPĚT (stejný styl jako ostatní menu)
  tft.drawRoundRect(230, 125, 50, 50, 4, TFT_RED);
  drawArrowBack(230, 125, TFT_RED);
}


void drawGraphicsScreen() {
  tft.fillScreen(getBgColor());
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("GRAPHICS", 160, 30, 4);
  
  // === TÉMATA ===
  tft.drawString("Themes", 135, 50, 2);

  // BLACK Theme
  tft.drawRoundRect(20, 65, 50, 30, 4, TFT_BLACK);
  tft.drawRoundRect(19, 64, 52, 32, 4, TFT_BLACK);
  tft.fillRoundRect(21, 66, 48, 28, 3, themeMode == 0 ? TFT_WHITE : TFT_DARKGREY);
  tft.setTextColor(TFT_BLACK);
  tft.drawString("BLK", 45, 78, 1);
  tft.setTextColor(getTextColor());

  // WHITE Theme
  tft.drawRoundRect(80, 65, 50, 30, 4, TFT_WHITE);
  tft.drawRoundRect(79, 64, 52, 32, 4, TFT_WHITE);
  tft.fillRoundRect(81, 66, 48, 28, 3, themeMode == 1 ? TFT_WHITE : TFT_DARKGREY);
  tft.setTextColor(TFT_BLACK);
  tft.drawString("WHT", 105, 78, 1);
  tft.setTextColor(getTextColor());

  // BLUE Theme
  tft.drawRoundRect(140, 65, 50, 30, 4, 0x0010);
  tft.drawRoundRect(139, 64, 52, 32, 4, 0x0010);
  tft.fillRoundRect(141, 66, 48, 28, 3, themeMode == 2 ? 0x07FF : TFT_DARKGREY);
  tft.setTextColor(0x0010);
  tft.drawString("BLU", 165, 78, 1);
  tft.setTextColor(getTextColor());

  // YELLOW Theme
  tft.drawRoundRect(200, 65, 50, 30, 4, 0xCC00);
  tft.drawRoundRect(199, 64, 52, 32, 4, 0xCC00);
  tft.fillRoundRect(201, 66, 48, 28, 3, themeMode == 3 ? 0xFFE0 : TFT_DARKGREY);
  tft.setTextColor(TFT_BLACK);
  tft.drawString("YEL", 225, 78, 1);
  tft.setTextColor(getTextColor());

  // === NOVÉ TLAČÍTKO INVERT ===
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Colours", 285, 50, 2);
  
  // INVERT tlačítko - stejný styl jako témata
  uint16_t invertBorderColor = invertColors ? TFT_GREEN : TFT_DARKGREY;
  uint16_t invertFillColor = invertColors ? TFT_GREEN : TFT_DARKGREY;
  
  tft.drawRoundRect(260, 65, 50, 30, 4, invertBorderColor);
  tft.drawRoundRect(259, 64, 52, 32, 4, invertBorderColor);
  tft.fillRoundRect(261, 66, 48, 28, 3, invertFillColor);
  tft.setTextColor(TFT_BLACK);
  tft.drawString("INV", 285, 78, 1);
  tft.setTextColor(getTextColor());

  // === SLIDER PRO JAS DISPLEJE (Zmenšený) ===
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(getTextColor());
  tft.drawString("Brightness", 10, 108, 2);
  
  int sliderX = 10;
  int sliderY = 125;
  int sliderWidth = 130; 
  int sliderHeight = 12;
  
  // Rámec slideru
  tft.drawRect(sliderX, sliderY, sliderWidth, sliderHeight, getTextColor());

  // Vyplnění podle aktuálního jasu
  int fillWidth = map(brightness, 0, 255, 0, sliderWidth - 2);
  tft.fillRect(sliderX + 1, sliderY + 1, fillWidth, sliderHeight - 2, TFT_SKYBLUE);
  
  // Procenta - posunuta hned za zmenšený slider
  int brightnessPercent = map(brightness, 0, 255, 0, 100);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(String(brightnessPercent) + "%", sliderX + sliderWidth + 5, sliderY + 1, 1);

  // === PŘEPÍNAČ ANALOG / DIGITAL (Vpravo od jasu) ===
  int swX = 200; 
  int swY = 115; 
  int swW = 110; 
  int swH = 28;

  // Barva aktivního prvku
  uint16_t activeColor = TFT_GREEN; 
  if(themeMode == 2) activeColor = blueLight;
  if(themeMode == 3) activeColor = yellowDark;

  tft.drawRect(swX, swY, swW, swH, getTextColor());

  if (!isDigitalClock) {
    // Stav: ANALOG (aktivní vlevo)
    tft.fillRect(swX + 2, swY + 2, (swW / 2) - 2, swH - 4, activeColor);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("ANA", swX + (swW / 4), swY + (swH / 2), 2);
    tft.setTextColor(getTextColor());
    tft.drawString("DIGI", swX + (3 * swW / 4), swY + (swH / 2), 2);
  } else {
    // Stav: DIGITAL (aktivní vpravo)
    tft.fillRect(swX + (swW / 2), swY + 2, (swW / 2) - 2, swH - 4, activeColor);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(getTextColor());
    tft.drawString("ANA", swX + (swW / 4), swY + (swH / 2), 2);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("DIGI", swX + (3 * swW / 4), swY + (swH / 2), 2);
  }

  // === AUTO DIM NASTAVENÍ ===
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(getTextColor());
  tft.drawString("Auto Dim", 10, 155, 2);

  // ON/OFF TLAČÍTKA
  int onX = 10;
  int onY = 175;
  int offX = 10;
  int offY = 195;
  int btnWidth = 28;
  int btnHeight = 16;
  if (autoDimEnabled) {
    tft.fillRoundRect(onX, onY, btnWidth, btnHeight, 3, TFT_GREEN);
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("ON", onX + btnWidth/2, onY + btnHeight/2, 1);
    tft.fillRoundRect(offX, offY, btnWidth, btnHeight, 3, TFT_BLUE);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("OFF", offX + btnWidth/2, offY + btnHeight/2, 1);
  } else {
    tft.fillRoundRect(onX, onY, btnWidth, btnHeight, 3, TFT_BLUE);
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("ON", onX + btnWidth/2, onY + btnHeight/2, 1);
    tft.fillRoundRect(offX, offY, btnWidth, btnHeight, 3, TFT_GREEN);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("OFF", offX + btnWidth/2, offY + btnHeight/2, 1);
  }

  // NASTAVENÍ VPRAVO OD ON/OFF
  if (autoDimEnabled) {
    tft.setTextColor(getTextColor());
    tft.setTextDatum(ML_DATUM);

    int startX = 50;
    int startY = onY + 3;
    int lineHeight = 16;
    // === START - ČAS ===
    tft.drawString("Start", startX, startY, 1);
    int startTimeX = startX + 50;
    tft.drawString(String(autoDimStart) + "h", startTimeX, startY, 1);
    int startPlusX = startTimeX + 50;
    int startPlusY = startY - 6;
    int btnW = 16;
    int btnH = 12;
    tft.drawRoundRect(startPlusX, startPlusY, btnW, btnH, 2, TFT_GREEN);
    tft.setTextColor(TFT_GREEN);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("+", startPlusX + btnW/2, startPlusY + btnH/2, 1);
    tft.setTextColor(getTextColor());
    int startMinusX = startPlusX + btnW + 10;
    int startMinusY = startPlusY;
    tft.drawRoundRect(startMinusX, startMinusY, btnW, btnH, 2, TFT_GREEN);
    tft.setTextColor(TFT_GREEN);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("-", startMinusX + btnW/2, startMinusY + btnH/2, 1);
    tft.setTextColor(getTextColor());

    // === END - ČAS ===
    int endY = startY + lineHeight;
    tft.setTextDatum(ML_DATUM);
    tft.drawString("End", startX, endY, 1);
    int endTimeX = startX + 50;
    tft.drawString(String(autoDimEnd) + "h", endTimeX, endY, 1);
    int endPlusX = endTimeX + 50;
    int endPlusY = endY - 6;
    tft.drawRoundRect(endPlusX, endPlusY, btnW, btnH, 2, TFT_GREEN);
    tft.setTextColor(TFT_GREEN);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("+", endPlusX + btnW/2, endPlusY + btnH/2, 1);
    tft.setTextColor(getTextColor());
    int endMinusX = endPlusX + btnW + 10;
    int endMinusY = endPlusY;
    tft.drawRoundRect(endMinusX, endMinusY, btnW, btnH, 2, TFT_GREEN);
    tft.setTextColor(TFT_GREEN);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("-", endMinusX + btnW/2, endMinusY + btnH/2, 1);
    tft.setTextColor(getTextColor());

    // === LEVEL - PROCENTA ===
    int levelY = endY + lineHeight;
    tft.setTextDatum(ML_DATUM);
    tft.drawString("Level", startX, levelY, 1);
    int levelTimeX = startX + 50;
    tft.drawString(String(autoDimLevel) + "%", levelTimeX, levelY, 1);
    int levelPlusX = levelTimeX + 50;
    int levelPlusY = levelY - 6;
    tft.drawRoundRect(levelPlusX, levelPlusY, btnW, btnH, 2, TFT_GREEN);
    tft.setTextColor(TFT_GREEN);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("+", levelPlusX + btnW/2, levelPlusY + btnH/2, 1);
    tft.setTextColor(getTextColor());
    int levelMinusX = levelPlusX + btnW + 10;
    int levelMinusY = levelPlusY;
    tft.drawRoundRect(levelMinusX, levelMinusY, btnW, btnH, 2, TFT_GREEN);
    tft.setTextColor(TFT_GREEN);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("-", levelMinusX + btnW/2, levelMinusY + btnH/2, 1);
    tft.setTextColor(getTextColor());
  }

  // === BACK BUTTON ===
  int backX = 252;
  int backY = 182;
  int backSize = 56;
  tft.drawRoundRect(backX, backY, backSize, backSize, 3, TFT_RED);
  tft.drawRoundRect(backX + 1, backY + 1, backSize - 2, backSize - 2, 2, TFT_RED);
  tft.setTextColor(TFT_RED);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("<", backX + backSize/2, backY + backSize/2, 4);
}

void drawInitialSetup() {
  tft.fillScreen(getBgColor());
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("WIFI SELECTION", 160, 15, 2);
  tft.setTextDatum(ML_DATUM);

  if (wifiCount == 0) {
    tft.drawString("No networks found", 15, 45, 2);
  } else {
    for (int i = wifiOffset; i < wifiOffset + 6 && i < wifiCount; i++) {
      int idx = i - wifiOffset;
      String txt = wifiSSIDs[i];
      if (txt.length() > 18) txt = txt.substring(0, 15) + "...";
      tft.drawString(txt, 15, 45 + idx * 30, 2);
      // Linka oddělující položky
      tft.drawFastHLine(10, 62 + idx * 30, 240, TFT_DARKGREY);
    }
  }

  // Vykreslení navigačních šipek
  // Šipka ZPĚT - pouze pokud už máme nějakou WiFi uloženou (nejsme v initial setupu bez dat)
  if (ssid != "") {
    drawArrowBack(265, 50, TFT_RED);
  }

  // Šipka NAHORU
  if (wifiOffset > 0) {
    drawArrowUp(265, 110, TFT_BLUE);
  }

  // Šipka DOLŮ
  if (wifiOffset + 6 < wifiCount) {
    drawArrowDown(265, 170, TFT_BLUE);
  }
}

void drawKeyboardScreen() {
  tft.fillScreen(getBgColor());
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSansBold12pt7b);

  String title = "WiFi Password";
  if (currentState == CUSTOMCITYINPUT) title = "Enter City Name";
  else if (currentState == CUSTOMCOUNTRYINPUT) title = "Enter Country Name";
  tft.drawString(title, 160, 20);

  tft.drawRect(10, 40, 300, 30, isWhiteTheme ? TFT_BLACK : TFT_WHITE);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(ML_DATUM);
  // LOGIKA ZOBRAZENÍ: Hvězdičky pouze pro WiFi a pouze pokud showPassword je false
  if (currentState == KEYBOARD && !showPassword) {
    String stars = "";
    for (int i = 0; i < passwordBuffer.length(); i++) stars += "*";
    tft.drawString(stars, 20, 55);
  } else {
    tft.drawString(passwordBuffer, 20, 55);
  }

  // PŘEPÍNAČ VIDITELNOSTI (pouze pro WiFi klávesnici)
  if (currentState == KEYBOARD) {
    tft.setTextDatum(MC_DATUM);
    tft.drawRect(250, 140, 60, 25, isWhiteTheme ? TFT_DARKGREY : TFT_WHITE);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.drawString(showPassword ? "Hide" : "Show", 280, 153);
  }

  // Klávesnice
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(MC_DATUM);
  
   const char* rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
  if (keyboardNumbers) {
    // ČÍSELNÝ MÓD: 1. řádek čísla, 2. a 3. řádek speciální znaky
    rows[0] = "1234567890";
    rows[1] = "!@#$%^&*(/";
    rows[2] = ")-_+=.,?";
  }

  for (int r = 0; r < 3; r++) {
    int len = strlen(rows[r]);
    for (int i = 0; i < len; i++) {
      int btnX = i * 29 + 2;
      int btnY = 80 + r * 30;
      tft.drawRect(btnX, btnY, 26, 26, isWhiteTheme ? TFT_DARKGREY : TFT_WHITE);
      char ch = rows[r][i];
      if (keyboardShift && !keyboardNumbers) ch = toupper(ch);
      tft.drawString(String(ch), btnX + 13, btnY + 15);
    }
  }

  // Mezerník a funkční tlačítka (zbytek zůstává stejný)
  tft.drawRect(2, 170, 316, 25, isWhiteTheme ? TFT_DARKGREY : TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Space", 160, 183);

  int bw = 64; int by = 198; int bh = 35;
  tft.setTextColor(isWhiteTheme ? TFT_BLACK : TFT_WHITE);
  tft.drawRect(0 * bw + 2, by, bw - 4, bh, isWhiteTheme ? TFT_DARKGREY : TFT_WHITE);
  tft.drawString("Shift", 0 * bw + bw / 2, by + 18);
  tft.drawRect(1 * bw + 2, by, bw - 4, bh, isWhiteTheme ? TFT_DARKGREY : TFT_WHITE);
  tft.drawString("123", 1 * bw + bw / 2, by + 18);
  tft.drawRect(2 * bw + 2, by, bw - 4, bh, isWhiteTheme ? TFT_DARKGREY : TFT_WHITE);
  tft.drawString("Del", 2 * bw + bw / 2, by + 18);
  tft.setTextColor(TFT_RED);
  tft.drawRect(3 * bw + 2, by, bw - 4, bh, TFT_RED);
  tft.drawString("Back", 3 * bw + bw / 2, by + 18);
  tft.setTextColor(TFT_GREEN);
  tft.drawRect(4 * bw + 2, by, bw - 4, bh, TFT_GREEN);
  tft.drawString("OK", 4 * bw + bw / 2, by + 18);
  tft.setTextColor(isWhiteTheme ? TFT_BLACK : TFT_WHITE);
}

void updateKeyboardText() {
  // Vymaže pouze vnitřek rámečku pro text, aby neblikal zbytek klávesnice
  tft.fillRect(11, 41, 298, 28, isWhiteTheme ? TFT_WHITE : TFT_BLACK);
  
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(isWhiteTheme ? TFT_BLACK : TFT_WHITE);
  tft.setTextDatum(ML_DATUM); 
  
  if (currentState == KEYBOARD && !showPassword) {
    String stars = "";
    for (int i = 0; i < passwordBuffer.length(); i++) stars += "*";
    tft.drawString(stars, 20, 55);
  } else {
    tft.drawString(passwordBuffer, 20, 55);
  }
}

void drawClockStatic()
{
  if (isDigitalClock) return; // V digitálním módu nic nekreslíme

  // Vykreslení minutových a hodinových index čárek
  for (int i = 0; i < 60; i++) {
    float ang = (i * 6 - 90) * DEGTORAD;
    // Úprava u menšího ciferníku zkracujeme čárky
    int r1 = (i % 5 == 0) ? (radius - 10) : (radius - 5);
    uint16_t color;
    if (i % 5 == 0) {
      color = getTextColor();
    } else {
      color = (themeMode == 3) ? 0x0010 : TFT_DARKGREY;
    }
    tft.drawLine(clockX + cos(ang) * radius, clockY + sin(ang) * radius, clockX + cos(ang) * r1, clockY + sin(ang) * r1, color);
  }

  // Vykreslení čísel 1-12
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSans9pt7b);

  for (int h = 1; h <= 12; h++) {
    float angle = (h * 30 - 90) * DEGTORAD;
    int x = clockX + cos(angle) * (radius - 22);
    int y = clockY + sin(angle) * (radius - 22);
    
    tft.drawString(String(h), x, y);
  }
}

void drawClockFace() {
  tft.fillScreen(getBgColor());
  // V digitálním módu nekreslíme kruh ciferníku
  if (!isDigitalClock) {
    tft.drawCircle(clockX, clockY, radius + 2, getTextColor());
    drawClockStatic();
  }
  forceClockRedraw = true;
}

void drawDateAndWeek(const struct tm *ti)
{
  // NOVÁ LOGIKA BAREV PRO YELLOW TÉMA
  uint16_t dateColor = getTextColor();
  if (themeMode == 3) {  // YELLOW TÉMA - Text je ČERNÝ a TMAVÝ HNĚDÝ
    dateColor = TFT_BLACK;  // Hlavní text - černý
  }
  
  tft.setFreeFont(NULL);
  tft.setTextColor(dateColor, getBgColor());
  tft.setTextDatum(MC_DATUM);
  
  // OPRAVA: Mazn pouze vpravo od cifernku x > 155, Výka od y160 do y240 (80 pixel)
  tft.fillRect(155, 160, 165, 80, getBgColor());

  char dateBuf[30];
  strftime(dateBuf, sizeof(dateBuf), "%B %d, %Y", ti);
  tft.drawString(String(dateBuf), clockX, 175, 2);

  int weekNum = 0;
  char weekBuf[20];
  strftime(weekBuf, sizeof(weekBuf), "%V", ti);
  weekNum = atoi(weekBuf);

  const char *dayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  String dayStr = String(dayNames[ti->tm_wday]);
  String weekStr = "Week " + String(weekNum) + ", " + dayStr;
  tft.drawString(weekStr, clockX, 193, 2);

  if (cityName != "") {
    // YELLOW TÉMA - město tmavě zeleně
    if (themeMode == 3) {
      tft.setTextColor(0x0220, getBgColor());  // Tmavě zelená
    } else {
      tft.setTextColor(TFT_SKYBLUE, getBgColor());
    }
    tft.drawString(cityName, clockX, 211, 2);
  }

  if (namedayValid && todayNameday != "--" && selectedCountry == "Czech Republic") {
    uint16_t namedayColor;
    if (themeMode == 3) {  // YELLOW TÉMA - Svátek tmavě zeleně jako město
      namedayColor = 0x0220;  // Tmavě zelená - stejné jako město v YELLOW
    } else {
      namedayColor = isWhiteTheme ? TFT_DARKGREEN : TFT_ORANGE;
    }
    tft.setTextColor(namedayColor, getBgColor());
    tft.drawString("Nameday: " + todayNameday, clockX, 227, 1);
  }
}

void drawDigitalClock(int h, int m, int s) {
  // Barva textu se musí lišit od pozadí
  uint16_t clockColor = getTextColor();
  uint16_t bgColor = getBgColor();

  // Pro lepší čitelnost v barevných tématech
  if (themeMode == 2) clockColor = TFT_WHITE; 
  if (themeMode == 3) clockColor = TFT_BLACK;

  // Formátování hodin
  int displayH = h;
  String suffix = "";
  
  if (is12hFormat) {
    if (displayH >= 12) { suffix = " PM"; if (displayH > 12) displayH -= 12; }
    else { suffix = " AM"; if (displayH == 0) displayH = 12; }
  }

  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", displayH, m);

  tft.setTextDatum(MC_DATUM);
  
  // Smazání předchozího času (box s barvou pozadí)
  // Používáme clockX a clockY jako střed (stejné jako analog)
  // Box: w=160, h=60
  
  // Pokud je aktivní gradient (theme 2 a 3), mazání rectem udělá "díru".
  // Nejlepší řešení pro text na gradientu bez flicker free knihovny je
  // nastavit pozadí textu na barvu, která tam cca je, nebo jen přepisovat.
  // Zde použijeme barvu pozadí (pro flat themes ok, pro gradient to bude vidět, ale je to funkční)
  tft.setTextColor(clockColor, bgColor); 
  
  // Velký čas (použijeme font 7 pokud je, jinak 6. Font 7 je 7-segment)
  // V CYD knihovnách bývá font 7 (7-seg)
  tft.drawString(timeStr, clockX, clockY, 7); 

  // Sekundy a suffix pod tím
  char secStr[10];
  if (is12hFormat) {
     sprintf(secStr, ":%02d%s", s, suffix.c_str());
  } else {
     sprintf(secStr, ":%02d", s);
  }
  
  tft.setTextColor(getSecHandColor(), bgColor);
  tft.drawString(secStr, clockX, clockY + 45, 4); // Menší font pod časem
}

void updateHands(int h, int m, int s) {
  // Pokud je zapnutý digitální režim, kreslíme digitálně
  if (isDigitalClock) {
    drawDigitalClock(h, m, s);
    return;
  }

  // --- PŮVODNÍ ANALOGOVÝ KÓD ---
  uint16_t bgColor = getBgColor();
  uint16_t mainHandColor = getTextColor();
  uint16_t secColor = getSecHandColor();

  // SMAZÁNÍ STARÝCH RUČEK (pokud existují)
  if (lastSec != -1) {
    float hO = (lastHour % 12) + (lastMin / 60.0f);
    hO = hO * 30 - 90;  // Převod do stupňů
    
    float mO = lastMin * 6 - 90;
    float sO = lastSec * 6 - 90;

    // Kreslení starých ruček barvou pozadí (smazání)
    tft.drawLine(clockX, clockY,
                 clockX + cos(hO * DEGTORAD) * (radius - 35),
                 clockY + sin(hO * DEGTORAD) * (radius - 35),
                 bgColor);
    tft.drawLine(clockX, clockY,
                 clockX + cos(mO * DEGTORAD) * (radius - 20),
                 clockY + sin(mO * DEGTORAD) * (radius - 20),
                 bgColor);
    tft.drawLine(clockX, clockY,
                 clockX + cos(sO * DEGTORAD) * (radius - 14),
                 clockY + sin(sO * DEGTORAD) * (radius - 14),
                 bgColor);
    // DŮLEŽITÉ: Znovu nakreslit indexy!
    drawClockStatic();
  }

  // KRESLENÍ NOVÝCH RUČEK
  float hA = (h % 12) + (m / 60.0f);
  hA = hA * 30 - 90;
  
  float mA = m * 6 - 90;
  float sA = s * 6 - 90;

  tft.drawLine(clockX, clockY,
               clockX + cos(hA * DEGTORAD) * (radius - 35),
               clockY + sin(hA * DEGTORAD) * (radius - 35),
               mainHandColor);
  tft.drawLine(clockX, clockY,
               clockX + cos(mA * DEGTORAD) * (radius - 20),
               clockY + sin(mA * DEGTORAD) * (radius - 20),
               mainHandColor);
  tft.drawLine(clockX, clockY,
               clockX + cos(sA * DEGTORAD) * (radius - 14),
               clockY + sin(sA * DEGTORAD) * (radius - 14),
               secColor);
  // Středový kroužek
  tft.fillCircle(clockX, clockY, 3, TFT_LIGHTGREY);
}

String ianaToPostfixTZ(String iana) {
  String posix = TzDbLookup::getPosix(iana.c_str());
  if (posix) {
    Serial.printf("POSIX string for %s: %s\n", iana, posix);
    return (posix);
  } else {
    Serial.println("Time zone not found.");
    return ("UTC0");
  }
}
// ============================================
// OPRAVA 3: Ukládání a načítání souřadnic
// ============================================
void applyLocation() {
  posixTZ = ianaToPostfixTZ(selectedTimezone);
  Serial.println("[TZ] Applying POSIX TZ: " + posixTZ + " for IANA: " + selectedTimezone);
  configTime(0, 0, ntpServer);
  setenv("TZ", posixTZ.c_str(), 1);
  tzset();
  
  // RESET SOUŘADNIC - při vyberu ze seznamu musíme nechat fetchWeatherData() najít nové souřadnice
  lat = 0.0;
  lon = 0.0;
  
  // Uložení do preferencí
  prefs.begin("sys", false);
  prefs.putString("city", selectedCity);
  prefs.putString("country", selectedCountry);
  prefs.putString("timezone", selectedTimezone);
  prefs.putString("posixTZ", posixTZ);
  prefs.putInt("gmt", gmtOffset_sec);
  prefs.putInt("dst", daylightOffset_sec);
  
  // UKLÁDÁME I SOUŘADNICE (nyní 0.0, aby se při dalším update počasí našly správné)
  prefs.putFloat("lat", lat);
  prefs.putFloat("lon", lon);
  
  prefs.end();
  cityName = selectedCity;
  
  lastDay = -1; // Vynutí update data
  lastWeatherUpdate = 0; // Vynutí update počasí
  lastNamedayDay = -1; // OPRAVA: Vynutí update svátku při změně lokace
  handleNamedayUpdate(); // OPRAVA: Aktualizace svátku ihned po změně lokace
}

void loadSavedLocation() {
  prefs.begin("sys", false);
  regionAutoMode = prefs.getBool("regionAuto", true);
  String savedCountry = prefs.getString("country", "");
  String savedCity = prefs.getString("city", "");
  selectedTimezone = prefs.getString("timezone", "");
  posixTZ = prefs.getString("posixTZ", "CET-1CEST,M3.5.0,M10.5.0/3");
  
  // OPRAVA: Sjednocení názvů klíčů s funkcí applyLocation ("gmt" místo "gmtOffset")
  gmtOffset_sec = prefs.getInt("gmt", 3600);
  daylightOffset_sec = prefs.getInt("dst", 3600);
  
  // NAČTEME ULOŽENÉ SOUŘADNICE
  lat = prefs.getFloat("lat", 0.0);
  lon = prefs.getFloat("lon", 0.0);
  
  prefs.end();
  
  if (savedCity != "") {
    cityName = savedCity;
    selectedCity = savedCity;
    selectedCountry = savedCountry;
    // Pokud posixTZ nebyl uložen (stará verze firmware), odvoď ho z IANA timezone
    if (posixTZ == "" || posixTZ == "CET-1CEST,M3.5.0,M10.5.0/3") {
      if (selectedTimezone != "") {
        posixTZ = ianaToPostfixTZ(selectedTimezone);
      }
    }
    Serial.println("[LOAD] Applying POSIX TZ: " + posixTZ);
    configTime(0, 0, ntpServer);
    setenv("TZ", posixTZ.c_str(), 1);
    tzset();
    Serial.println("[LOAD] Location loaded: " + cityName + " (" + String(lat) + "," + String(lon) + ")");
  }
}

// ================= WEATHER FUNCTIONS =================
String getWeatherDesc(int code) {
  if (code == 0) return "Clear";
  if (code <= 3) return "Cloudy";
  if (code <= 48) return "Fog";
  if (code <= 67) return "Rain";
  if (code <= 77) return "Snow";
  if (code <= 82) return "Showers";
  if (code <= 99) return "Storm";
  return "Unknown";
}

String getWindDir(int deg) {
  if (deg >= 337 || deg < 22) return "N";
  if (deg < 67) return "NE";
  if (deg < 112) return "E";
  if (deg < 157) return "SE";
  if (deg < 202) return "S";
  if (deg < 247) return "SW";
  if (deg < 292) return "W";
  return "NW";
}

// ============================================
// OPRAVA 4: Použití přesných souřadnic pro počasí
// ============================================
void fetchWeatherData() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  
  // KROK 1: Získání souřadnic
  // Pokud už máme souřadnice z Custom Lookup (nejsou 0.0), POUŽIJEME JE a nehledáme znovu
  if (lat != 0.0 && lon != 0.0) {
     Serial.println("[WEATHER] Using saved coordinates: " + String(lat, 4) + ", " + String(lon, 4));
  } 
  else {
    // Pokud nemáme souřadnice (např. vybráno ze seznamu embedded měst), musíme je najít
    // Ale hledáme chytřeji - stahneme vice vysledku a filtrujeme zemi
    Serial.println("[WEATHER] Searching coordinates for: " + weatherCity + ", Country: " + selectedCountry);
    
    String searchName = weatherCity;
    searchName.replace(" ", "+");
    String geoUrl = "https://geocoding-api.open-meteo.com/v1/search?name=" + searchName + "&count=5&language=en&format=json";

    http.setTimeout(3000);
    http.begin(geoUrl);
    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(4096);
      deserializeJson(doc, payload);
      
      bool found = false;
      if (doc["results"].size() > 0) {
        // Projdeme výsledky a zkusíme najít shodu země
        for (JsonVariant result : doc["results"].as<JsonArray>()) {
           String resCountry = result["country"].as<String>();
           String resCode = result["country_code"].as<String>();
           
           // Porovnáme zemi (fuzzy match)
           if (resCountry.indexOf(selectedCountry) >= 0 || selectedCountry.indexOf(resCountry) >= 0 || 
               resCode.equalsIgnoreCase(selectedCountry)) {
               
               lat = result["latitude"];
               lon = result["longitude"];
               Serial.println("[WEATHER] Match found: " + result["name"].as<String>() + ", " + resCountry);
               found = true;
               break;
           }
        }
        
        // Pokud jsme nenašli shodu země, vezmeme první výsledek (fallback)
        if (!found) {
           lat = doc["results"][0]["latitude"];
           lon = doc["results"][0]["longitude"];
           Serial.println("[WEATHER] Country match failed, taking first result: " + doc["results"][0]["country"].as<String>());
        }
        
        // Uložíme nové souřadnice, abychom příště nemuseli hledat
        prefs.begin("sys", false);
        prefs.putFloat("lat", lat);
        prefs.putFloat("lon", lon);
        prefs.end();
      }
    }
    http.end();
  }

  // KROK 2: Stažení počasí pro dané souřadnice
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);

  if (timeinfo) {
    const char* dayAbbr[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    int tomorrowWday = (timeinfo->tm_wday + 1) % 7;
    forecastDay1Name = dayAbbr[tomorrowWday];
    int afterTomorrowWday = (timeinfo->tm_wday + 2) % 7;
    forecastDay2Name = dayAbbr[afterTomorrowWday];
  }

  String weatherUrl = "https://api.open-meteo.com/v1/forecast?latitude=" + String(lat, 4) + "&longitude=" + String(lon, 4) +
                    "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,wind_direction_10m,pressure_msl&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset&timezone=auto";
  
  http.setTimeout(5000);
  http.begin(weatherUrl);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(8192); // Větší buffer pro weather data
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      currentTemp = doc["current"]["temperature_2m"];
      currentHumidity = doc["current"]["relative_humidity_2m"];
      weatherCode = doc["current"]["weather_code"];
      currentWindSpeed = doc["current"]["wind_speed_10m"];
      currentWindDirection = doc["current"]["wind_direction_10m"];

      forecast[0].code = doc["daily"]["weather_code"][1];
      forecast[0].tempMax = doc["daily"]["temperature_2m_max"][1];
      forecast[0].tempMin = doc["daily"]["temperature_2m_min"][1];
      forecast[1].code = doc["daily"]["weather_code"][2];
      forecast[1].tempMax = doc["daily"]["temperature_2m_max"][2];
      forecast[1].tempMin = doc["daily"]["temperature_2m_min"][2];

      // Sunrise/Sunset processing
      if (doc["daily"].containsKey("sunrise") && doc["daily"]["sunrise"].size() > 0) {
        String sunriseRaw = doc["daily"]["sunrise"][0].as<String>();
        int tPos = sunriseRaw.indexOf('T');
        if (tPos > 0) sunriseTime = sunriseRaw.substring(tPos + 1, tPos + 6);
      }
      if (doc["daily"].containsKey("sunset") && doc["daily"]["sunset"].size() > 0) {
        String sunsetRaw = doc["daily"]["sunset"][0].as<String>();
        int tPos = sunsetRaw.indexOf('T');
        if (tPos > 0) sunsetTime = sunsetRaw.substring(tPos + 1, tPos + 6);
      }
      
      // Pressure processing
      if (doc["current"].containsKey("pressure_msl")) {
        currentPressure = doc["current"]["pressure_msl"].as<int>();
      } else {
        currentPressure = 1013;
      }

      initialWeatherFetched = true;
      Serial.println("[WEATHER] Data fetched successfully");
    } else {
      Serial.println("[WEATHER] JSON error: " + String(error.c_str()));
    }
  } else {
    Serial.println("[WEATHER] HTTP Error: " + String(httpCode));
  }
  http.end();
}

// ========== GRADIENTNÍ VÝPLŇ ==========
void fillGradientVertical(int x, int y, int w, int h, uint16_t colorTop, uint16_t colorBottom) {
  // Vykreslí gradient vertikálně (od horní barvy k dolní)
  for (int i = 0; i < h; i++) {
    // Interpolace mezi barvami (R,G,B)
    uint8_t r1 = (colorTop >> 11) & 0x1F;
    uint8_t g1 = (colorTop >> 5) & 0x3F;
    uint8_t b1 = colorTop & 0x1F;
    
    uint8_t r2 = (colorBottom >> 11) & 0x1F;
    uint8_t g2 = (colorBottom >> 5) & 0x3F;
    uint8_t b2 = colorBottom & 0x1F;
    
    float ratio = (float)i / h;
    uint8_t r = r1 + (r2 - r1) * ratio;
    uint8_t g = g1 + (g2 - g1) * ratio;
    uint8_t b = b1 + (b2 - b1) * ratio;
    
    uint16_t color = (r << 11) | (g << 5) | b;
    tft.drawFastHLine(x, y + i, w, color);
  }
}

void drawWeatherSection() {
  uint16_t bg = getBgColor();
  uint16_t txt = getTextColor();
  uint16_t txtContrast = TFT_SKYBLUE;

  if (themeMode == 2) {                           // MODRÝ - text žlutý
    txtContrast = TFT_YELLOW;
  } else if (themeMode == 3) {                     // ŽLUTÝ - text černý
    txtContrast = TFT_BLACK;
  }

  // Vymazání sekce - POUZE počasí, ne hodiny
  tft.fillRect(0, 0, 155, 95, bg);                 // Horní část
  tft.fillRect(0, 105, 155, 100, bg);              // Středová část
  tft.fillRect(0, 206, 155, 34, bg);               // Dolní část

  if (!initialWeatherFetched) {
    tft.setTextColor(txt, bg);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Loading...", 75, 120);
    return;
  }

  // --- 1. SEKCE: Aktuální teplota s ikonou ---
  drawWeatherIconVector(weatherCode, 5, 15);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(txtContrast, bg);
  tft.setFreeFont(&FreeSansBold18pt7b);

  float dispTemp = weatherUnitF ? (currentTemp * 9.0 / 5.0 + 32) : currentTemp;
  String unit = weatherUnitF ? "F" : "C";
  String tempStr = String((int)dispTemp);
  tft.drawString(tempStr, 45, 15);

  int tempWidth = tft.textWidth(tempStr);
  drawDegreeCircle(45 + tempWidth + 5, 20, 3, txtContrast);  // ✅ r=3 - větší kroužek
  tft.drawString(unit, 45 + tempWidth + 12, 15);

  // Popis počasí: Clear, Cloudy, Rainy, atd.
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(txt, bg);
  tft.drawString(getWeatherDesc(weatherCode), 45, 48);

  // Vlhkost a tlak
  tft.setFreeFont(NULL);
  tft.setTextColor(txt, bg);
  tft.setCursor(5, 75);
 tft.printf("Hum: %d%% Press: %d hPa", currentHumidity, currentPressure);

  // Vítr na dalším řádku
  tft.setCursor(5, 88);
  if (weatherUnitMph) {
  float windMph = currentWindSpeed * 0.621371;
  tft.printf("Wind: %.1f mph %s", windMph, getWindDir(currentWindDirection).c_str());
} else {
  tft.printf("Wind: %.1f km/h %s", currentWindSpeed, getWindDir(currentWindDirection).c_str());
}

  // --- SUNRISE/SUNSET ---
  tft.drawBitmap(5, 98, icon_sunrise, 16, 16, TFT_ORANGE);
  tft.setCursor(24, 102);
  tft.setTextColor(TFT_ORANGE, bg);
  tft.print(sunriseTime);

  tft.drawBitmap(85, 98, icon_sunset, 16, 16, TFT_RED);
  tft.setCursor(104, 102);
  tft.setTextColor(TFT_RED, bg);
  tft.print(sunsetTime);

  // ODDLOVACÍ LINKA
  tft.setTextColor(txt, bg);
  tft.drawFastHLine(5, 120, 145, TFT_DARKGREY);

  // --- 2. SEKCE: Předpověď s DNEM ---
  tft.setTextColor(txt, bg);
  tft.setFreeFont(NULL);
  tft.drawString("Forecast:", 5, 128);

  // ============================================
  // PRVNÍ DEN - VYKRESLENÍ S KROUZKEM JAKO °
  // ============================================
  drawWeatherIconVectorSmall(forecast[0].code, 8, 138);
  tft.setTextDatum(ML_DATUM);
  tft.setFreeFont(NULL); 
  tft.setTextColor(txt, bg);
  int day1x = 70;
  int day1y = 138;
  tft.drawString(forecastDay1Name, day1x, day1y);

// ✅ OPRAVA: Vykreslení bez textového °, ale s krouzkem funkcí
  tft.setTextColor(txtContrast, bg);
  float fMin1 = weatherUnitF ? (forecast[0].tempMin * 9.0 / 5.0 + 32) : forecast[0].tempMin;
  float fMax1 = weatherUnitF ? (forecast[0].tempMax * 9.0 / 5.0 + 32) : forecast[0].tempMax;
  String tempMin1 = String((int)fMin1);
  String tempMax1 = String((int)fMax1);
  
  // Vykreslení teploty s LOMÍTKEM místo pomlčky
  String tempRangeOnly1 = tempMin1 + "/" + tempMax1;
  tft.drawString(tempRangeOnly1, day1x, day1y + 13);
  
  // Výpočet pozice pro kroulek (stupень symbol)
  int tempWidth1 = tft.textWidth(tempRangeOnly1);
  int degreeX1 = day1x + tempWidth1 + 3;
  int degreeY1 = day1y + 8;
  
  // Vykreslení malého krouzku jako stupně (r=1)
  drawDegreeCircle(degreeX1, degreeY1, 1, txtContrast);
  
  // Vykreslení jednotky (C/F) za kruhem
  tft.drawString(unit, degreeX1 + 4, day1y + 13);

  // ============================================
  // DRUHÝ DEN - VYKRESLENÍ S KROUZKEM JAKO °
  // ============================================
  drawWeatherIconVectorSmall(forecast[1].code, 8, 170);
  tft.setTextColor(txt, bg);
  int day2x = 70;
  int day2y = 170;
  tft.drawString(forecastDay2Name, day2x, day2y);

// ✅ OPRAVA: Vykreslení bez textového °, ale s krouzkem funkcí
  tft.setTextColor(txtContrast, bg);
  float fMin2 = weatherUnitF ? (forecast[1].tempMin * 9.0 / 5.0 + 32) : forecast[1].tempMin;
  float fMax2 = weatherUnitF ? (forecast[1].tempMax * 9.0 / 5.0 + 32) : forecast[1].tempMax;
  String tempMin2 = String((int)fMin2);
  String tempMax2 = String((int)fMax2);
  
  // Vykreslení teploty s LOMÍTKEM místo pomlčky
  String tempRangeOnly2 = tempMin2 + "/" + tempMax2;
  tft.drawString(tempRangeOnly2, day2x, day2y + 13);
  
  // Výpočet pozice pro kroulek (stupень symbol)
  int tempWidth2 = tft.textWidth(tempRangeOnly2);
  int degreeX2 = day2x + tempWidth2 + 3;
  int degreeY2 = day2y + 8;
  
  // Vykreslení malého krouzku jako stupně (r=1)
  drawDegreeCircle(degreeX2, degreeY2, 1, txtContrast);

  // Vykreslení jednotky (C/F) za kruhem
  tft.drawString(unit, degreeX2 + 4, day2y + 13);
  
// ODDĚLUJÍCÍ LINKA
  tft.setTextColor(txt, bg);
  tft.drawFastHLine(5, 200, 145, TFT_DARKGREY);

// --- 3. SEKCE: Měsíční fáze s SPRÁVNOU GRAFIKOU ---
  struct tm ti;
  if (getLocalTime(&ti)) {
    int phase = getMoonPhase(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    moonPhaseVal = phase;  // Aktualizace globální proměnné
    
    tft.setTextColor(txt, bg);
    tft.setFreeFont(NULL);
    tft.drawString("Moon Phase:", 5, 210);

    String phaseNames[] = {"New Moon", "Waxing Crescent", "First Quarter", "Waxing Gibbous", "Full Moon", "Waning Gibbous", "Last Quarter", "Waning Crescent"};
    if (phase >= 0 && phase <= 7) {
      tft.drawString(phaseNames[phase], 5, 222);
    }

    // Volání správné funkce pro kreslení měsíční fáze
    int mx = 120;       // X pozice měsíce
    int my = 222;       // Y pozice měsíce
    int r = 13;         // Radius měsíce
    
    drawMoonPhaseIcon(mx, my, r, phase, txt, bg);
    
    // Debug output
    Serial.print("[MOON] Phase: ");
    Serial.print(phase);
    Serial.print(" | Date: ");
    Serial.print(ti.tm_year + 1900);
    Serial.print("-");
    Serial.print(ti.tm_mon + 1);
    Serial.print("-");
    Serial.println(ti.tm_mday);
  }
}


// ============================================
// POMOCNÁ FUNKCE PRO KRESLENÍ STUPNĚ
// ============================================
void drawDegreeCircle(int x, int y, int r, uint16_t color) {
  tft.drawCircle(x, y, r, color);
  if (r > 1) {
    tft.drawCircle(x, y, r - 1, color);
  }
}


// ================= AUTODIM HELPER FUNKCE =================
void applyAutoDim() {
  if (!autoDimEnabled) return;

  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  int currentHour = timeinfo->tm_hour;

  bool shouldDim = false;
  if (autoDimStart < autoDimEnd) {
    // Normální interval (např. 22-6 není normální, to je přes půlnoc)
    shouldDim = (currentHour >= autoDimStart && currentHour < autoDimEnd);
  } else {
    // Interval přes půlnoc (např. start=22, end=6)
    shouldDim = (currentHour >= autoDimStart || currentHour < autoDimEnd);
  }

  if (shouldDim && !isDimmed) {
    // Přejít na nižší jas
    brightness = map(autoDimLevel, 0, 100, 0, 255);
    isDimmed = true;
    analogWrite(LCD_BL_PIN, brightness);
    Serial.println("[AUTODIM] Dim ON - level: " + String(brightness));
  } else if (!shouldDim && isDimmed) {
    // Vrátit na full jas
    brightness = 255;
    isDimmed = false;
    analogWrite(LCD_BL_PIN, brightness);
    Serial.println("[AUTODIM] Dim OFF - brightness: 255");
  }
}


// ================= SUNRISE/SUNSET IKONY =================
void drawSunriseIcon(int x, int y, uint16_t color) {
  // Minimalistická ikona východu (slunce nad čárou s vlnkou)
  // Slunce
  tft.fillCircle(x + 8, y + 2, 4, color);
  // Paprsky
  tft.drawLine(x + 8, y - 3, x + 8, y - 5, color);     // vrch
  tft.drawLine(x + 8, y + 7, x + 8, y + 9, color);     // spod
  tft.drawLine(x + 3, y + 2, x + 1, y + 2, color);     // vlevo
  tft.drawLine(x + 13, y + 2, x + 15, y + 2, color);   // vpravo
  tft.drawLine(x + 5, y - 1, x + 3, y - 3, color);     // levý horní
  tft.drawLine(x + 11, y - 1, x + 13, y - 3, color);   // pravý horní

  // Vlnka pod sluncem
  tft.drawLine(x + 2, y + 10, x + 6, y + 10, color);
  tft.drawLine(x + 10, y + 10, x + 14, y + 10, color);
  tft.drawLine(x + 4, y + 11, x + 5, y + 12, color);
  tft.drawLine(x + 11, y + 11, x + 12, y + 12, color);
  tft.drawLine(x + 6, y + 11, x + 10, y + 11, color);
}

void drawSunsetIcon(int x, int y, uint16_t color) {
  // Minimalistická ikona západu (slunce pod čárou s vlnkou)
  // Vlnka nad sluncem
  tft.drawLine(x + 2, y, x + 6, y, color);
  tft.drawLine(x + 10, y, x + 14, y, color);
  tft.drawLine(x + 4, y - 1, x + 5, y - 2, color);
  tft.drawLine(x + 11, y - 1, x + 12, y - 2, color);
  tft.drawLine(x + 6, y - 1, x + 10, y - 1, color);

  // Slunce
  tft.fillCircle(x + 8, y + 10, 4, color);
  // Paprsky
  tft.drawLine(x + 8, y + 5, x + 8, y + 3, color);     // vrch
  tft.drawLine(x + 8, y + 17, x + 8, y + 19, color);   // spod
  tft.drawLine(x + 3, y + 10, x + 1, y + 10, color);   // vlevo
  tft.drawLine(x + 13, y + 10, x + 15, y + 10, color); // vpravo
  tft.drawLine(x + 5, y + 9, x + 3, y + 7, color);     // levý horní
  tft.drawLine(x + 11, y + 9, x + 13, y + 7, color);   // pravý horní
}

// ================= OTA UPDATE FUNCTIONS =================

// Porovnání verzí (vrací true pokud newVer > currentVer)
bool isNewerVersion(String currentVer, String newVer) {
  // Odstraníme "v" prefix pokud existuje
  currentVer.replace("v", "");
  newVer.replace("v", "");
  
  int currMajor = 0, currMinor = 0, currPatch = 0;
  int newMajor = 0, newMinor = 0, newPatch = 0;
  
  // Parse current version (podporuje formát X.Y.Z)
  int firstDot = currentVer.indexOf('.');
  if (firstDot > 0) {
    currMajor = currentVer.substring(0, firstDot).toInt();
    int secondDot = currentVer.indexOf('.', firstDot + 1);
    if (secondDot > 0) {
      currMinor = currentVer.substring(firstDot + 1, secondDot).toInt();
      currPatch = currentVer.substring(secondDot + 1).toInt();
    } else {
      currMinor = currentVer.substring(firstDot + 1).toInt();
    }
  }
  
  // Parse new version (podporuje formát X.Y.Z)
  firstDot = newVer.indexOf('.');
  if (firstDot > 0) {
    newMajor = newVer.substring(0, firstDot).toInt();
    int secondDot = newVer.indexOf('.', firstDot + 1);
    if (secondDot > 0) {
      newMinor = newVer.substring(firstDot + 1, secondDot).toInt();
      newPatch = newVer.substring(secondDot + 1).toInt();
    } else {
      newMinor = newVer.substring(firstDot + 1).toInt();
    }
  }
  
  Serial.print("[OTA] Comparing versions: ");
  Serial.print(currMajor); Serial.print("."); Serial.print(currMinor); Serial.print("."); Serial.print(currPatch);
  Serial.print(" vs ");
  Serial.print(newMajor); Serial.print("."); Serial.print(newMinor); Serial.print("."); Serial.println(newPatch);
  
  // Porovnání verzí
  if (newMajor > currMajor) return true;
  if (newMajor == currMajor && newMinor > currMinor) return true;
  if (newMajor == currMajor && newMinor == currMinor && newPatch > currPatch) return true;
  return false;
}

// Kontrola dostupné verze na GitHubu
void checkForUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[OTA] WiFi not connected");
    return;
  }
  
  Serial.println("[OTA] Checking for updates...");
  HTTPClient http;
  
  // OPRAVA: Povolit redirecty i pro kontrolu verze (pro jistotu)
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  http.begin(VERSION_CHECK_URL);
  http.setTimeout(10000);
  // 10s timeout
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    Serial.println("[OTA] Response: " + payload);
    
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      availableVersion = doc["version"].as<String>();
      downloadURL = doc["download_url"].as<String>();  // NOVÉ: Načtení download URL
      
      Serial.print("[OTA] Current: ");
      Serial.print(FIRMWARE_VERSION);
      Serial.print(" | Available: ");
      Serial.println(availableVersion);
      
      updateAvailable = isNewerVersion(String(FIRMWARE_VERSION), availableVersion);
      
      if (updateAvailable) {
        Serial.println("[OTA] ✓ New version available!");
        Serial.println("[OTA] Download URL: " + downloadURL);
      } else {
        Serial.println("[OTA] Already up to date");
      }
    } else {
      Serial.println("[OTA] JSON parse error");
    }
  } else {
    Serial.print("[OTA] HTTP error: ");
    Serial.println(httpCode);
  }
  
  http.end();
  lastVersionCheck = millis();
}

// Stažení a instalace firmware
void performOTAUpdate() {
  if (!updateAvailable) {
    Serial.println("[OTA] No update available");
    return;
  }
  
  isUpdating = true;
  updateProgress = 0;
  updateStatus = "Connecting...";
  
  // Zobrazíme progress screen
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("FIRMWARE UPDATE", 160, 30, 2);
  
  // Kontrola, zda máme download URL
  if (downloadURL == "") {
    Serial.println("[OTA] ✗ No download URL available!");
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.drawString("ERROR!", 160, 80, 2);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("No download URL", 160, 110, 1);
    delay(3000);
    isUpdating = false;
    currentState = FIRMWARE_SETTINGS;
    drawFirmwareScreen();
    return;
  }
  
  String firmwareURL = downloadURL;  // Používáme přesnou URL z version.json
  Serial.println("[OTA] Downloading from: " + firmwareURL);
  Serial.println("[OTA] Installing version: " + availableVersion);
  
  HTTPClient http;
  
  // OPRAVA: Povolit sledování přesměrování (GitHub vrací 302 pro download linky)
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  http.begin(firmwareURL);
  http.setTimeout(30000);  // 30s timeout
  int httpCode = http.GET();
  
  // Pokud dojde k přesměrování, httpCode bude nyní 200 (z finální URL)
  if (httpCode == 200) {
    int contentLength = http.getSize();
    bool canBegin = Update.begin(contentLength);
    
    if (canBegin) {
      WiFiClient * client = http.getStreamPtr();
      
      size_t written = 0;
      uint8_t buff[128];
      int lastProgress = -1;
      
      // ========== FÁZE 1: DOWNLOADING ==========
      while (http.connected() && (written < contentLength)) {
        size_t available = client->available();
        if (available) {
          int bytesRead = client->readBytes(buff, min(available, sizeof(buff)));
          written += Update.write(buff, bytesRead);
          
          updateProgress = (written * 100) / contentLength;
          
          // Aktualizace pouze pokud se progress změnil
          if (updateProgress != lastProgress) {
            lastProgress = updateProgress;
            
            // Vymazání předchozího textu
            tft.fillRect(0, 60, 320, 130, TFT_BLACK);
            
            // Text "Downloading"
            tft.setTextColor(TFT_CYAN);
            tft.drawString("Downloading...", 160, 70, 2);
            
            // Progress bar - downloading
            tft.drawRoundRect(40, 100, 240, 25, 4, TFT_DARKGREY);
            tft.fillRoundRect(42, 102, (updateProgress * 236) / 100, 21, 3, TFT_CYAN);
            
            // Procenta
            tft.setTextColor(TFT_WHITE);
            tft.drawString(String(updateProgress) + "%", 160, 112, 2);
            
            // Velikost stažených dat
            tft.setTextColor(TFT_LIGHTGREY);
            String sizeStr = String(written / 1024) + " / " + String(contentLength / 1024) + " KB";
            tft.drawString(sizeStr, 160, 140, 1);
            
            if (updateProgress % 10 == 0) {
              Serial.print("[OTA] Downloading: ");
              Serial.print(updateProgress);
              Serial.println("%");
            }
          }
        }
        delay(1);
      }
      
      // ========== FÁZE 2: INSTALLING ==========
      tft.fillRect(0, 60, 320, 130, TFT_BLACK);
      tft.setTextColor(TFT_ORANGE);
      tft.drawString("Installing...", 160, 70, 2);
      
      // Progress bar - installing (animace)
      for (int i = 0; i <= 100; i += 5) {
        tft.drawRoundRect(40, 100, 240, 25, 4, TFT_DARKGREY);
        tft.fillRoundRect(42, 102, (i * 236) / 100, 21, 3, TFT_ORANGE);
        tft.setTextColor(TFT_WHITE);
        tft.drawString(String(i) + "%", 160, 112, 2);
        delay(50);
      }
      
      Serial.println("[OTA] Finalizing update...");
      
      if (Update.end(true)) {
        updateStatus = "Update successful!";
        Serial.println("[OTA] ✓ Update successful!");
        
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_GREEN);
        tft.drawString("UPDATE SUCCESS!", 160, 100, 2);
        tft.setTextColor(TFT_WHITE);
        tft.drawString("Rebooting...", 160, 130, 1);
        delay(2000);
        ESP.restart();
        
      } else {
        // ========== SELHÁNÍ - ROLLBACK ==========
        updateStatus = "Update failed!";
        Serial.println("[OTA] ✗ Update failed!");
        Serial.println(Update.errorString());
        
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_RED);
        tft.drawString("UPDATE FAILED!", 160, 80, 2);
        
        tft.setTextColor(TFT_ORANGE);
        tft.drawString("Rolling back to", 160, 110, 1);
        tft.drawString("previous version...", 160, 125, 1);
        
        // Odpočítávání 10 sekund
        for (int i = 10; i > 0; i--) {
          tft.fillRect(140, 150, 40, 20, TFT_BLACK);
          tft.setTextColor(TFT_WHITE);
          tft.drawString(String(i), 160, 160, 2);
          delay(1000);
        }
        
        // Návrat do Firmware menu
        isUpdating = false;
        currentState = FIRMWARE_SETTINGS;
        drawFirmwareScreen();
        return;
      }
      
    } else {
      // ========== NEDOSTATEK MÍSTA ==========
      updateStatus = "Not enough space!";
      Serial.println("[OTA] ✗ Not enough space!");
      
      tft.fillScreen(TFT_BLACK);
      tft.setTextColor(TFT_RED);
      tft.drawString("ERROR!", 160, 80, 2);
      tft.setTextColor(TFT_WHITE);
      tft.drawString("Not enough storage", 160, 110, 1);
      tft.drawString("space for update", 160, 125, 1);
      
      for (int i = 10; i > 0; i--) {
        tft.fillRect(140, 150, 40, 20, TFT_BLACK);
        tft.drawString(String(i), 160, 160, 2);
        delay(1000);
      }
      
      isUpdating = false;
      currentState = FIRMWARE_SETTINGS;
      drawFirmwareScreen();
      http.end();
      return;
    }
    
  } else {
    // ========== CHYBA STAHOVÁNÍ ==========
    updateStatus = "Download failed!";
    Serial.print("[OTA] ✗ HTTP error: ");
    Serial.println(httpCode);
    
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.drawString("DOWNLOAD FAILED!", 160, 80, 2);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("HTTP Error: " + String(httpCode), 160, 110, 1);
    tft.drawString("Check your connection", 160, 125, 1);
    
    for (int i = 10; i > 0; i--) {
      tft.fillRect(140, 150, 40, 20, TFT_BLACK);
      tft.drawString(String(i), 160, 160, 2);
      delay(1000);
    }
    
    isUpdating = false;
    currentState = FIRMWARE_SETTINGS;
    drawFirmwareScreen();
    http.end();
    return;
  }
  
  http.end();
  isUpdating = false;
}

void setup() {
  Serial.begin(115200);
  
  delay(500);
  Serial.println("\n\n[SETUP] === CYD Starting ===");
  Serial.println("[SETUP] Version: " + String(FIRMWARE_VERSION));
  
  // ===== PREFERENCES INITIALIZATION (Načítání nastavení) =====
  // Musíme načíst preference PŘED inicializací TFT, abychom znali barvu pozadí
  prefs.begin("sys", false);
  ssid = prefs.getString("ssid", "");
  password = prefs.getString("pass", "");
  isDigitalClock = prefs.getBool("digiClock", false);
  is12hFormat = prefs.getBool("12hFmt", false);
  
  // OPRAVA: Načtení uloženého tématu
  themeMode = prefs.getInt("themeMode", 0);
  isWhiteTheme = prefs.getBool("theme", false);
  invertColors = prefs.getBool("invertColors", false);

  // Načtení OTA nastavení
otaInstallMode = prefs.getInt("otaMode", 1);  // Default: By user
Serial.print("[OTA] Install mode: ");
Serial.println(otaInstallMode);
  
   // OPRAVA: Načtení nastavení jasu a Auto Dim
  brightness = prefs.getInt("bright", 255); // Načteme i uložený jas
  autoDimEnabled = prefs.getBool("autoDimEnabled", false);
  autoDimStart = prefs.getInt("autoDimStart", 22);
  autoDimEnd = prefs.getInt("autoDimEnd", 6);
  autoDimLevel = prefs.getInt("autoDimLevel", 20);
  
  // OPRAVA: Načtení nastavení jednotek teploty (°C / °F)
  weatherUnitF = prefs.getBool("weatherUnitF", false);
  weatherUnitMph = prefs.getBool("weatherUnitMph", false);
  Serial.print("[SETUP] Weather unit loaded: ");
  Serial.println(weatherUnitF ? "°F" : "°C");
  
  prefs.end();
  
  // Debug výpis s invertColors
  Serial.print("[SETUP] Preferences loaded - Theme: ");
  Serial.print(themeMode);
  Serial.print(", AutoDim: ");
  Serial.print(autoDimEnabled);
  Serial.print(", InvertColors: ");
  Serial.println(invertColors ? "TRUE" : "FALSE");

// ===== TFT LCD INITIALIZATION =====
  tft.init();
  tft.setRotation(1);
  
  // DŮLEŽITÉ: CYD má hardwarově invertovaný displej, takže logika je opačná:
  // invertColors=false (normální) → tft.invertDisplay(true) - kompenzuje hardware
  // invertColors=true (invertováno) → tft.invertDisplay(false) - nechat hardware
  delay(50);
  tft.invertDisplay(!invertColors);  // OPAČNÁ LOGIKA kvůli hardwarové inverzi
  delay(50);
  
  tft.fillScreen(getBgColor()); // Nejprve vyplníme obrazovku
  
  pinMode(LCD_BL_PIN, OUTPUT);
  analogWrite(LCD_BL_PIN, brightness);
  
  Serial.print("[SETUP] Display inverted (SW): ");
  Serial.print(!invertColors ? "TRUE" : "FALSE");
  Serial.print(" | User wants inversion: ");
  Serial.println(invertColors ? "YES" : "NO");
  
  Serial.println("[SETUP] TFT initialized");

  // ===== TOUCHSCREEN INITIALIZATION =====
  SPI.begin(T_CLK, T_DOUT, T_DIN);
  ts.begin();
  ts.setRotation(1);
  
  Serial.println("[SETUP] Touchscreen initialized");
  
  // ===== UI INITIALIZATION =====
  tft.setTextColor(getTextColor());
  tft.setTextDatum(MC_DATUM);

  // ===== LOAD SAVED LOCATION =====
  loadSavedLocation();
  loadRecentCities();
  weatherCity = cityName;
  
  Serial.println("[SETUP] Location loaded: " + cityName);

  // ===== NAMEDAY VARIABLES =====
  lastNamedayDay = -1;
  lastNamedayHour = -1;

  // ===== WIFI CONNECTION IF SAVED =====
  if (ssid != "") {
    Serial.println("[SETUP] Attempting WiFi connection with saved SSID: " + ssid);
    showWifiConnectingScreen(ssid);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) {
      delay(500);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[SETUP] WiFi connected successfully");
      showWifiResultScreen(true);
      
      if (regionAutoMode) {
        Serial.println("[SETUP] Auto-sync enabled, syncing region...");
        syncRegion();
      }
      
      currentState = CLOCK;
      lastSec = -1;  // Vynutí kompletní překreslení v loop()
      
      handleNamedayUpdate();
    } else {
      Serial.println("[SETUP] WiFi connection failed");
      showWifiResultScreen(false);
      currentState = WIFICONFIG;
      scanWifiNetworks();
      drawInitialSetup();
    }
  } else {
    Serial.println("[SETUP] No saved WiFi, showing setup screen");
    currentState = WIFICONFIG;
    scanWifiNetworks();
    drawInitialSetup();
  }
  
  Serial.println("[SETUP] === Setup complete ===\n");
}

String getNamedayForDate(int day, int month) {
  // Hardcoded ceske svatky bez diakritiky - pouze pro Czech Republic
  static const char* namedays[13][32] = {
    {}, // mesic 0 (neexistuje)
    {"--","Novy rok","Karina","Radmila","Diana","Dalimil","Tri krále","Vilma","Ctirad","Adrian","Brezislav","Bohdana","Pravoslav","Edita","Radovan","Alice","Ctirad","Drahoslav","Vladislav","Doubravka","Ilona","Elian","Slavomir","Zdenek","Milena","Milos","Zora","Ingrid","Otyla","Zdislava","Robin","Marika"}, // Leden
    {"--","Hynek","Nela","Blazej","Jarmila","Dobromila","Vanda","Veronika","Milada","Apolena","Mojmir","Bozena","Slavena","Vendelin","Valentin","Jiri","Ljuba","Miloslav","Gizela","Patrik","Oldrich","Lenka","Petr","Svatopluk","Matej","Liliana","Dorotea","Alexandr","Lumír","Horymír","--","--"}, // Unor
    {"--","Bedrich","Anezka","Kamil","Stela","Kazimir","Miroslav","Tomas","Gabriela","Franciska","Viktorie","Andelka","Rehore","Ruzena","Matylda","Kristyna","Lubomir","Vlastimil","Eduard","Josef","Svetlana","Radek","Leona","Ivona","Gabriel","Marian","Emanuel","Dita","Sonar","Taťana","Arnošt","Kveta"}, // Brezen
    {"--","Hugo","Erika","Richard","Ivana","Miroslava","Vendula","Herman","Ema","Dusan","Darja","Izabela","Julius","Ales","Vincenc","Anastázie","Irena","Rudolf","Valerie","Rostislav","Marcela","Alexandr","Evženie","Vojtech","Jiri","Marek","Oto","Jaroslav","Vlastislav","Robert","Blahoslav","--"}, // Duben
    {"--","Svátek práce","Zikmund","Alexej","Květoslav","Klaudie","Radoslav","Stanislav","Den vítězství","Ctibor","Blažena","Svatava","Pankrac","Servác","Bonifác","Žofie","Přemysl","Aneta","Nataša","Ivo","Zbyšek","Monika","Emil","Vladimír","Jana","Viola","Filip","Valdemar","Vilém","Maxim","Ferdinand","Kamila"}, // Kveten
    {"--","Laura","Jarmil","Tamara","Dalibor","Dobroslav","Norbert","Iveta","Medard","Stanislava","Gita","Bruno","Antonie","Antonín","Roland","Vít","Zbyněk","Adolf","Milan","Leoš","Květa","Alois","Pavla","Zdeňka","Jan","Ivan","Adriana","Ladislav","Lubomír","Petr a Pavel","Šárka","--"}, // Cerven
    {"--","Jaroslava","Patricie","Radomír","Prokop","Cyril a Metoděj","Jan Hus","Bohuslava","Nora","Drahoslava","Libuše a Amálie","Olga","Bořek","Markéta","Karolína","Jindřich","Luboš","Martina","Drahomíra","Čeněk","Ilja","Vítězslav","Magdaléna","Libor","Kristýna","Jakub","Anna","Věroslav","Viktor","Marta","Bořivoj","Ignác"}, // Cervenec
    {"--","Oskar","Gustav","Miluše","Dominik","Kristián","Oldřiška","Lada","Soběslav","Roman","Vavřinec","Zuzana","Klára","Alena","Alan","Hana","Jáchym","Petra","Helena","Ludvík","Bernard","Johana","Bohuslav","Sandra","Bartoloměj","Radim","Luděk","Otakar","Augustýn","Evelína","Vladěna","Pavlína"}, // Srpen
    {"--","Linda","Adéla","Bronislav","Jindřiška","Boris","Boleslav","Regína","Mariana","Daniela","Irma","Denisa","Marie","Lubor","Radka","Jolana","Ludmila","Naděžda","Kryštof","Zita","Oleg","Matouš","Darina","Berta","Jaromír","Zlata","Andrea","Jonáš","Václav","Michal","Jeroným","--"}, // Zari
    {"--","Igor","Olivie","Bohumil","František","Eliška","Hanuš","Justýna","Věra","Štefan","Marina","Andrej","Marcel","Renáta","Agáta","Tereza","Havel","Hedvika","Lukáš","Michaela","Vendelín","Brigita","Sabina","Teodor","Nina","Beáta","Erik","Šarlota","Státní svátek","Silvie","Tadeáš","Štěpánka"}, // Rijen
    {"--","Felix","Památka zesnulých","Hubert","Karel","Miriam","Liběna","Saskie","Bohumír","Bohdan","Evžen","Martin","Benedikt","Tibor","Sáva","Leopold","Otmar","Den boje za svobodu","Romana","Alžběta","Nikola","Albert","Cecílie","Klement","Emílie","Kateřina","Artur","Xenie","René","Zina","Ondřej","--"}, // Listopad
    {"--","Iva","Blanka","Svatoslav","Barbora","Jitka","Mikuláš","Ambrož","Květoslava","Vratislav","Julie","Dana","Simona","Lucie","Lýdie","Radana","Albína","Daniel","Miloslav","Ester","Dagmar","Natálie","Šimon","Vlasta","Štědrý den","1. svátek vánoční","2. svátek vánoční","Žaneta","Bohumila","Judita","David","Silvestr"} // Prosinec
  };
  
  if (month < 1 || month > 12 || day < 1 || day > 31) return "--";
  return String(namedays[month][day]);
}

void handleNamedayUpdate() {
  // Pouze pro Czech Republic - hardcoded svatky
  if (selectedCountry != "Czech Republic") {
    namedayValid = false;
    todayNameday = "--";
    return;
  }

  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);

  if (!timeinfo) {
    namedayValid = false;
    todayNameday = "--";
    return;
  }
  
  if (timeinfo->tm_year < 125) {
    namedayValid = false;
    todayNameday = "--";
    return;
  }

  int today = timeinfo->tm_mday;
  int month = timeinfo->tm_mon + 1;
  int hour = timeinfo->tm_hour;

  // Aktualizace svatku pri zmene dne
  if (today != lastNamedayDay) {
    lastNamedayDay = today;
    lastNamedayHour = hour;

    Serial.print("[NAMEDAY] Getting nameday for day ");
    Serial.print(String(today));
    Serial.print(".");
    Serial.println(String(month));

    todayNameday = getNamedayForDate(today, month);
    namedayValid = (todayNameday != "--");

    if (namedayValid) {
      Serial.print("[NAMEDAY] SUCCESS: ");
      Serial.println(todayNameday);
      forceClockRedraw = true;
    } else {
      Serial.println("[NAMEDAY] No nameday for this date");
    }
  } else if (hour == 0 && lastNamedayHour != 0) {
    // Pulnocni kontrola
    lastNamedayHour = hour;
    
    time_t now2 = time(nullptr);
    struct tm *timeinfo2 = localtime(&now2);
    if (timeinfo2 && timeinfo2->tm_mday != lastNamedayDay) {
      lastNamedayDay = timeinfo2->tm_mday;
      month = timeinfo2->tm_mon + 1;
      todayNameday = getNamedayForDate(lastNamedayDay, month);
      namedayValid = (todayNameday != "--");
      
      if (namedayValid) {
        Serial.println("[NAMEDAY] Midnight update: " + todayNameday);
        forceClockRedraw = true;
      }
    }
  } else {
    lastNamedayHour = hour;
  }
}

void handleKeyboardTouch(int x, int y) {
  Serial.println("[KEYBOARD] Touch detected at X=" + String(x) + ", Y=" + String(y));
  // ========== DETEKCE PÍSMEN A ČÍSEL V ŘÁDCÍCH KLÁVESNICE ==========
  for (int r = 0; r < 3; r++) {
    const char *row;
    if (keyboardNumbers) {
      if (r == 0) {
        row = "1234567890";
      } else if (r == 1) {
        row = "!@#$%^&*(/";
      } else {
        row = ")-_+=.,?";
      }
    } else {
      if (r == 0) {
        row = "qwertyuiop";
      } else if (r == 1) {
        row = "asdfghjkl";
      } else {
        row = "zxcvbnm";
      }
    }
    
    int len = strlen(row);
    for (int i = 0; i < len; i++) {
      int btnX = i * 29 + 2;
      int btnY = 80 + r * 30;
      
      // Upravena detekce dotyku na 26x26 (design WiFi klávesnice)
      if (x >= btnX && x <= btnX + 26 && y >= btnY && y <= btnY + 26) {
        char ch = row[i];
        if (keyboardShift && !keyboardNumbers) {
          ch = toupper(ch);
        }
        
        if (currentState == KEYBOARD) {
          passwordBuffer += ch;
          Serial.println("[KEYBOARD] Added to passwordBuffer: " + String(ch));
          updateKeyboardText();
        } else if (currentState == CUSTOMCITYINPUT) {
          customCityInput += ch;
          Serial.println("[KEYBOARD] Added to customCityInput: " + String(ch));
          drawCustomCityInput(); // Celkové překreslení pro udržení vizuálu
        } else if (currentState == CUSTOMCOUNTRYINPUT) {
          customCountryInput += ch;
          Serial.println("[KEYBOARD] Added to customCountryInput: " + String(ch));
          drawCustomCountryInput(); // Celkové překreslení pro udržení vizuálu
        }
        
        delay(150);
        return;
      }
    }
  }
  
  // ========== DETEKCE MEZERNÍKU (SPACE) ==========
  if (x >= 2 && x <= 318 && y >= 170 && y <= 195) {
    Serial.println("[KEYBOARD] Space pressed");
    if (currentState == KEYBOARD) {
      passwordBuffer += " ";
      updateKeyboardText();
    } else if (currentState == CUSTOMCITYINPUT) {
      customCityInput += " ";
      drawCustomCityInput();
    } else if (currentState == CUSTOMCOUNTRYINPUT) {
      customCountryInput += " ";
      drawCustomCountryInput();
    }
    
    delay(150);
    return;
  }
  
  // ========== DETEKCE FUNKČNÍCH TLAČÍTEK ==========
  int bw = 64;
  int by = 198;
  int bh = 35;
  
  // ===== TLAČÍTKO 1: SHIFT (CAP) =====
  if (x >= 0 && x <= bw && y >= by && y <= by + bh) {
    Serial.println("[KEYBOARD] SHIFT pressed");
    keyboardShift = !keyboardShift;
    
    if (currentState == KEYBOARD) {
      drawKeyboardScreen();
    } else if (currentState == CUSTOMCITYINPUT) {
      drawCustomCityInput();
    } else if (currentState == CUSTOMCOUNTRYINPUT) {
      drawCustomCountryInput();
    }
    
    delay(150);
    return;
  }
  
  // ===== TLAČÍTKO 2: ČÍSLA (123) =====
  if (x >= bw && x <= 2 * bw && y >= by && y <= by + bh) {
    Serial.println("[KEYBOARD] NUMBERS toggle pressed");
    keyboardNumbers = !keyboardNumbers;
    
    if (currentState == KEYBOARD) {
      drawKeyboardScreen();
    } else if (currentState == CUSTOMCITYINPUT) {
      drawCustomCityInput();
    } else if (currentState == CUSTOMCOUNTRYINPUT) {
      drawCustomCountryInput();
    }
    
    delay(150);
    return;
  }
  
  // ===== TLAČÍTKO 3: SMAZÁNÍ (DEL) =====
  if (x >= 2 * bw && x <= 3 * bw && y >= by && y <= by + bh) {
    Serial.println("[KEYBOARD] DEL pressed");
    if (currentState == KEYBOARD) {
      if (passwordBuffer.length() > 0) {
        passwordBuffer.remove(passwordBuffer.length() - 1);
        updateKeyboardText();
      }
    } else if (currentState == CUSTOMCITYINPUT) {
      if (customCityInput.length() > 0) {
        customCityInput.remove(customCityInput.length() - 1);
        drawCustomCityInput();
      }
    } else if (currentState == CUSTOMCOUNTRYINPUT) {
      if (customCountryInput.length() > 0) {
        customCountryInput.remove(customCountryInput.length() - 1);
        drawCustomCountryInput();
      }
    }
    
    delay(150);
    return;
  }
  
  // ===== TLAČÍTKO 4: LOOKUP / SEARCH / OK (WiFi) =====
  if (x >= 3 * bw && x <= 4 * bw && y >= by && y <= by + bh) {
    // 1) WiFi - Tlačítko BACK (v designu WiFi je Back na pozici 3, zde je to pozice 4 v gridu?)
    // Pozor: V drawKeyboardScreen je: 3*bw = Back(Red), 4*bw = OK(Green).
    // V drawCustomCityInput je: 3*bw = Lookup(Green), 4*bw = Back(Orange).
    
    // Zde musíme rozlišit podle currentState, co je na které souřadnici
    
    if (currentState == KEYBOARD) {
       // Pro WiFi je na pozici 3*bw tlačítko BACK
       Serial.println("[KEYBOARD] WIFI BACK pressed");
       passwordBuffer = ""; // VYMAZÁNÍ TEXTU PŘI OPUŠTĚNÍ
       keyboardShift = false;
       keyboardNumbers = false;
       currentState = WIFICONFIG;
       drawInitialSetup();
       
    } else if (currentState == CUSTOMCITYINPUT) {
      // Pro City Input je na pozici 3*bw tlačítko LOOKUP (Green)
      Serial.println("[KEYBOARD] LOOKUP pressed for city");
      if (customCityInput.length() > 0) {
        Serial.println("[KEYBOARD] Looking up city: " + customCityInput);
        lookupCityGeonames(customCityInput, selectedCountry);
        currentState = CITYLOOKUPCONFIRM;
        drawCityLookupConfirm();
      } else {
        Serial.println("[KEYBOARD] City input empty, cannot lookup");
        delay(2000);
        drawCustomCityInput();
      }
    } else if (currentState == CUSTOMCOUNTRYINPUT) {
      // Pro Country Input je na pozici 3*bw tlačítko SEARCH (Green)
      Serial.println("[KEYBOARD] SEARCH pressed for country");
      if (customCountryInput.length() > 0) {
        Serial.println("[KEYBOARD] Looking up country: " + customCountryInput);
        lookupCountryGeonames(customCountryInput);
        currentState = COUNTRYLOOKUPCONFIRM;
        drawCountryLookupConfirm();
      } else {
        Serial.println("[KEYBOARD] Country input empty, cannot lookup");
        delay(2000);
        drawCustomCountryInput();
      }
    }
    
    delay(150);
    return;
  }
  
  // ===== TLAČÍTKO 5: ZPĚT (Custom) / OK (WiFi) =====
  if (x >= 4 * bw && x <= 5 * bw && y >= by && y <= by + bh) {
    
    if (currentState == KEYBOARD) {
      // Pro WiFi je na pozici 4*bw tlačítko OK (Green)
      Serial.println("[KEYBOARD] WIFI OK pressed");
      prefs.begin("sys", false);
      prefs.putString("ssid", selectedSSID);
      prefs.putString("pass", passwordBuffer);
      prefs.end();
      
      ssid = selectedSSID;
      password = passwordBuffer;
      
      showWifiConnectingScreen(ssid);
      WiFi.mode(WIFI_STA);
      WiFi.disconnect();
      delay(100);
      WiFi.begin(ssid.c_str(), password.c_str());
      
      unsigned long startWait = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - startWait < 15000) {
        delay(500);
      }
      
      if (WiFi.status() == WL_CONNECTED) {
         showWifiResultScreen(true);
         if (regionAutoMode) syncRegion();
         currentState = CLOCK;
         lastSec = -1; 
      } else {
         showWifiResultScreen(false);
         currentState = WIFICONFIG;
         drawInitialSetup();
      }

    } else if (currentState == CUSTOMCITYINPUT) {
      // Pro City Input je na pozici 4*bw tlačítko BACK
      Serial.println("[KEYBOARD] Returning from custom city input");
      customCityInput = ""; // VYMAZÁNÍ TEXTU PŘI OPUŠTĚNÍ
      keyboardShift = false;
      keyboardNumbers = false;
      currentState = CITYSELECT;
      cityOffset = 0;
      drawCitySelection();
      
    } else if (currentState == CUSTOMCOUNTRYINPUT) {
      // Pro Country Input je na pozici 4*bw tlačítko BACK
      Serial.println("[KEYBOARD] Returning from custom country input");
      customCountryInput = ""; // VYMAZÁNÍ TEXTU PŘI OPUŠTĚNÍ
      keyboardShift = false;
      keyboardNumbers = false;
      currentState = COUNTRYSELECT;
      countryOffset = 0;
      drawCountrySelection();
    }
    
    delay(150);
    return;
  }
  
  // ===== SHOW/HIDE HESLA (POUZE PRO WIFI) =====
  if (currentState == KEYBOARD && x >= 250 && x <= 320 && y >= 140 && y <= 165) {
    Serial.println("[KEYBOARD] SHOW/HIDE password toggled");
    showPassword = !showPassword;
    drawKeyboardScreen();
    delay(150);
    return;
  }
}

void loop() {
  // AUTODIM LOGIC
  if (millis() - lastBrightnessUpdate > 60000) {
    applyAutoDim();
    lastBrightnessUpdate = millis();
  }

  // 1. WiFi CONNECTION CHECK
  if (WiFi.status() != WL_CONNECTED) {
    if (currentState != WIFICONFIG && currentState != KEYBOARD && currentState != CUSTOMCITYINPUT && currentState != CUSTOMCOUNTRYINPUT &&
        currentState != SETTINGS && currentState != WEATHERCONFIG && currentState != REGIONALCONFIG && currentState != GRAPHICSCONFIG &&
        currentState != FIRMWARE_SETTINGS && currentState != COUNTRYSELECT && currentState != CITYSELECT && currentState != LOCATIONCONFIRM &&
        currentState != COUNTRYLOOKUPCONFIRM && currentState != CITYLOOKUPCONFIRM) {
       currentState = CLOCK;
    }
    
    static unsigned long lastReconnectAttempt = 0;
    if (millis() - lastReconnectAttempt > 30000) {
      Serial.println("WIFI: Attempting reconnect...");
      WiFi.reconnect();
      lastReconnectAttempt = millis();
    }
  }

  // 2. TOUCH HANDLING
  if (ts.touched()) {
    if (millis() - lastTouchTime < 200) {
      return;
    }
    lastTouchTime = millis();

    TS_Point p = ts.getPoint();
    int x = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_WIDTH);
    int y = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_HEIGHT);
    x = constrain(x, 0, SCREEN_WIDTH - 1);
    y = constrain(y, 0, SCREEN_HEIGHT - 1);

    switch (currentState) {
      case CLOCK: {
        // Tlačítko nastavení (Settings)
        if (x >= 270 && x <= 320 && y >= 200 && y <= 240) {
          currentState = SETTINGS;
          menuOffset = 0;
          drawSettingsScreen();
        }
        
        // PŘIDÁNO: Dotyk na hodiny pro změnu formátu 12/24h (JEN V DIGITÁLNÍM REŽIMU)
        // Oblast hodin cca x: 180-280, y: 40-130 (dle clockX, clockY a radius)
        // clockX = 230, clockY = 85, radius = 67
        if (isDigitalClock && x >= 160 && x <= 300 && y >= 20 && y <= 150) {
           is12hFormat = !is12hFormat;
           prefs.begin("sys", false); prefs.putBool("12hFmt", is12hFormat); prefs.end();
           // Vynutit překreslení vymazáním lastSec
           lastSec = -1;
           delay(200); 
        }
        break;
      }

      case SETTINGS: {
        // Tlačítko ZPĚT (červené)
        if (x >= 230 && x <= 280 && y >= 125 && y <= 175) {
          currentState = CLOCK;
          lastSec = -1;
          delay(150);
        } 
        // Šipka nahoru
        else if (menuOffset > 0 && x >= 230 && x <= 280 && y >= 70 && y <= 120) {
          menuOffset--;
          drawSettingsScreen();
          delay(150);
        }
        // Šipka dolů
        else if (menuOffset < 1 && x >= 230 && x <= 280 && y >= 180 && y <= 230) {
          menuOffset++;
          drawSettingsScreen();
          delay(150);
        }
        // Detekce kliknutí na položky menu
        else {
          for (int i = 0; i < 4; i++) {  // 4 viditelné položky na obrazovce
            if (isTouchInMenuItem(y, i)) {
              int actualItem = i + menuOffset;  // Přepočet: vizuální pozice → skutečná položka
              
              switch(actualItem) {
                case 0: // WiFi Setup
                  currentState = WIFICONFIG;
                  scanWifiNetworks();
                  wifiOffset = 0;
                  drawInitialSetup();
                  break;
                  
                case 1: // Weather
                  currentState = WEATHERCONFIG;
                  drawWeatherScreen();
                  break;
                  
                case 2: // Regional
                  currentState = REGIONALCONFIG;
                  drawRegionalScreen();
                  break;
                  
                case 3: // Graphics
                  currentState = GRAPHICSCONFIG;
                  drawGraphicsScreen();
                  break;
                  
                case 4: // Firmware
                  currentState = FIRMWARE_SETTINGS;
                  drawFirmwareScreen();
                  break;
              }
              
              delay(150);
              break;  // Vyskočit z for cyklu
            }
          }
        }
        break;
      }

      case WIFICONFIG: {
        if (ssid != "" && x >= 265 && x <= 315 && y >= 50 && y <= 100) {
          currentState = SETTINGS;
          menuOffset = 0;
          drawSettingsScreen();
        } 
        else if (x >= 265 && x <= 315 && y >= 110 && y <= 160) {
          if (wifiOffset > 0) {
            wifiOffset--;
            drawInitialSetup();
          }
        } 
        else if (x >= 265 && x <= 315 && y >= 170 && y <= 220) {
          if (wifiOffset + 6 < wifiCount) {
            wifiOffset++;
            drawInitialSetup();
          }
        } 
        else {
          for (int i = wifiOffset; i < wifiOffset + 6 && i < wifiCount; i++) {
            int idx = i - wifiOffset;
            int yPos = 45 + idx * 30;
            if (y >= yPos && y <= yPos + 25) {
              selectedSSID = wifiSSIDs[i];
              currentState = KEYBOARD;
              passwordBuffer = "";
              keyboardNumbers = false;
              keyboardShift = false;
              drawKeyboardScreen();
              break;
            }
          }
        }
        break;
      }

      case KEYBOARD: {
        for (int r = 0; r < 3; r++) {
          const char *row;
          if (keyboardNumbers) {
            if (r == 0) row = "1234567890";
            else if (r == 1) row = "!@#$%^&*(/";
            else row = ")-_+=.,?";
          } else {
            if (r == 0) row = "qwertyuiop";
            else if (r == 1) row = "asdfghjkl";
            else row = "zxcvbnm";
          }
          int len = strlen(row);
          for (int i = 0; i < len; i++) {
            int btnX = i * 29 + 2;
            int btnY = 80 + r * 30;
            if (x >= btnX && x <= btnX + 26 && y >= btnY && y <= btnY + 26) {
              char ch = row[i];
              if (keyboardShift && !keyboardNumbers) ch = toupper(ch);
              passwordBuffer += ch;
              updateKeyboardText();
              delay(150);
              return;
            }
          }
         }
        if (x >= 2 && x <= 318 && y >= 170 && y <= 195) {
          passwordBuffer += " ";
          updateKeyboardText();
          delay(150);
          return;
        }
        int bw = 64; int by = 198;
        int bh = 35;
        if (x >= 0 && x <= bw && y >= by && y <= by + bh) {
          keyboardShift = !keyboardShift;
          drawKeyboardScreen();
          delay(150);
          return;
        }
        if (x >= bw && x <= 2 * bw && y >= by && y <= by + bh) {
          keyboardNumbers = !keyboardNumbers;
          drawKeyboardScreen();
          delay(150);
          return;
        }
        if (x >= 2 * bw && x <= 3 * bw && y >= by && y <= by + bh) {
          if (passwordBuffer.length() > 0) {
            passwordBuffer.remove(passwordBuffer.length() - 1);
            updateKeyboardText();
            delay(150);
          }
          return;
        }
        if (x >= 3 * bw && x <= 4 * bw && y >= by && y <= by + bh) {
          passwordBuffer = "";
          currentState = WIFICONFIG;
          drawInitialSetup();
          delay(200);
          return;
        }
        if (x >= 4 * bw && x <= 5 * bw && y >= by && y <= by + bh) {
          prefs.begin("sys", false);
          prefs.putString("ssid", selectedSSID);
          prefs.putString("pass", passwordBuffer);
          prefs.end();
          ssid = selectedSSID; password = passwordBuffer;
          showWifiConnectingScreen(ssid);
          WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(100);
          WiFi.begin(ssid.c_str(), password.c_str());
          unsigned long startWait = millis();
          while (WiFi.status() != WL_CONNECTED && millis() - startWait < 15000) delay(500);
          if (WiFi.status() == WL_CONNECTED) {
             showWifiResultScreen(true);
             if (regionAutoMode) syncRegion();
             currentState = CLOCK; lastSec = -1; 
          } else {
             showWifiResultScreen(false);
             currentState = WIFICONFIG; drawInitialSetup();
          }
          delay(200);
          return;
        }
        if (x >= 250 && x <= 310 && y >= 140 && y <= 165) {
          showPassword = !showPassword;
          drawKeyboardScreen();
          delay(150);
          return;
        }
        break;
      }

         case WEATHERCONFIG: {
        // Tlačítko °C (levá strana)
        if (x >= 15 && x <= 55 && y >= 150 && y <= 175) {
          if (weatherUnitF) {
            weatherUnitF = false;
            prefs.begin("sys", false); 
            prefs.putBool("weatherUnitF", weatherUnitF); 
            prefs.end();
            drawWeatherScreen(); 
            delay(200);
          }
          break;
        }
        
        // Tlačítko °F (levá strana)
        if (x >= 75 && x <= 115 && y >= 150 && y <= 175) {
          if (!weatherUnitF) {
            weatherUnitF = true;
            prefs.begin("sys", false); 
            prefs.putBool("weatherUnitF", weatherUnitF); 
            prefs.end();
            drawWeatherScreen(); 
            delay(200);
          }
          break;
        }
        
        // Tlačítko km/h (pravá strana)
        if (x >= 175 && x <= 225 && y >= 150 && y <= 175) {
          if (weatherUnitMph) {
            weatherUnitMph = false;
            prefs.begin("sys", false); 
            prefs.putBool("weatherUnitMph", weatherUnitMph); 
            prefs.end();
            drawWeatherScreen(); 
            delay(200);
          }
          break;
        }
        
        // Tlačítko mph (pravá strana)
        if (x >= 235 && x <= 285 && y >= 150 && y <= 175) {
          if (!weatherUnitMph) {
            weatherUnitMph = true;
            prefs.begin("sys", false); 
            prefs.putBool("weatherUnitMph", weatherUnitMph); 
            prefs.end();
            drawWeatherScreen(); 
            delay(200);
          }
          break;
        }
        
        // Tlačítko BACK
        if (x >= 40 && x <= 280 && y >= 220 && y <= 235) {
          currentState = SETTINGS;
          menuOffset = 0; 
          drawSettingsScreen();
        }
        break;
      }

      case REGIONALCONFIG: {
        if (x >= 160 - 55 && x <= 160 + 55 && y >= 60 - 15 && y <= 60 + 15) {
          regionAutoMode = !regionAutoMode;
          prefs.begin("sys", false); prefs.putBool("regionAuto", regionAutoMode); prefs.end();
          drawRegionalScreen();
        } else if (x >= 40 && x <= 145 && y >= 205 && y <= 235) {
          if (regionAutoMode) {
            syncRegion();
            currentState = CLOCK; lastSec = -1;
          } else {
            currentState = COUNTRYSELECT;
            countryOffset = 0; drawCountrySelection();
          }
        } else if (x >= 155 && x <= 260 && y >= 205 && y <= 235) {
          currentState = CLOCK;
          lastSec = -1;
        }
        break;
      }

      case COUNTRYSELECT: {
        if (x >= 230 && x <= 320 && y >= 45 && y <= 95) {
          if (countryOffset > 0) countryOffset--;
          drawCountrySelection();
        } else if (x >= 230 && x <= 320 && y >= 180 && y <= 230) {
          if (countryOffset + 5 < COUNTRIES_COUNT) countryOffset++;
          drawCountrySelection();
        } else if (x >= 230 && x <= 320 && y >= 110 && y <= 160) {
          currentState = REGIONALCONFIG;
          drawRegionalScreen();
        } else if (y >= 70 + 5 * 30 && y <= 70 + 6 * 30) {
          customCountryInput = "";
          currentState = CUSTOMCOUNTRYINPUT;
          keyboardNumbers = false; keyboardShift = false; drawCustomCountryInput();
        } else {
          for (int i = countryOffset; i < countryOffset + 5 && i < COUNTRIES_COUNT; i++) {
            int idx = i - countryOffset;
            int yPos = 70 + idx * 30;
            if (y >= yPos && y <= yPos + 25) {
              selectedCountry = String(countries[i].name);
              currentState = CITYSELECT; cityOffset = 0; drawCitySelection();
              break;
            }
          }
        }
        break;
      }

      case CITYSELECT: {
        String cities[20];
        int cityCount = 0;
        getCountryCities(selectedCountry, cities, cityCount);
        if (x >= 230 && x <= 320 && y >= 45 && y <= 95) {
          if (cityOffset > 0) cityOffset--;
          drawCitySelection();
        } else if (x >= 230 && x <= 320 && y >= 180 && y <= 230) {
          if (cityOffset + 5 < cityCount) cityOffset++;
          drawCitySelection();
        } else if (x >= 230 && x <= 320 && y >= 110 && y <= 160) {
          currentState = COUNTRYSELECT;
          countryOffset = 0; drawCountrySelection();
        } else if (y >= 70 + 5 * 30 && y <= 70 + 6 * 30) {
          customCityInput = "";
          currentState = CUSTOMCITYINPUT;
          keyboardNumbers = false; keyboardShift = false; drawCustomCityInput();
        } else {
          for (int i = cityOffset; i < cityOffset + 5 && i < cityCount; i++) {
            int idx = i - cityOffset;
            int yPos = 70 + idx * 30;
            if (y >= yPos && y <= yPos + 25) {
              selectedCity = cities[i];
              String tz; int go, doff;
              if (getTimezoneForCity(selectedCountry, selectedCity, tz, go, doff)) {
                selectedTimezone = tz;
                gmtOffset_sec = go; daylightOffset_sec = doff;
                currentState = LOCATIONCONFIRM; drawLocationConfirm();
              }
              break;
            }
          }
        }
        break;
      }

      case LOCATIONCONFIRM: {
        if (x >= 40 && x <= 145 && y >= 205 && y <= 235) {
          applyLocation();
          currentState = CLOCK; lastSec = -1;
        } else if (x >= 155 && x <= 260 && y >= 205 && y <= 235) {
          currentState = CITYSELECT;
          cityOffset = 0; drawCitySelection();
        }
        break;
      }

      case CUSTOMCITYINPUT: {
        if (x >= 180 && x <= 250 && y >= 198 && y <= 233) {
          if (customCityInput.length() > 0) {
            lookupCityGeonames(customCityInput, selectedCountry);
            currentState = CITYLOOKUPCONFIRM; drawCityLookupConfirm();
          } else drawCustomCityInput();
          return;
        }
        
        // OPRAVA: Výběr správné sady znaků podle keyboardNumbers
        const char *rows[3];
        if (keyboardNumbers) {
            rows[0] = "1234567890";
            rows[1] = "!@#$%^&*(/";
            rows[2] = ")-_+=.,?";
        } else {
            rows[0] = "qwertyuiop";
            rows[1] = "asdfghjkl";
            rows[2] = "zxcvbnm";
        }
        
        for (int r = 0; r < 3; r++) {
            for (int i = 0; i < strlen(rows[r]); i++) {
              if (x >= i * 29 && x <= i * 29 + 29 && y >= 80 + r * 30 && y <= 80 + r * 30 + 30) {
                char ch = rows[r][i];
                if (keyboardShift && !keyboardNumbers) ch = toupper(ch);
                customCityInput += ch; keyboardShift = false; drawCustomCityInput();
                return;
              }
            }
        }
        if (x >= 0 && x <= 318 && y >= 170 && y <= 195) { customCityInput += " ";
          drawCustomCityInput(); return; }
        if (x >= 250 && x <= 320 && y >= 198 && y <= 233) { customCityInput = "";
          currentState = CITYSELECT; cityOffset = 0; drawCitySelection(); return; }
        if (x >= 120 && x <= 180 && y >= 198 && y <= 233) { if (customCityInput.length() > 0) { customCityInput.remove(customCityInput.length() - 1);
          drawCustomCityInput(); } return; }
        if (x >= 60 && x <= 120 && y >= 198 && y <= 233) { keyboardNumbers = !keyboardNumbers;
          drawCustomCityInput(); return; }
        if (x >= 0 && x <= 60 && y >= 198 && y <= 233) { keyboardShift = !keyboardShift;
          drawCustomCityInput(); return; }
        break;
      }

      case CUSTOMCOUNTRYINPUT: {
        if (x >= 180 && x <= 250 && y >= 198 && y <= 233) {
          if (customCountryInput.length() > 0) {
            lookupCountryGeonames(customCountryInput);
            currentState = COUNTRYLOOKUPCONFIRM; drawCountryLookupConfirm();
          } else drawCustomCountryInput();
          return;
        }
        
        // OPRAVA: Výběr správné sady znaků podle keyboardNumbers
        const char *rows[3];
        if (keyboardNumbers) {
            rows[0] = "1234567890";
            rows[1] = "!@#$%^&*(/";
            rows[2] = ")-_+=.,?";
        } else {
            rows[0] = "qwertyuiop";
            rows[1] = "asdfghjkl";
            rows[2] = "zxcvbnm";
        }
        
        for (int r = 0; r < 3; r++) {
            for (int i = 0; i < strlen(rows[r]); i++) {
              if (x >= i * 29 && x <= i * 29 + 29 && y >= 80 + r * 30 && y <= 80 + r * 30 + 30) {
                char ch = rows[r][i];
                if (keyboardShift && !keyboardNumbers) ch = toupper(ch);
                customCountryInput += ch; keyboardShift = false; drawCustomCountryInput();
                return;
              }
            }
        }
        if (x >= 0 && x <= 318 && y >= 170 && y <= 195) { customCountryInput += " ";
          drawCustomCountryInput(); return; }
        if (x >= 250 && x <= 320 && y >= 198 && y <= 233) { customCountryInput = "";
          currentState = COUNTRYSELECT; countryOffset = 0; drawCountrySelection(); return; }
        if (x >= 120 && x <= 180 && y >= 198 && y <= 233) { if (customCountryInput.length() > 0) { customCountryInput.remove(customCountryInput.length() - 1);
          drawCustomCountryInput(); } return; }
        if (x >= 60 && x <= 120 && y >= 198 && y <= 233) { keyboardNumbers = !keyboardNumbers;
          drawCustomCountryInput(); return; }
        if (x >= 0 && x <= 60 && y >= 198 && y <= 233) { keyboardShift = !keyboardShift;
          drawCustomCountryInput(); return; }
        break;
      }

      case CITYLOOKUPCONFIRM: {
        if (x >= 40 && x <= 145 && y >= 205 && y <= 235) {
          selectedCity = lookupCity;
          selectedTimezone = lookupTimezone;
          gmtOffset_sec = lookupGmtOffset; daylightOffset_sec = lookupDstOffset;
          applyLocation(); currentState = CLOCK; lastSec = -1;
        } else if (x >= 155 && x <= 260 && y >= 205 && y <= 235) {
          currentState = CITYSELECT;
          drawCitySelection();
        }
        break;
      }

      case COUNTRYLOOKUPCONFIRM: {
        if (x >= 40 && x <= 145 && y >= 205 && y <= 235) {
          selectedCountry = lookupCountry;
          currentState = CITYSELECT; cityOffset = 0; drawCitySelection();
        } else if (x >= 155 && x <= 260 && y >= 205 && y <= 235) {
          currentState = COUNTRYSELECT;
          countryOffset = 0; drawCountrySelection();
        }
        break;
      }
case FIRMWARE_SETTINGS: {
        // Tlačítko ZPĚT (sjednocená pozice jako ostatní menu)
        if (x >= 230 && x <= 280 && y >= 125 && y <= 175) {
          currentState = SETTINGS;
          menuOffset = 0;
          drawSettingsScreen();
          delay(150);
          break;
        }
        
        // Radio buttons pro režim (jen Auto a By user)
        // V drawFirmwareScreen:
        // yPos = 60 (Current) → 85 (Available) → 120 (Install mode) → 145 (první radio button)
        // První radio button: btnY = 145 + (0 * 25) = 145
        // Druhý radio button: btnY = 145 + (1 * 25) = 170
        for (int i = 0; i < 2; i++) {
          int btnY = 145 + (i * 25);  // OPRAVENO: 145 místo 120!
          // Kruh má střed na btnY, poloměr 6px
          // Klikatelná oblast: větší pro lepší UX (±10px od středu)
          if (x >= 10 && x <= 30 && y >= btnY - 10 && y <= btnY + 10) {
            otaInstallMode = i;
            prefs.begin("sys", false);
            prefs.putInt("otaMode", otaInstallMode);
            prefs.end();
            Serial.print("[OTA] Install mode changed to: ");
            Serial.println(i == 0 ? "Auto" : "By user");
            drawFirmwareScreen();
            delay(150);
            break;
          }
        }
        
        // Tlačítko CHECK NOW / INSTALL
        if (x >= 10 && x <= 150 && y >= 190 && y <= 220) {
          if (updateAvailable) {
            // INSTALL - vždy provedeme update (Manual režim už neexistuje)
            performOTAUpdate();
          } else {
            // CHECK NOW
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(TFT_WHITE);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("CHECKING...", 160, 100, 2);
            
            checkForUpdate();
            
            delay(1000);
            drawFirmwareScreen();
          }
          delay(150);
          break;
        }
        break;
      }

      case GRAPHICSCONFIG: {
        // ... (Kód pro Témata zůstává stejný) ...
        if (x >= 20 && x <= 70 && y >= 65 && y <= 95) {
          themeMode = 0;
          isWhiteTheme = false;
          prefs.begin("sys", false); prefs.putInt("themeMode", themeMode); prefs.putBool("theme", isWhiteTheme); prefs.end();
          tft.fillScreen(getBgColor()); drawGraphicsScreen(); delay(200); break;
        }
        if (x >= 80 && x <= 130 && y >= 65 && y <= 95) {
          themeMode = 1;
          isWhiteTheme = true;
          prefs.begin("sys", false); prefs.putInt("themeMode", themeMode); prefs.putBool("theme", isWhiteTheme); prefs.end();
          tft.fillScreen(getBgColor()); drawGraphicsScreen(); delay(200); break;
        }
        if (x >= 140 && x <= 190 && y >= 65 && y <= 95) {
          themeMode = 2;
          prefs.begin("sys", false); prefs.putInt("themeMode", themeMode); prefs.end();
          fillGradientVertical(0, 0, 320, 240, blueDark, blueLight); drawGraphicsScreen(); delay(200); break;
        }
        if (x >= 200 && x <= 250 && y >= 65 && y <= 95) {
          themeMode = 3;
          prefs.begin("sys", false); prefs.putInt("themeMode", themeMode); prefs.end();
          fillGradientVertical(0, 0, 320, 240, yellowDark, yellowLight); drawGraphicsScreen(); delay(200); break;
        }
        
// === NOVÉ TLAČÍTKO INVERT ===
        if (x >= 260 && x <= 310 && y >= 65 && y <= 95) {
          Serial.println("[INVERT] Button clicked!");
          Serial.print("[INVERT] OLD value: ");
          Serial.println(invertColors ? "TRUE" : "FALSE");
          
          invertColors = !invertColors;
          
          Serial.print("[INVERT] NEW value: ");
          Serial.println(invertColors ? "TRUE" : "FALSE");
          
          // Uložení s PODROBNÝM logováním
          Serial.println("[INVERT] Opening preferences...");
          bool prefOpened = prefs.begin("sys", false);
          Serial.print("[INVERT] Preferences opened: ");
          Serial.println(prefOpened ? "SUCCESS" : "FAILED");
          
          if (prefOpened) {
            Serial.println("[INVERT] Writing value...");
            size_t written = prefs.putBool("invertColors", invertColors);
            Serial.print("[INVERT] Bytes written: ");
            Serial.println(written);
            
            delay(100); // Dáme více času na zápis
            
            Serial.println("[INVERT] Closing preferences...");
            prefs.end();
            Serial.println("[INVERT] Preferences closed");
            
            // KONTROLA: Otevřeme znovu a přečteme
            Serial.println("[INVERT] Verification: Reading back...");
            prefs.begin("sys", true); // read-only
            bool readBack = prefs.getBool("invertColors", false);
            prefs.end();
            Serial.print("[INVERT] Read back value: ");
            Serial.println(readBack ? "TRUE" : "FALSE");
            
            if (readBack == invertColors) {
              Serial.println("[INVERT] ✓ VERIFICATION PASSED!");
            } else {
              Serial.println("[INVERT] ✗ VERIFICATION FAILED!");
            }
          }
          
          // OPAČNÁ LOGIKA: CYD má hardwarově invertovaný displej
          // invertColors=false → tft.invertDisplay(true) = normální zobrazení
          // invertColors=true → tft.invertDisplay(false) = invertované zobrazení
          Serial.println("[INVERT] Applying tft.invertDisplay...");
          tft.invertDisplay(!invertColors);  // OPAČNÁ LOGIKA
          Serial.print("[INVERT] Display SW inverted: ");
          Serial.println(!invertColors ? "TRUE" : "FALSE");
          
          drawGraphicsScreen(); 
          delay(200); 
          break;
        }

        // === NOVÉ OVLÁDÁNÍ JASU (ZMENŠENÉ) ===
        // Slider je nyní x=10, width=130
        if (x >= 10 && x <= 140 && y >= 125 && y <= 137) {
          int newBrightness = map(x - 10, 0, 130, 0, 255);
          brightness = constrain(newBrightness, 0, 255);
          prefs.begin("sys", false); prefs.putInt("bright", brightness); prefs.end();
          analogWrite(LCD_BL_PIN, brightness); drawGraphicsScreen(); delay(100); break;
        }

        // === NOVÝ PŘEPÍNAČ ANALOG / DIGITAL ===
        // Oblast: x >= 200, y cca 115-143
        if (x >= 200 && x <= 310 && y >= 115 && y <= 145) {
          isDigitalClock = !isDigitalClock;
          prefs.begin("sys", false); prefs.putBool("digiClock", isDigitalClock); prefs.end();
          drawGraphicsScreen(); delay(200); break;
        }

        // Tlačítko ZPĚT
        if (x >= 252 && x <= 308 && y >= 182 && y <= 238) {
          currentState = SETTINGS;
          menuOffset = 0; drawSettingsScreen(); delay(150); break;
        }
        
        // ... (Zbytek kódu pro AutoDim zůstává stejný) ...
        if (x >= 10 && x <= 38 && y >= 175 && y <= 191) {
             // ... kód pro AutoDim ON ...
             autoDimEnabled = true;
             prefs.begin("sys", false); prefs.putBool("autoDimEnabled", autoDimEnabled); prefs.end();
             drawGraphicsScreen(); delay(150); break;
        }
        if (x >= 10 && x <= 38 && y >= 195 && y <= 211) {
             // ... kód pro AutoDim OFF ...
             autoDimEnabled = false;
             prefs.begin("sys", false); prefs.putBool("autoDimEnabled", autoDimEnabled); prefs.end();
             drawGraphicsScreen(); delay(150); break;
        }
        // ... (zbytek AutoDim logiky ponech beze změny) ...
        // PRO JISTOTU ZKOPÍRUJ CELÝ BLOK AUTODIM Z PŮVODNÍHO SOUBORU, POKUD SI NEJSI JISTÝ.
        // Zde jen naznačuji, že zbytek case GRAPHICSCONFIG se nemění, kromě posunutí jasu a nového tlačítka.
        
        // POKRAČOVÁNÍ AUTODIM LOGIKY (aby byl kód kompletní pro copy-paste bloku):
        if (autoDimEnabled) {
          int startX = 50;
          int startY = 178; int lineHeight = 16;
          int startTimeX = startX + 50; int startPlusX = startTimeX + 50;
          int startMinusX = startPlusX + 26; int btnW = 16;
          if (x >= startPlusX && x <= startPlusX + btnW && y >= startY - 6 && y <= startY + 6) {
            autoDimStart = (autoDimStart + 1) % 24;
            prefs.begin("sys", false); prefs.putInt("autoDimStart", autoDimStart); prefs.end();
            drawGraphicsScreen(); delay(150); break;
          }
          if (x >= startMinusX && x <= startMinusX + btnW && y >= startY - 6 && y <= startY + 6) {
            autoDimStart = (autoDimStart - 1 + 24) % 24;
            prefs.begin("sys", false); prefs.putInt("autoDimStart", autoDimStart); prefs.end();
            drawGraphicsScreen(); delay(150); break;
          }
          int endY = startY + lineHeight;
          int endPlusX = startTimeX + 50; int endMinusX = endPlusX + 26;
          if (x >= endPlusX && x <= endPlusX + btnW && y >= endY - 6 && y <= endY + 6) {
            autoDimEnd = (autoDimEnd + 1) % 24;
            prefs.begin("sys", false); prefs.putInt("autoDimEnd", autoDimEnd); prefs.end();
            drawGraphicsScreen(); delay(150); break;
          }
          if (x >= endMinusX && x <= endMinusX + btnW && y >= endY - 6 && y <= endY + 6) {
            autoDimEnd = (autoDimEnd - 1 + 24) % 24;
            prefs.begin("sys", false); prefs.putInt("autoDimEnd", autoDimEnd); prefs.end();
            drawGraphicsScreen(); delay(150); break;
          }
          int levelY = endY + lineHeight;
          int levelPlusX = startTimeX + 50; int levelMinusX = levelPlusX + 26;
          if (x >= levelPlusX && x <= levelPlusX + btnW && y >= levelY - 6 && y <= levelY + 6) {
            autoDimLevel = min(autoDimLevel + 5, 100);
            prefs.begin("sys", false); prefs.putInt("autoDimLevel", autoDimLevel); prefs.end();
            drawGraphicsScreen(); delay(150); break;
          }
          if (x >= levelMinusX && x <= levelMinusX + btnW && y >= levelY - 6 && y <= levelY + 6) {
            autoDimLevel = max(autoDimLevel - 5, 0);
            prefs.begin("sys", false); prefs.putInt("autoDimLevel", autoDimLevel); prefs.end();
            drawGraphicsScreen(); delay(150); break;
          }
        }
        break;
      }
    }
  }

// 3. CLOCK AND WEATHER LOGIC
  if (currentState == CLOCK) {
    struct tm ti;
    if (getLocalTime(&ti)) {
      if (ti.tm_sec != lastSec) {
        if (lastSec == -1) {
          tft.fillScreen(getBgColor());
          if (themeMode == 2) fillGradientVertical(0, 0, 320, 240, blueDark, blueLight);
          else if (themeMode == 3) fillGradientVertical(0, 0, 320, 240, yellowDark, yellowLight);
          
          forceClockRedraw = true;
          // --- TADY JE TA OPRAVA ---
          handleNamedayUpdate();
          // Nejdříve zjistíme jméno (uloží se do lokální/statické proměnné v dané funkci)
          // -------------------------

          drawClockFace();
          drawClockStatic();
          if (lastWeatherUpdate == 0 && cityName != "") {
            weatherCity = cityName;
            fetchWeatherData();
            lastWeatherUpdate = millis();
          }

          drawWeatherSection();
          drawDateAndWeek(&ti);
          // Tato funkce si už vezme čerstvá data
          drawSettingsIcon(TFT_SKYBLUE);
          drawWifiIndicator();
          drawUpdateIndicator();
        }

        updateHands(ti.tm_hour, ti.tm_min, ti.tm_sec);
        lastHour = ti.tm_hour; lastMin = ti.tm_min; lastSec = ti.tm_sec;
      }
      
      // Zbytek obsluhy změny dne (tm_mday != lastDay) ponechte jak je.
      if (ti.tm_mday != lastDay) {
        lastDay = ti.tm_mday;
        handleNamedayUpdate(); 
        drawDateAndWeek(&ti);
        drawSettingsIcon(TFT_SKYBLUE);
        drawWifiIndicator();
        drawUpdateIndicator();
      }
    }

    if (millis() - lastWeatherUpdate > 1800000) {
      if (WiFi.status() == WL_CONNECTED && cityName != "") {
        fetchWeatherData();
        drawWeatherSection();
        lastWeatherUpdate = millis();
      }
    }
  }
// OTA verze check (při startu a každých X hodin)
  if (!isUpdating && WiFi.status() == WL_CONNECTED) {
    if (lastVersionCheck == 0 || (millis() - lastVersionCheck > VERSION_CHECK_INTERVAL)) {
      checkForUpdate();

      // Debug: Zobrazíme co jsme načetli
      if (updateAvailable) {
        Serial.println("[OTA] Update check complete:");
        Serial.println("[OTA]   Version: " + availableVersion);
        Serial.println("[OTA]   URL: " + downloadURL);
      }
      
      // Pokud je dostupná aktualizace, vynutíme překreslení ikon
      if (updateAvailable && currentState == CLOCK) {
        drawUpdateIndicator();  // Okamžitě zobrazíme ikonu
      }
      
      // Pokud je dostupná aktualizace a režim je AUTO
      if (updateAvailable && otaInstallMode == 0) {
        Serial.println("[OTA] Auto-update mode - starting update...");
        performOTAUpdate();
      }
    }
  }
  delay(20);
}

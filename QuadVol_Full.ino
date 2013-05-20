/*
 Infrared controlled volume control
*/

#include <IRremote.h>
#include <EEPROM.h>
#include <Metro.h>
#include <LiquidCrystal.h>
#include "mytypes.h"
#include "EEPROMAnything.h"

#define NAME "QualVol"
#define VERSION "0.1"

//// Sparrow hardware
//int LED = 9;           // Sparrow onboard LED
//int CS = 0;            // also used for LCD D4 
//int CLOCK = 13;        // also SPI CLOCK 
//int DATA = 11;         // also SPI MOSI
//int IRDATA = 5;        // IO7 on Sparrow 
//int BUTT_LEARN = 8;    // Button on Sparrow 

// Teensy 2.0 hardware
const int LED = A11;           // onboard LED
const int CS = 8;              
const int CLOCK = 9;          
const int DATA = 10;          
const int IRDATA = 0;         
const int BUTT_LEARN = 11;      
const int LCD_RS = 1;      
const int LCD_EN = 2;
const int LCD_D4 = 18;
const int LCD_D5 = 19;
const int LCD_D6 = 20;
const int LCD_D7 = 21; 


/* 
 * Infrared 
 */
const uint8_t code_len=4;  // learn 4 codes
const uint8_t CODE_UP = 0;
const uint8_t CODE_DOWN = 1;
const uint8_t CODE_MUTE = 2;
const uint8_t CODE_MENU = 3;
const uint8_t CODE_REPEAT = 0x80;

IRrecv irrecv(IRDATA);
decode_results results;
IRCode lastCode = { -1 , 0 };
IRCode code = { -1 , 0 };
IRCode codes[code_len];


/* SPI timing */
int CYCLE = 1; // 1ms clock cycle

/* learn mode */
int learn=0;


/* Volume settings */
uint8_t volume;
uint8_t lastVolume;
uint8_t storedVolume;
uint8_t unmutedVolume;
uint8_t maxVol=254;
uint8_t muted=0;
uint8_t offset[4]; // offsets for all channels

/*
 * Menu 
 */
const uint8_t NO_MENU = 0;
const uint8_t MENU_MAX = 1;
const uint8_t MENU_OFFSET1 = 2;
const uint8_t MENU_OFFSET2 = 3;
const uint8_t MENU_OFFSET3 = 4;
const uint8_t MENU_OFFSET4 = 5;

/* EEPROM */
const int OFFSET_IRCODES=0;
const int OFFSET_VOLUME=20;

/* 2 sec timer to store settings */
int storeTimer = 10000;
int repeatTimer = 300;
Metro storeMetro = Metro(storeTimer); 
Metro flashMetro = Metro(0); 
Metro repeatMetro = Metro(0); 

/* LCD */
LiquidCrystal lcd(LCD_RS,LCD_EN,LCD_D4,LCD_D5,LCD_D6,LCD_D7);

/*
 Progmem helper functions
*/
#define serial_nl Serial.println("")
#define serial_print(x) Serial.print(x)
#define serial_print_dec(x) Serial.print(x,DEC)
#define serial_print_hex(x) Serial.print(x,HEX)
#define serial_println(x) Serial.println(x)
#define serial_print_p(x) SerialPrint_P(PSTR(x))
#define serial_println_p(x) SerialPrint_P(PSTR(x)); Serial.println("");
void SerialPrint_P(PGM_P str) {
  for (uint8_t c; (c = pgm_read_byte(str)); str++) Serial.write(c);
}

#define lcd_print_p(x) LCDPrint_P(PSTR(x))
void LCDPrint_P(PGM_P str) {
  for (uint8_t c; (c = pgm_read_byte(str)); str++) lcd.write(c);
}

void storeVolume() {
  EEPROM.write(OFFSET_VOLUME,volume);
  storedVolume = volume;
}

void displayVolume() {
  lcd.clear();
  lcd.print("Volume: ");
  if (volume==0) {
    lcd.print("Mute");
  } else {
    lcd.print(volume);
  }
  // 
}

// set the volume on all 4 channels to the same level
// 0 = mute
// 255: +31.5db
// note that all signals are inverted!!!
void setVolume(byte vol) {

  // we're using software-SPI here, because the SPI frequency is very low and we have to invert some signals
  digitalWrite(CS, HIGH);  
  delay(CYCLE);

  // set volume 4 times  
  for (byte b=0; b<=4; b++) {
    byte b=vol;
    for (byte bv=0; bv<8; bv++) {
      digitalWrite(CLOCK,HIGH);
      if (b & 0x80) {
        digitalWrite(DATA,LOW);
      } else {
        digitalWrite(DATA,HIGH);
      }
      delay(CYCLE);
      digitalWrite(CLOCK,LOW);
      delay(CYCLE);
      b <<= 1;
    }
  }
  
  delay(CYCLE);
  
  // all to low again
  digitalWrite(CS, LOW);
  digitalWrite(DATA, LOW);
  digitalWrite(CLOCK, LOW);
}

byte codeEquals(IRCode c1, IRCode c2) {
  return ((c1.value==c2.value) && (c1.type==c2.type));
}

void readSettingsFromEEPROM() {
  EEPROM_readAnything(OFFSET_IRCODES, codes);
  volume = EEPROM.read(OFFSET_VOLUME);
  if (volume == 0xFF) {
    // EEPROM defaults to 0xFF which is EXTREMELY loud, will ignore this
    volume = 0;
  }
  lastVolume = volume - 1;
  storedVolume = volume;
}


// Stores the code for later playback
// Most of this code is just logging
byte storeCode(decode_results *results, byte codenum) {
  code.type = results->decode_type;
  code.value = results->value;
  
  lcd.setCursor(0,1);

  if (code.type == UNKNOWN) {
    lcd.print("unknown ");
    return 0;
  }
  else {
    if (code.type == NEC) {
      if (results->value == REPEAT) {
        // Don't record a NEC repeat value as that's useless.
        serial_print_p("repeat ");
        return 0;
      }
      lcd.print("NEC: ");
    } 
    else if (code.type == SONY) {
      lcd.print("SONY: ");
    } 
    else if (code.type == RC5) {
      lcd.print("RC5: ");
    } 
    else if (code.type == RC6) {
      lcd.print("RC6: ");
    } 
    else {
      lcd.print("unknown");
    }
    lcd.print(results->value,16);
    if ((code.value == lastCode.value) && (code.type == lastCode.type)) {
      // same button pressed, ignore
      serial_print_p("Same IR code received, ignoring");
      return 0;
    }
  }
  
  // store code in RAM
  lastCode.value = code.value;
  lastCode.type = code.type;
  codes[codenum].value = code.value;
  codes[codenum].type = code.type;
  return 1;
}

short findCode(IRCode c) {
  for (byte b=0; b<code_len;b++) {
    if(codeEquals(c,codes[b]))
      return b;
  }
  return -1;
}

void flashLED(int millis) {
  digitalWrite(LED,HIGH);
  flashMetro.interval(millis);
  flashMetro.reset();
}





void setup() {
  
  // start serial
  Serial.begin(9600);
  serial_print_p(NAME);
  serial_print_p(VERSION);

  // start LCD
  lcd.begin(16,2);
  lcd_print_p("QuadVol ");
  lcd_print_p(VERSION);
  lcd.setCursor(0,1);
  lcd_print_p("initialising");
  
  // initialize outputs
  pinMode(LED, OUTPUT);     
  pinMode(CLOCK,OUTPUT);
  pinMode(CS, OUTPUT);
  pinMode(DATA, OUTPUT);
  
  // initialize IR receiver
  irrecv.enableIRIn(); // Start the receiver
  
  // load data from EEPROM
  readSettingsFromEEPROM();
  
  // learn button
  pinMode(BUTT_LEARN, INPUT_PULLUP);
  
  delay(2000);
}


short keyCode = -1;
short lastKeyCode = -1;

void loop() {
  
  if ((learn==0) && (digitalRead(BUTT_LEARN)==0)) {
     learn=1;
     lastCode.value=0;
     lcd.clear();
     lcd.print("Learning: UP");
  }

  if (irrecv.decode(&results)) {
    // Serial.println(results.value, HEX);
    
    flashLED(100);
    
    // learn mode
    if (learn) {

       if (storeCode(&results,learn-1)) {
         serial_print_p("Code stored for ");
         serial_print_dec(learn-1);

         learn++;
         
         lcd.setCursor(10,0);
         switch (learn) {
           case 2:  lcd_print_p("DOWN"); break;
           case 3:  lcd_print_p("MUTE"); break;
           case 4:  lcd_print_p("MENU"); break;
         }
       }
      
       if (learn>code_len) {
         learn=0;
         // wait until learn button unpressed again
         lcd.clear();
         while (digitalRead(BUTT_LEARN)==0) { 
           lcd_print_p("Release");
          lcd.setCursor(0,1);
          lcd_print_p("learn button !");
           delay(500);
           lcd.clear();
           delay(500);
         }
        EEPROM_writeAnything(OFFSET_IRCODES, codes);
        lcd_print_p("Finished IR");
        lcd.setCursor(0,1);
        lcd_print_p("learning");
        delay(1000);
        displayVolume();
       }
       
    } else {
    // use IR code
      code.value=results.value;
      code.type=results.decode_type;
      keyCode=findCode(code);
      if (keyCode != -1) {
         serial_print_p("Key pressed: ");
         serial_print(keyCode);
         lastKeyCode=keyCode;
         repeatMetro.interval(repeatTimer);
         repeatMetro.reset();
      } else if ((code.type == NEC)  && (code.value == REPEAT)) {
         serial_print_p("Repeat");
         if (repeatMetro.check()) {
           serial_print_p("Repeating");
           keyCode = lastKeyCode;
           repeatMetro.interval(10);
           repeatMetro.reset();
         }
      } else {
        serial_print("Unknown key :");
        serial_print_hex(results.value);
      }
      
      
      switch (keyCode) {
        case CODE_UP:    serial_println_p("Vol +");
                         if ((volume<254) && (! muted)) volume++;
                         break;
        case CODE_DOWN:  serial_println_p("Vol -");
                         if ((volume > 0) && (! muted)) volume--;
                         break;
        case CODE_MUTE:  serial_println_p("Muting");
                         muted = ! muted;
                         if (muted) {
                           unmutedVolume=volume;
                           volume=0;
                         } else {
                           volume=unmutedVolume;
                         }
                         serial_print_dec(muted);
                         lastVolume=volume-1; // to signal, that the volume has been changed
                         break;
      }
    }
 
    irrecv.resume(); // Receive the next value
  }
  
  // has the volume changed?
  if (volume != lastVolume) {
    setVolume(volume);
    lastVolume = volume;
    displayVolume();
  }
  
  // store to EEPROM?
  if ((storeMetro.check()) && (volume != storedVolume)) {
    lcd.setCursor(15,0);
    lcd.print("*");
    serial_print_p("Storing volume: ");
    serial_println(volume);
    storeVolume();
    storeMetro.reset();
    flashLED(1000);
  }
  
  // turn LED off
  if (flashMetro.check()) {
    digitalWrite(LED,LOW);
  }
  
  keyCode = -1;

}

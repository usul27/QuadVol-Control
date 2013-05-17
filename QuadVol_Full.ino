/*
 Infrared controlled volume control
*/

#include <IRremote.h>
#include <EEPROM.h>
#include <Metro.h>
#include "mytypes.h"
#include "EEPROMAnything.h"

int LED = 11;       // Teensy 2.0 LED
int CS = 0;         // Teensy 2.0 B0
int CLOCK = 1;      // Teensy 2.0 B1
int DATA = 2;       // Teensy 2.0 B2
int IRDATA = 3;     // Teensy 2.0 B3
int BUTT_LEARN = 4; // Teensy 2.0


int CODE_UP = 0;
int CODE_DOWN = 1;
int CODE_REPEAT = 2;

/* SPI timing */
int CYCLE = 1; // 1ms clock cycle

/* learn mode */
int learn=0;
const int code_len=2;  // learn 2 codes

/* Infrared */
IRrecv irrecv(IRDATA);
decode_results results;
IRCode lastCode = { -1 , 0 };
IRCode code = { -1 , 0 };
IRCode codes[code_len];
byte ir_up=0;
byte ir_down=1;

/* Volume settings */
byte volume;
byte lastVolume;
byte storedVolume;

/* EEPROM */
int OFFSET_IRCODES=0;
int OFFSET_VOLUME=20;

/* 2 sec timer to store settings */
int storeTimer = 2000;
int repeatTimer = 300;
Metro storeMetro = Metro(storeTimer); 
Metro flashMetro = Metro(0); 
Metro repeatMetro = Metro(0); 


void setup() {                
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
    
  Serial.begin(9600);
}

void storeVolume() {
  EEPROM.write(OFFSET_VOLUME,volume);
  storedVolume = volume;
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

  if (code.type == UNKNOWN) {
    Serial.println("Received unknown code, ignoring");
    return 0;
  }
  else {
    if (code.type == NEC) {
      Serial.print("Received NEC: ");
      if (results->value == REPEAT) {
        // Don't record a NEC repeat value as that's useless.
        Serial.println("repeat; ignoring.");
        return 0;
      }
    } 
    else if (code.type == SONY) {
      Serial.print("Received SONY: ");
    } 
    else if (code.type == RC5) {
      Serial.print("Received RC5: ");
    } 
    else if (code.type == RC6) {
      Serial.print("Received RC6: ");
    } 
    else {
      Serial.print("Unexpected codeType ");
      Serial.print(code.type, DEC);
      Serial.println("");
    }
    Serial.println(results->value, HEX);
    if ((code.value == lastCode.value) && (code.type == lastCode.type)) {
      // same button pressed, ignore
      Serial.println("Same IR code received, ignoring");
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


short keyCode = -1;
short lastKeyCode = -1;

void loop() {
  
  if ((learn==0) && (digitalRead(BUTT_LEARN)==0)) {
     learn=1;
     lastCode.value=0;
     Serial.print("Starting IR learning");
  }

  if (irrecv.decode(&results)) {
    // Serial.println(results.value, HEX);
    
    // learn mode
    if (learn) {
       if (storeCode(&results,learn-1)) {
         learn++;
       }
       if (learn>code_len) {
         learn=0;
         // wait until learn button unpressed again
         while (digitalRead(BUTT_LEARN)==0) delay(1);
        EEPROM_writeAnything(OFFSET_IRCODES, codes);
       }
    } else {
    // use IR code
      code.value=results.value;
      code.type=results.decode_type;
      keyCode=findCode(code);
      if (keyCode != -1) {
         Serial.print("Key pressed: ");
         Serial.println(keyCode);
         lastKeyCode=keyCode;
         repeatMetro.interval(repeatTimer);
         repeatMetro.reset();
      } else if ((code.type == NEC)  && (code.value == REPEAT)) {
         Serial.println("Repeat");
         if (repeatMetro.check()) {
           Serial.print("Repeating");
           keyCode = lastKeyCode;
           repeatMetro.interval(10);
           repeatMetro.reset();
         }
      } else {
        Serial.print("Unknown key :");
        Serial.println(results.value, HEX);
      }
    }
    
    if (keyCode==CODE_UP) {
      Serial.println("Vol +");
      volume++;
    } else if (keyCode==CODE_DOWN) {
      Serial.println("Vol -");
      volume--;
    }
 
    irrecv.resume(); // Receive the next value
  }
  
  // has the volume changed?
  if (volume != lastVolume) {
    setVolume(volume);
    lastVolume = volume;
    Serial.print("Volume: ");
    Serial.println(volume);
  }
  
  // store to EEPROM?
  if ((storeMetro.check()) && (volume != storedVolume)) {
    Serial.print("Storing volume: ");
    Serial.println(volume);
    storeVolume();
    storeMetro.reset();
    flashLED(1000);
  }
  
  // turn LED off
  if (flashMetro.check()) {
    digitalWrite(LED,LOW);
  }
  
  keyCode = -1;
  
  delay(10);
}

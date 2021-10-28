#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>
#include <avr/wdt.h> 

#define PN532_IRQ   (2)
#define PN532_RESET (3)

SoftwareSerial fingerSerial(5, 4);

Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerSerial);

// variable status
int state = 0;
int stateFingerprint = 0;
int stateAlarm = 0;
int userId = 0;
int id = 0;

// pin initiate
int pinPower = 6;
int pinStarter = 7;
int pinAlarm = 8;
int pinSelenoid = 9;
int pinAntiTheft = 10;

// id card  
String userKey = "119372152";
String userKeyReg = "4931404214992128";
String userKeyUnReg = "10616117821";

// millis
unsigned long previousAlarmMillis = 0;
unsigned long periodAlarm = 60000;
unsigned long previousMillis = 0;
unsigned long period = 10000;

void setup() {
  fingerSerial.begin(9600);
  Serial.begin(115200);
  while (!Serial) delay(10); // for Leonardo/Micro/Zero
  while (!fingerSerial) delay(10);  // for Yun/Leo/Micro/Zero/...

  delay(2000);

  state = EEPROM.read(1);

  initiatePin();
  initiateNfc();
  initiateFingerprint();

  wdt_enable(WDTO_4S);
}

void loop() {
  readNfc();

  switch(stateFingerprint) {
    case 0:
      readFingerprint();
    break;
    case 1:
      id = finger.templateCount + 1;
      finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_PURPLE);
      while(!enrollFingerprint());
      stateFingerprint = 0;
    break;
    case 2:
      finger.emptyDatabase();
      stateFingerprint = 0;
      finger.LEDcontrol(FINGERPRINT_LED_GRADUAL_ON, 200, FINGERPRINT_LED_RED);
      delay(2000);
      finger.LEDcontrol(FINGERPRINT_LED_GRADUAL_OFF, 200, FINGERPRINT_LED_RED);
      delay(2000);
    break;
    default:
    break;
  }

  if(!digitalRead(pinAntiTheft) && !state && !stateAlarm) {
    previousAlarmMillis = millis();
    stateAlarm = 1;
    logicState();
  }

  if (millis() - previousAlarmMillis >= periodAlarm && stateAlarm == 1) {
    stateAlarm = 0;
    digitalWrite(pinAlarm, HIGH);
  }

  if (millis() - previousMillis >= period && state == 1) {
    state = 2;
  }

  wdt_reset();
}

void initiatePin() {
  pinMode(pinPower, OUTPUT);
  pinMode(pinStarter, OUTPUT);
  pinMode(pinAlarm, OUTPUT);
  pinMode(pinSelenoid, OUTPUT);
  pinMode(pinAntiTheft, INPUT_PULLUP);

  digitalWrite(pinPower, EEPROM.read(pinPower));
  digitalWrite(pinSelenoid, EEPROM.read(pinSelenoid));
  digitalWrite(pinStarter, HIGH);
  digitalWrite(pinAlarm, HIGH);
}

void initiateNfc() {
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
    Serial.print("Didn't find PN53x board");
    while (1); // halt
  }

  // Got ok data, print it out!
  Serial.print("Found chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX); 
  Serial.print("Firmware ver. "); Serial.print((versiondata>>16) & 0xFF, DEC); 
  Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);

  nfc.setPassiveActivationRetries(0xFF);

  nfc.SAMConfig();

  Serial.println("Waiting for an ISO14443A card");
}

void initiateFingerprint() {
  finger.begin(57600);
  delay(5);
  if (finger.verifyPassword()) {
    Serial.println("Found fingerprint sensor!");
  } else {
    Serial.println("Did not find fingerprint sensor :(");
    while (1) { delay(1); }
  }

  Serial.println(F("Reading sensor parameters"));
  finger.getParameters();
  Serial.print(F("Status: 0x")); Serial.println(finger.status_reg, HEX);
  Serial.print(F("Sys ID: 0x")); Serial.println(finger.system_id, HEX);
  Serial.print(F("Capacity: ")); Serial.println(finger.capacity);
  Serial.print(F("Security level: ")); Serial.println(finger.security_level);
  Serial.print(F("Device address: ")); Serial.println(finger.device_addr, HEX);
  Serial.print(F("Packet len: ")); Serial.println(finger.packet_len);
  Serial.print(F("Baud rate: ")); Serial.println(finger.baud_rate);

  finger.getTemplateCount();

  if (finger.templateCount == 0) {
    Serial.print("Sensor doesn't contain any fingerprint data. Please run the 'enroll' example.");
  } else {
    Serial.println("Waiting for valid finger...");
    Serial.print("Sensor contains "); Serial.print(finger.templateCount); Serial.println(" templates");
  }
}

void readNfc() {
  boolean success;
  String resultId;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;

  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength, 250);
  
  if (success) {
    Serial.println("Found a card!");
    Serial.print("UID Length: ");Serial.print(uidLength, DEC);Serial.println(" bytes");
    Serial.print("UID Value: ");
    for (uint8_t i=0; i < uidLength; i++) 
    {
      resultId += uid[i];
      Serial.print(" 0x");Serial.print(uid[i], HEX); 
    }

    Serial.println("");

    if (resultId == userKey) {
      state += 1;
    } else if (resultId == userKeyReg) {
      stateFingerprint = 1;
    } else if (resultId == userKeyUnReg) {
      stateFingerprint = 2;
    }

    logicState();
    // Wait 1 second before continuing
    delay(1000);
  }
}

uint8_t readFingerprint() {
  uint8_t p = finger.getImage();
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      Serial.println("No finger detected");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      return p;
    case FINGERPRINT_IMAGEFAIL:
      Serial.println("Imaging error");
      return p;
    default:
      Serial.println("Unknown error");
      return p;
  }

  // OK success!

  p = finger.image2Tz();
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      return p;
    default:
      Serial.println("Unknown error");
      return p;
  }

  // OK converted!
  p = finger.fingerSearch();
  if (p == FINGERPRINT_OK) {
    Serial.println("Found a print match!");
    state += 1;
    logicState();
    delay(1000);
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    return p;
  } else if (p == FINGERPRINT_NOTFOUND) {
    Serial.println("Did not find a match");
    finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 25, FINGERPRINT_LED_RED, 8);
    delay(250);
    return p;
  } else {
    Serial.println("Unknown error");
    return p;
  }

  // found a match!
  Serial.print("Found ID #"); Serial.print(finger.fingerID);
  Serial.print(" with confidence of "); Serial.println(finger.confidence);

  return finger.fingerID;
}

uint8_t enrollFingerprint() {
  int p = -1;
  Serial.print("Waiting for valid finger to enroll as #"); Serial.println(id);
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      Serial.println(".");
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      break;
    case FINGERPRINT_IMAGEFAIL:
      Serial.println("Imaging error");
      break;
    default:
      Serial.println("Unknown error");
      break;
    }
  }

  // OK success!

  p = finger.image2Tz(1);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      return p;
    default:
      Serial.println("Unknown error");
      return p;
  }

  Serial.println("Remove finger");
  finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, FINGERPRINT_LED_PURPLE);
  delay(2000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }
  Serial.print("ID "); Serial.println(id);
  p = -1;
  Serial.println("Place same finger again");
  finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_PURPLE);
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      Serial.print(".");
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      break;
    case FINGERPRINT_IMAGEFAIL:
      Serial.println("Imaging error");
      break;
    default:
      Serial.println("Unknown error");
      break;
    }
  }

  // OK success!

  p = finger.image2Tz(2);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      return p;
    default:
      Serial.println("Unknown error");
      return p;
  }

  // OK converted!
  Serial.print("Creating model for #");  Serial.println(id);

  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    Serial.println("Prints matched!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    return p;
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
    Serial.println("Fingerprints did not match");
    return p;
  } else {
    Serial.println("Unknown error");
    return p;
  }

  Serial.print("ID "); Serial.println(id);
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    Serial.println("Stored!");
    finger.LEDcontrol(FINGERPRINT_LED_GRADUAL_ON, 200, FINGERPRINT_LED_BLUE);
    delay(2000);
    finger.LEDcontrol(FINGERPRINT_LED_GRADUAL_OFF, 200, FINGERPRINT_LED_BLUE);
    delay(2000);
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    return p;
  } else if (p == FINGERPRINT_BADLOCATION) {
    Serial.println("Could not store in that location");
    return p;
  } else if (p == FINGERPRINT_FLASHERR) {
    Serial.println("Error writing to flash");
    return p;
  } else {
    Serial.println("Unknown error");
    return p;
  }

  return true;
}

void logicState() {
  EEPROM.write(1, state);
  switch (state) {
    case 1:
      if (stateAlarm) {
        state = 0;
        stateAlarm = 0;
        digitalWrite(pinAlarm, HIGH);
      } else {
        previousMillis = millis();
        finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 3);
        digitalWrite(pinPower, LOW);
        digitalWrite(pinSelenoid, LOW);
        EEPROM.write(pinPower, 0);
        EEPROM.write(pinSelenoid, 0);
      }
    break;
    case 2:
      finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 3);
      digitalWrite(pinStarter, LOW);
      delay(1500);
      digitalWrite(pinStarter, HIGH);
    break;
    case 3:
      finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 3);
      digitalWrite(pinPower, HIGH);
      digitalWrite(pinSelenoid, HIGH);
      EEPROM.write(pinPower, 1);
      EEPROM.write(pinSelenoid, 1);
      state = 0;
    break;
    default:
      if (state > 3) {
        state = 0;
      }

      if (state == 0 && stateAlarm == 1) {
        digitalWrite(pinAlarm, LOW);
      }
    break;
  }
}

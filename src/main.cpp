#include <Arduino.h>
#include <Adafruit_Fingerprint.h>

// Hardware Pin Mapping
#define FINGERPRINT_RX_PIN 32
#define FINGERPRINT_TX_PIN 33
#define SOLENOID_LOCK_PIN 26
#define BUZZER_PIN 14
#define PIR_MOTION_PIN 27
#define VIBRATION_SENSOR_PIN 25

// Constants
#define FINGERPRINT_BAUDRATE 57600
#define UNLOCK_DURATION 5000  // 5 seconds in milliseconds
#define ALARM_THRESHOLD 3     // Number of consecutive failures before alarm
#define BUZZER_WARNING_DURATION 500  // Buzzer warning duration in ms
#define BUZZER_ALARM_DURATION 2000   // Buzzer alarm duration in ms

// Global Variables
HardwareSerial FingerprintSerial(2);  // UART2 for fingerprint sensor
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&FingerprintSerial);

volatile uint8_t failureCount = 0;
volatile bool lockEngaged = true;
volatile bool alarmTriggered = false;
volatile bool motionDetected = false;
volatile bool vibrationDetected = false;
unsigned long unlockStartTime = 0;

// Function Declarations
void initializeHardware();
void enrollFingerprint();
void matchFingerprint();
void triggerBuzzer(uint16_t duration);
void unlockDoor();
void lockDoor();
void checkMotionSensor();
void checkVibrationSensor();
void handleTamper();
void printMenuOptions();
uint8_t getFingerprintID();
uint8_t getFingerprintEnroll(uint8_t id);

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n\n================================");
  Serial.println("Guardian AI Door Lock System");
  Serial.println("================================\n");
  
  initializeHardware();
  
  FingerprintSerial.begin(FINGERPRINT_BAUDRATE, SERIAL_8N1, FINGERPRINT_RX_PIN, FINGERPRINT_TX_PIN);
  delay(100);
  
  if (finger.verifyPassword()) {
    Serial.println("[OK] Fingerprint sensor initialized successfully");
  } else {
    Serial.println("[ERROR] Could not communicate with fingerprint sensor");
    while (1) { delay(1); }
  }
  
  Serial.print("Fingerprint sensor library version: ");
  Serial.println(finger.getFirmwareVersion());
  
  Serial.print("Number of fingerprints stored: ");
  Serial.println(finger.templateCount);
  
  printMenuOptions();
  triggerBuzzer(200);
}

void loop() {
  if (Serial.available()) {
    uint8_t command = Serial.read();
    
    if (Serial.available() && Serial.peek() == '\n') {
      Serial.read();
    }
    
    switch (command) {
      case 'E':
      case 'e':
        enrollFingerprint();
        printMenuOptions();
        break;
        
      case 'M':
      case 'm':
        matchFingerprint();
        printMenuOptions();
        break;
        
      case 'H':
      case 'h':
        printMenuOptions();
        break;
        
      case 'S':
      case 's':
        Serial.println("\n[INFO] System Status:");
        Serial.print("  Lock Status: ");
        Serial.println(lockEngaged ? "LOCKED" : "UNLOCKED");
        Serial.print("  Failure Count: ");
        Serial.println(failureCount);
        Serial.print("  Alarm Triggered: ");
        Serial.println(alarmTriggered ? "YES" : "NO");
        Serial.print("  Motion Detected: ");
        Serial.println(motionDetected ? "YES" : "NO");
        Serial.print("  Vibration Detected: ");
        Serial.println(vibrationDetected ? "YES" : "NO");
        Serial.print("  Stored Fingerprints: ");
        Serial.println(finger.templateCount);
        Serial.println();
        printMenuOptions();
        break;
        
      default:
        Serial.println("[WARNING] Invalid command. Type 'H' for help.");
        printMenuOptions();
        break;
    }
  }
  
  checkMotionSensor();
  checkVibrationSensor();
  
  if (motionDetected || vibrationDetected) {
    if (!alarmTriggered) {
      handleTamper();
    }
  }
  
  if (!lockEngaged && (millis() - unlockStartTime >= UNLOCK_DURATION)) {
    lockDoor();
  }
  
  delay(50);
}

void initializeHardware() {
  pinMode(SOLENOID_LOCK_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PIR_MOTION_PIN, INPUT);
  pinMode(VIBRATION_SENSOR_PIN, INPUT);
  
  digitalWrite(SOLENOID_LOCK_PIN, HIGH);
  digitalWrite(BUZZER_PIN, LOW);
  
  Serial.println("[OK] Hardware pins configured");
}

void printMenuOptions() {
  Serial.println("\n========== MENU OPTIONS ==========");
  Serial.println("E - Enroll new fingerprint");
  Serial.println("M - Match/identify fingerprint");
  Serial.println("S - System status");
  Serial.println("H - Show this menu");
  Serial.println("==================================\n");
}

void enrollFingerprint() {
  Serial.println("\n[ENROLL] Fingerprint enrollment mode");
  Serial.print("Enter fingerprint ID (1-127): ");
  
  while (!Serial.available()) {
    delay(100);
  }
  
  uint8_t id = Serial.parseInt();
  
  if (Serial.available() && Serial.peek() == '\n') {
    Serial.read();
  }
  
  if (id < 1 || id > 127) {
    Serial.println("[ERROR] Invalid ID. Must be between 1-127");
    return;
  }
  
  Serial.print("Enrolling ID #");
  Serial.println(id);
  
  uint8_t result = getFingerprintEnroll(id);
  
  if (result == FINGERPRINT_OK) {
    Serial.print("[SUCCESS] Fingerprint ID #");
    Serial.print(id);
    Serial.println(" enrolled successfully");
    triggerBuzzer(300);
  } else {
    Serial.println("[ERROR] Enrollment failed");
    triggerBuzzer(100);
    delay(100);
    triggerBuzzer(100);
  }
}

void matchFingerprint() {
  Serial.println("\n[MATCH] Waiting for fingerprint...");
  triggerBuzzer(100);
  
  uint8_t result = getFingerprintID();
  
  if (result == FINGERPRINT_OK) {
    Serial.println("[SUCCESS] Fingerprint matched!");
    failureCount = 0;
    alarmTriggered = false;
    triggerBuzzer(500);
    delay(500);
    triggerBuzzer(500);
    unlockDoor();
  } else if (result == FINGERPRINT_NOTFOUND) {
    failureCount++;
    Serial.print("[FAILED] No match found. Failures: ");
    Serial.print(failureCount);
    Serial.print("/");
    Serial.println(ALARM_THRESHOLD);
    
    triggerBuzzer(BUZZER_WARNING_DURATION);
    
    if (failureCount >= ALARM_THRESHOLD) {
      Serial.println("[ALARM] Multiple failed attempts detected!");
      alarmTriggered = true;
      handleTamper();
    }
  } else {
    Serial.println("[ERROR] Communication error with fingerprint sensor");
  }
}

uint8_t getFingerprintID() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return p;
  
  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return p;
  
  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    Serial.print("Found ID #");
    Serial.print(finger.fingerID);
    Serial.print(" with confidence ");
    Serial.println(finger.confidence);
    return FINGERPRINT_OK;
  } else if (p == FINGERPRINT_NOTFOUND) {
    Serial.println("No matching fingerprint found");
    return FINGERPRINT_NOTFOUND;
  } else {
    Serial.println("Communication error");
    return p;
  }
}

uint8_t getFingerprintEnroll(uint8_t id) {
  int p = -1;
  Serial.print("Waiting for valid finger to enroll as #");
  Serial.println(id);
  
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_OK) {
      Serial.println("Image taken");
    } else if (p == FINGERPRINT_PACKETRECIEVEERR || p == FINGERPRINT_IMAGEFAIL) {
      return p;
    }
  }
  
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) return p;
  
  Serial.println("Remove finger");
  delay(2000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }
  
  p = -1;
  Serial.println("Place same finger again");
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_OK) {
      Serial.println("Image taken");
    } else if (p == FINGERPRINT_PACKETRECIEVEERR || p == FINGERPRINT_IMAGEFAIL) {
      return p;
    }
  }
  
  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) return p;
  
  Serial.print("Creating model for #");
  Serial.println(id);
  
  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    Serial.println("Prints matched!");
  } else {
    return p;
  }
  
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    Serial.println("Stored!");
    return FINGERPRINT_OK;
  } else {
    return p;
  }
}

void triggerBuzzer(uint16_t duration) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(duration);
  digitalWrite(BUZZER_PIN, LOW);
}

void unlockDoor() {
  Serial.println("[ACTION] DOOR UNLOCKED - Solenoid relay activated");
  digitalWrite(SOLENOID_LOCK_PIN, LOW);
  lockEngaged = false;
  unlockStartTime = millis();
}

void lockDoor() {
  Serial.println("[ACTION] DOOR LOCKED - Solenoid relay deactivated");
  digitalWrite(SOLENOID_LOCK_PIN, HIGH);
  lockEngaged = true;
  failureCount = 0;
}

void checkMotionSensor() {
  static unsigned long lastMotionTime = 0;
  static bool lastMotionState = false;
  
  bool currentMotionState = digitalRead(PIR_MOTION_PIN);
  
  if (currentMotionState && !lastMotionState) {
    lastMotionTime = millis();
    motionDetected = true;
    Serial.println("[ALERT] Motion detected by PIR sensor!");
  }
  
  if (motionDetected && (millis() - lastMotionTime > 5000)) {
    motionDetected = false;
  }
  
  lastMotionState = currentMotionState;
}

void checkVibrationSensor() {
  static unsigned long lastVibrationTime = 0;
  static bool lastVibrationState = false;
  
  bool currentVibrationState = digitalRead(VIBRATION_SENSOR_PIN);
  
  if (currentVibrationState && !lastVibrationState) {
    lastVibrationTime = millis();
    vibrationDetected = true;
    Serial.println("[ALERT] Vibration detected by SW-420 sensor!");
  }
  
  if (vibrationDetected && (millis() - lastVibrationTime > 5000)) {
    vibrationDetected = false;
  }
  
  lastVibrationState = currentVibrationState;
}

void handleTamper() {
  Serial.println("\n[!!! TAMPER ALERT !!!]");
  
  if (!lockEngaged) {
    lockDoor();
  }
  
  for (int i = 0; i < 5; i++) {
    triggerBuzzer(BUZZER_ALARM_DURATION);
    delay(300);
  }
  
  alarmTriggered = true;
}

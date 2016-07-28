#include <Wire.h>
#include <EEPROM.h>
#include "DS1307.h"
#include <SoftwareSerial.h>

// ****************************************************************************************** //
// ====================================   USER CONFIGURATION   ============================== //
const String  SOLENOID_TIME_CHECK      = "3:10";                            // Time to test solenoid. Format "G:i" 24-h without start zero.
const int     SOLENOID_CLOSED_DURATION = 40000;                             // Duration of solenoid close in seconds
const int     DELAY_CHECK_INTERVAL     = 3000;                              // Detectors interval check in seconds
const String  NOTIFY_PHONE_NUMBER      = "+79036867755";                    // Telephone number to alarm and status target
const int     NOTIFY_TRY               = 3;                                 // Count of notify owner by alarm
const int     NOTIFY_TRY_INTERVAL      = 30000;                             // Interval by notify tries in seconds
const int     NOTIFY_CALL_DURATION     = 15000;                             // Duration of owner telephone ring by alarm in seconds
//const unsigned long SMS_UPTIME_INTERVAL= 2617200000; // 30days+7hours          // Interval of SMS status WaterFlowController in milliseconds
const unsigned long SMS_UPTIME_INTERVAL= 25200000; // +7hours          // Interval of SMS status WaterFlowController in milliseconds
const String  detectorPinExplainList[] = {"Toilet", "BathRoom", "Kitchen"}; // Position of element must be equals at detectorPinList;
// ****************************************************************************************** //

// Pin config
const int detectorPinList[] = {7,6,5};
const int detectorPinCount = 3;
const int powerPin = 4;
const int relayPin = 8;
const int forceCloseButtonPin = 9;
const int gsmTXPin=3;
const int gsmRXPin=2;
const int onboardLedPin=13;

// Defines
DS1307 clock;
char compileTime[] = __TIME__;
SoftwareSerial gsmSerial(gsmTXPin, gsmRXPin);
const int ALARM_NOT_DETECTED = -1;
unsigned long checkInterval;
unsigned long checkIntervalCurrent;
const int gsmPortBusySMSDelay=8000;

// Variables
int waterTapState = 1;
int testSolenoidCount=0;
int forceCloseButtonState = 0;
int forceCloseButtonStateLast = 0;
int forceCloseButtonPushCounter = 0;
int alarmDetect = -1;
boolean needTest=true;

// ****************************************************************************************** //
// =====================================   INITIALIZE   ===================================== //

void setup() 
{
  // Onboard led
  pinMode(onboardLedPin, OUTPUT);

  // Force close water switch pin
  pinMode(forceCloseButtonPin, INPUT);

  // Relay Pin
  pinMode(relayPin, OUTPUT);

  closeWater();

  // Init detecors pin list
  initDetectorPin();

  // Init clock
  initClock();

  // Init SMSShield
  initSMSShield();

  openWater();
}

void initDetectorPin()
{
  // Power for Detectors
  pinMode(powerPin, OUTPUT);
  // Detectors inputs
  for (int i = 0; i < detectorPinCount; i++) {
    pinMode(detectorPinList[i], INPUT); 
  }
}

/**
 * @fixme
 */
void initClock()
{
  // Begin RTC
  clock.begin();

  // Получаем число из строки, зная номер первого символа
  byte hour = getInt(compileTime, 0);
  byte minute = getInt(compileTime, 3);
  byte second = getInt(compileTime, 6);

  //Импровизированный хэш времени
  //Содержит в себе количество секунд с начала дня
  unsigned int hash =  hour * 60 * 60 + minute  * 60 + second; 
 
  //Проверяем несовпадение нового хэша с хэшем в EEPROM
  if (EEPROMReadInt(0) != hash) {
 
    //Сохраняем новый хэш
    EEPROMWriteInt(0, hash);
 
    //Готовим для записи в RTC часы, минуты, секунды
    clock.fillByHMS(hour, minute, second);
    String timeNow="";
    timeNow += clock.hour;
    timeNow += ":";
    timeNow += clock.minute;

    //Записываем эти данные во внутреннюю память часов.
    //С этого момента они начинают считать нужное для нас время
    clock.setTime();
  }
  
}

void initSMSShield()
{
  delay(5000);  //время на инициализацию модуля
  gsmSerial.begin(115200);
  delay(100);
  gsmSerial.println("AT+CMGF=1");  // Coding for SMS
  delay(100);
  gsmSerial.println("AT+CSCS=\"GSM\"");  // Coding for text
  delay(100);
  gsmSerial.println("AT+CMGDA=\"DEL ALL\""); // Delete all sms for free mem
  // Send start sms init
  delay(10000);
  String message = "WaterFlowTap started. BuildVersion:"+String(__DATE__) + " " +String(__TIME__);
  sendSMS(NOTIFY_PHONE_NUMBER, message);
}



// ****************************************************************************************** //
// ====================================   NOTIFICATION   ==================================== //

void sendUptimeSMS()
{
  clock.getTime();
  String timeNow="";
  timeNow += clock.hour;
  timeNow += ":";
  timeNow += clock.minute;
  timeNow += ":";
  timeNow += clock.second;

  unsigned long uptime=millis();
  //int uptime=millis();
  float uptimeDays=uptime / 1000 / 24 / 60 / 60;

  String message=String("Status from WaterFlowTap: -uptimeDays:") + String(uptimeDays) + String("(") + String(uptime) + String(")") + String(",-testSolenoidCount:") + String(testSolenoidCount) + String(",now:") + timeNow;
  sendSMS(NOTIFY_PHONE_NUMBER, message);
  delay(gsmPortBusySMSDelay);
}

void notifyOwner(int detectorPin)
{
  // send SMS
  String message=String("Alaram by " + String(explainDetectorPin(detectorPin)) + String(" detector (pin#" + String(detectorPin) + String(")")));
  sendSMS(NOTIFY_PHONE_NUMBER, message);
  delay(gsmPortBusySMSDelay);

  // Call to phone
  call(NOTIFY_PHONE_NUMBER, NOTIFY_CALL_DURATION);
}

void notifyOwnerBatch(int detectorPin, int tryCount=NOTIFY_TRY, int interval=NOTIFY_TRY_INTERVAL)
{
  int i=0;
  while (i < NOTIFY_TRY) {
    notifyOwner(detectorPin);
    delay(NOTIFY_TRY_INTERVAL);
    i++;
  }
}

/**
 * Send text sms to phone
 */
void sendSMS(String phone, String text)
{
  gsmSerial.println("AT+CMGS=\"" + phone + "\"");
  delay(100);
  gsmSerial.print(text);
  delay(100);
  gsmSerial.print((char)26);
  delay(100);
}

/**
 * Call to phone with duration
 */
void call(String phone, int duration)
{
  gsmSerial.println("ATD=" + phone + ";");
  delay(100);
  delay(duration);
  gsmSerial.println("ATH0");
  delay(500);
}

/**
 * Set alram by detector pin
 */
void setAlarm(int detectorPin=ALARM_NOT_DETECTED)
{
  if (detectorPin != ALARM_NOT_DETECTED) {
    alarmDetect=detectorPin;
    notifyOwnerBatch(detectorPin, NOTIFY_TRY, NOTIFY_TRY_INTERVAL);
  }
  else {
    alarmDetect=ALARM_NOT_DETECTED;
    openWater();
  }
}

// ****************************************************************************************** //
// ====================================   TEST SOLENOID   =================================== //

/**
 * @todo
 */
boolean checkForceCloseState() {
  /*
  forceCloseButtonState = digitalRead(forceCloseButtonPin);
  if (forceCloseButtonState != forceCloseButtonStateLast) {
    if (forceCloseButtonState == LOW) {
      forceCloseButtonPushCounter++;
            Serial.println("on");
      Serial.print("number of button pushes:  ");
      Serial.println(forceCloseButtonPushCounter);

    }
    else {
      Serial.println("off");
    }
  }
  forceCloseButtonStateLast = forceCloseButtonState;
  if (forceCloseButtonPushCounter % 2 == 1) {
    digitalWrite(onboardLedPin, HIGH);
  }
  else {
    digitalWrite(onboardLedPin, LOW);
  }
  // */
  return false;
}

/**
 * Check time for SMS status log send
 */
boolean checkTimeToUptimeSMSSend()
{
  unsigned long uptimeSec=millis();
  if ((uptimeSec > 0) && (uptimeSec >= SMS_UPTIME_INTERVAL)) {
    return true;
  }
  return false;
}

/** 
 * Check condition for solenoid test
 */
boolean checkTimeToSolenoidTest()
{
  clock.getTime();
  String timeNow="";
  timeNow += clock.hour;
  timeNow += ":";
  timeNow += clock.minute;
  if (needTest && timeNow==SOLENOID_TIME_CHECK) {
    needTest=false;
    return true;
  }
  else if (!needTest && timeNow!=SOLENOID_TIME_CHECK) {
    needTest=true;
  }
  return false;
}

/**
 * Procedure for test solenoid.
 * It's close and open solenoid by SOLENOID_CLOSED_DURATION
 */
void testWaterSolenoid()
{
  if (alarmDetect != ALARM_NOT_DETECTED) {
    return;
  }
  closeWater();
  testSolenoidCount++;
  delay(SOLENOID_CLOSED_DURATION);
  openWater();
}

// ****************************************************************************************** //
// ======================================   HELPERS   ======================================= //

/**
 * Restart Arduino controller function
 */
void(* restartController) (void) = 0;

/**
 * Returns human readable String of detectorPin
 */
String explainDetectorPin(int detectorPin)
{
  for (int i = 0; i < detectorPinCount; i++) {
    if (detectorPinList[i]==detectorPin) {
      return detectorPinExplainList[i];
    }
  }
  return String("Empty detector");
}

/** 
 * For RTC time explain 
 */
char getInt(const char* string, int startIndex) {
  return int(string[startIndex] - '0') * 10 + int(string[startIndex+1]) - '0';
}

/**
 * Write two bytes in EEPROM
 */
void EEPROMWriteInt(int address, int value)
{
  EEPROM.write(address, lowByte(value));
  EEPROM.write(address + 1, highByte(value));
}
 
/**
 * Read from EEPROM
 */
unsigned int EEPROMReadInt(int address)
{
  byte lowByte = EEPROM.read(address);
  byte highByte = EEPROM.read(address + 1);
 
  return (highByte << 8) | lowByte;
}

/**
 * Returns success for interval exists from start arduino
 */
boolean interval(int sec)
{
  checkIntervalCurrent=millis();
  if ((checkIntervalCurrent - checkInterval) > sec) {
    checkInterval=checkIntervalCurrent;
    return true;
  }
  return false;
}

// ****************************************************************************************** //
// ========================================   MAIN   ======================================== //

/**
 * Функция обходит все датчики для проверки. Возвращает идентификатор при сработавшем датчике.
 */
int checkDetectors()
{
  if (alarmDetect != ALARM_NOT_DETECTED) {
    return alarmDetect;
  }

  // Power ON base
  digitalWrite(powerPin, HIGH);
  digitalWrite(onboardLedPin, HIGH);
  delay(100);
  // Loop all detectors for check
  int detectorState=HIGH;
  for (int i = 0; i < detectorPinCount; i++) {
    detectorState=digitalRead(detectorPinList[i]);
    if (detectorState==LOW) {
      digitalWrite(powerPin, LOW);
      digitalWrite(onboardLedPin, LOW);
      return detectorPinList[i];
    }
  }
  // Power Off base
  digitalWrite(powerPin, LOW);
  digitalWrite(onboardLedPin, LOW);

  return ALARM_NOT_DETECTED;
}

/** 
 * Процедура закрывает краны
 */
void closeWater()
{
  if (waterTapState == 0) {
    return;
  }
  waterTapState=0;
  digitalWrite(onboardLedPin, HIGH);
  digitalWrite(relayPin, HIGH);
}

/** 
 * Процедура открывает краны
 */
void openWater()
{
  if (waterTapState == 1) {
    return;
  }
  waterTapState=1;
  digitalWrite(onboardLedPin, LOW);
  digitalWrite(relayPin, LOW); 
}

void loop() 
{
  // @todo Проверяем на состояние принудительного закрытия кранов по кнопке
  //if (checkForceCloseState()) {
  //  closeWater();
  //  return;
  //}

  // Open water if alarm not detected
  if (alarmDetect == ALARM_NOT_DETECTED) {
    openWater();
  }
  else {
    // Alarm detected! Stay water closed.
    closeWater();
    return;
  }

  // Check water flow detectors
  if (interval(DELAY_CHECK_INTERVAL)) {
    int detectorPin = checkDetectors();
    if (detectorPin != ALARM_NOT_DETECTED) {
      closeWater();
      setAlarm(detectorPin);
      return;
    }
  }

  // Send sms log uptime
  if (checkTimeToUptimeSMSSend()) {
    sendUptimeSMS();
    restartController();
  }

  // Solenoid test func
  if (checkTimeToSolenoidTest()) {
    testWaterSolenoid();
  }
}

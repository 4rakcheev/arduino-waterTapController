#include <Wire.h>
#include <SoftwareSerial.h>
#include "RTClib.h"

// ****************************************************************************************** //
// ====================================   USER CONFIGURATION   ============================== //
const String   SOLENOID_TIME_CHECK      = "3:35:5";                           // Time to test solenoid. Format "h:m:s" without lead zero. Ex.: "3:25:0"
const int      SOLENOID_CLOSED_DURATION = 25000;                              // Duration of solenoid close in milliseconds //@fixme dont work with 40000 ms:water not open after close
const int      DELAY_CHECK_INTERVAL     = 3000;                               // Detectors interval check in milliseconds
const String   NOTIFY_PHONE_NUMBER      = "+79036867755";                     // Telephone number to alarm and status target
const int      NOTIFY_TRY               = 2;                                  // Count of notify owner by alarm
const int      NOTIFY_TRY_INTERVAL      = 30000;                              // Interval by notify tries in milliseconds
const int      NOTIFY_CALL_DURATION     = 18000;                              // Duration of owner telephone ring by alarm in milliseconds
const long int SMS_UPTIME_INTERVAL      = 1296000; // 15days                  // Interval of SMS status WaterFlowController in seconds
const String   detectorPinExplainList[] = {"Toilet", "BathRoom", "Kitchen"};  // Position of element must be equals at detectorPinList;
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
RTC_DS1307 rtc;
SoftwareSerial gsmSerial(gsmTXPin, gsmRXPin);
const int ALARM_NOT_DETECTED = -1;
const int gsmPortBusySMSDelay=8000;

// Variables
int waterTapState = 1;
int testSolenoidCount=0;
int forceCloseButtonState = 0;
int forceCloseButtonStateLast = 0;
int forceCloseButtonPushCounter = 0;
int alarmDetect = -1;
long int timestampStart=0;

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

  // Close water by initialize other components
  closeWater();

  // Init detecors pin list
  initDetectorPin();

  // Init clock
  initClock();

  // Init SMSShield
  initSMSShield();

  // Start water on finish initialize
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

void initClock()
{
  if (! rtc.begin()) {
    return;
  }
  
  if (! rtc.isrunning()) {
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Setup start timestamp
  DateTime now = rtc.now();
  timestampStart=now.unixtime();
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
  // Delay for GSM network initialize
  delay(10000);

  // Send status SMS on start controller
  sendPowerOnSMS();
}



// ****************************************************************************************** //
// ====================================   NOTIFICATION   ==================================== //

void sendPowerOnSMS()
{
  String message = "WaterFlowTapController started (buildVersion: "+ getBuildVersion()+")! SystemDateTime: " + getStringDateNow() + " " + getStringTimeNow();
  sendSMS(NOTIFY_PHONE_NUMBER, message);
}

void sendUptimeSMS()
{
  String message = "WaterFlowTapController status: UptimeDays:" + String(getUptimeDays()) + "(" + String(getUptime()) + "s)" + ",TestSolenoidCount:" + String(testSolenoidCount) + ",SysDate:" + getStringDateNow() + " " + getStringTimeNow();
  sendSMS(NOTIFY_PHONE_NUMBER, message);
}

void notifyOwner(int detectorPin)
{
  // send SMS
  String message=String("Alaram by " + String(explainDetectorPin(detectorPin)) + String(" detector (pin#" + String(detectorPin) + String(")")));
  sendSMS(NOTIFY_PHONE_NUMBER, message);

  // Call to phone
  call(NOTIFY_PHONE_NUMBER, NOTIFY_CALL_DURATION);
}

void notifyOwnerBatch(int detectorPin, int tryCount=NOTIFY_TRY, int interval=NOTIFY_TRY_INTERVAL)
{
  int i=0;
  while (i < NOTIFY_TRY) {
    notifyOwner(detectorPin);
    delay
    (NOTIFY_TRY_INTERVAL);
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
  delay(gsmPortBusySMSDelay);
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
  if (!rtc.isrunning()) {
    return false;
  }
  DateTime now = rtc.now();
  if (now.unixtime() % SMS_UPTIME_INTERVAL==0) {
    return true; 
  }
  return false;
}

/** 
 * Check condition for solenoid test
 */
boolean checkTimeToSolenoidTest()
{
  if (getStringTimeNow() == SOLENOID_TIME_CHECK) {
    return true;
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
 * Restart Arduino controller function. Usage: call restartController() 
 */
void(* restartController) (void) = 0;

String getBuildVersion()
{
  return String(__TIME__) + " " + String(__DATE__);
}

/**
 * Returns seconds uptime
 */
int getUptime()
{
  if (!rtc.isrunning() || timestampStart <=0 ) {
    return -1;
  }
  DateTime now = rtc.now();
  return now.unixtime() - timestampStart;
}

/**
 * Returns uptime in days
 */
int getUptimeDays() 
{
  return getUptime() / 24 / 60 / 60 ;
}

/**
 * Generate String Time at now
 */
String getStringTimeNow()
{
  if (!rtc.isrunning()) {
    return "RTC is not running";
  }
  DateTime now = rtc.now();
  return String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second());
}

/**
 * Generate String Date at now
 */
String getStringDateNow()
{
  if (!rtc.isrunning()) {
    return "RTC is not running";
  }
  DateTime now = rtc.now();
  return String(now.year()) + "/" + String(now.month()) + "/" + String(now.day());
}

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
 * Returns success for interval exists from start arduino
 */
boolean interval(int mil)
{
  if (millis()%mil==0) {
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
  }

  // Solenoid test func
  if (checkTimeToSolenoidTest()) {
    testWaterSolenoid();
  }

}

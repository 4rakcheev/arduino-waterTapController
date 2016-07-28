#include <Wire.h>
#include <EEPROM.h>
#include "DS1307.h"
#include <SoftwareSerial.h>

// ****************************************************************************************** //
// ====================================   USER CONFIGURATION   ============================== //
const String  TIME_CHECK            = "03:30";
const int     DELAY_CHECK_INTERVAL  = 3000;
const int     DELAY_TEST_CLOSED     = 40000;
const String  NOTIFY_PHONE_NUMBER   = "+79036867755";
// ****************************************************************************************** //

// Pin config
const int detectorPinList[] = {7,6,5};
const int detectorPinCount = 3;
const String detectorPinExplainList[] = {"Toilet", "BathRoom", "Kitchen"};

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
const int EMPTY_DETECTOR = -1;
unsigned long checkInterval;
unsigned long checkIntervalCurrent;

// Variables
int waterTapState = 1;
int forceCloseButtonState = 0;
int forceCloseButtonStateLast = 0;
int forceCloseButtonPushCounter = 0;
int alarmDetect = -1;
int notified = 0;
boolean needTest=true;


// ****************************************************************************************** //
// ====================================   NOTIFICATION   ==================================== //

/**
 * Процедура уведомления хозяина о сработавшем датчике detector
 */
void notifyOwner(int detector)
{
  String message=String("Alaram by " + String(explainDetector(detector)) + String(" detector (pin#" + String(detector) + String(")")));
  // send SMS
  sendSMS(NOTIFY_PHONE_NUMBER, message);

  // Call to phone
  delay(9000);
  call(NOTIFY_PHONE_NUMBER);

  // Send sms twice
  delay(9000);
  sendSMS(NOTIFY_PHONE_NUMBER, message);

  if (notified == 0) {
    notified = 1;
  }
}

/**
 * Отправляет смс с текстом text на номер phone
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
 * Производит звонок на номер телефона phone
 */
void call(String phone)
{
  gsmSerial.println("ATD=" + phone + ";");
  delay(100);
}

/**
 * Бьет тревогу о сработавшем датчике
 */
void setAlarm(int detector=EMPTY_DETECTOR)
{
  if (detector != EMPTY_DETECTOR) {
    alarmDetect=detector;
    notifyOwner(detector);
  }
  else {
    alarmDetect=EMPTY_DETECTOR;
    openWater();
  }
}

// ****************************************************************************************** //
// ====================================   TEST SOLENOID   =================================== //

boolean checkForceCloseState() {
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
  return false;
}

/** 
 * Проверяет наступило ли время  для проверки кранов
 */
boolean checkTimeToTest()
{
  clock.getTime();
  String timeNow="";
  timeNow += clock.hour;
  timeNow += ":";
  timeNow += clock.minute;
  if (needTest && timeNow==TIME_CHECK) {
    needTest=false;
    return true;
  }
  else if (!needTest && timeNow!=TIME_CHECK) {
    needTest=true;
  }
  return false;
}

/**
 * Процедура тестирование кранов.
 * Закрывает краны без тревоги на время DELAY_TEST_CLOSED
 */
void testWaterTap()
{
  if (alarmDetect != EMPTY_DETECTOR) {
    return;
  }
  if (checkTimeToTest()) {
    closeWater();
    delay(DELAY_TEST_CLOSED);
    openWater();
  }
}

// ****************************************************************************************** //
// ======================================   HELPERS   ======================================= //
/**
 * Возвращает текстовое описание по ключу детектора
 */
String explainDetector(int detector)
{
  for (int i = 0; i < detectorPinCount; i++) {
    if (detectorPinList[i]==detector) {
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
 * Запись двухбайтового числа в память
 */
void EEPROMWriteInt(int address, int value)
{
  EEPROM.write(address, lowByte(value));
  EEPROM.write(address + 1, highByte(value));
}
 
/**
 * Чтение числа из памяти
 */
unsigned int EEPROMReadInt(int address)
{
  byte lowByte = EEPROM.read(address);
  byte highByte = EEPROM.read(address + 1);
 
  return (highByte << 8) | lowByte;
}

/**
 * Возвращает успешность наступления окончания интервала
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

void reset()
{
  setAlarm(EMPTY_DETECTOR);
}

// ****************************************************************************************** //
// =====================================   INITIALIZE   ===================================== //
void initSMSShield()
{
  gsmSerial.begin(115200);
  gsmSerial.println("AT+CLIP=1");  //включаем АОН
  delay(100);
  gsmSerial.println("AT+CMGF=1");  //режим кодировки СМС - обычный (для англ.)
  delay(100);
  gsmSerial.println("AT+CSCS=\"GSM\"");  //режим кодировки текста
  delay(100);
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
  //Запускаем часы реального времени
  clock.begin();

  //Получаем число из строки, зная номер первого символа
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

void setup() 
{
  // Onboard led
  pinMode(onboardLedPin, OUTPUT);

  // Force close water switch pin
  pinMode(forceCloseButtonPin, INPUT);

  // Relay Pin
  pinMode(relayPin, OUTPUT);

  // Init detecors pin list
  initDetectorPin();

  Serial.begin(9600);

  // Init clock
  initClock();

  // Init SMSShield
  initSMSShield();
}

// ****************************************************************************************** //
// ========================================   MAIN   ======================================== //

/**
 * Функция обходит все датчики для проверки. Возвращает идентификатор при сработавшем датчике.
 */
int checkDetectors()
{
  if (alarmDetect != EMPTY_DETECTOR) {
    return alarmDetect;
  }

  // Power ON base
  digitalWrite(powerPin, HIGH);
  delay(100);
  // Loop all detectors for check
  int detectorState=HIGH;
  for (int i = 0; i < detectorPinCount; i++) {
    detectorState=digitalRead(detectorPinList[i]);
    if (detectorState==LOW) {
      digitalWrite(powerPin, LOW);
      return detectorPinList[i];
    }
  }
  // Power Off base
  digitalWrite(powerPin, LOW);

  return EMPTY_DETECTOR;
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
  Serial.println("CLOSED");
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
  Serial.println("OPENED");
  digitalWrite(onboardLedPin, LOW);
  digitalWrite(relayPin, LOW); 
}

void loop() 
{
  // Проверяем на состояние принудительного закрытия кранов по кнопке
  if (checkForceCloseState()) {
    closeWater();
    return;
  }

  // Открываем воду если нет тревоги
  if (alarmDetect == EMPTY_DETECTOR) {
    openWater();
  }
  else {
    closeWater();
    return;
  }

  // Обходим датчики
  if (interval(DELAY_CHECK_INTERVAL)) {
    int detector = checkDetectors();
    if (detector != EMPTY_DETECTOR) {
      //closeWater();
      //setAlarm(detector);
      return;
    }
  }

  // Тестируем краны
  testWaterTap();
}

/*
   УНИВЕРСАЛЬНАЯ ГОРЕЛКА-БОЙЛЕР
   v1.33: Управление по смс .
   - исправление 1.29
*/


////////////////////////// начало настроек которые можно менять //////////////////////
char SketchVersion[16] = "PORTNOV v1.33";
char CopyrightData[16] = "+79280061067"; //копирайт на экране
char SMSDefaultContact[18] = "+79280061067"; //кому отправлять смс по умолчанию
char SMSContact[18]; //кому отправляются уведомления о критических ошибках

//термостат
boolean ThermostatActive = 0; //имеется ли термостат? 0 = нет, 1 = да;
const int ThermostatButton = A1; //кнопка включения горелки по термостату

//маслонасос
unsigned long PumpTimeout = 200000; //аварийный таймаут маслонасоса (200 секунд).
//все что относится к воде
int DesiredWaterTemp = 60; //желаемая (меняемая из меню) температура воды в батареях
const int MinimumWaterTemp = 15; //минимальная температура воды в батарее
const int MaximumWaterTemp = 80; //максимальная температура воды в батарее
const int CriticalWaterTemp = 90; //аварийная температура воды
//все что относится к гистерезису воды
int DesiredWaterGisteresisTemp = 5; //желаемый (меняемый из меню) гистерезис воды
const int MinimumWaterGisteresisTemp = 0; //минимальный гистерезис воды
const int MaximumWaterGisteresisTemp = 10; //максимальный гистерезис воды
//все что относится к маслу
int DesiredFuelTemp = 86; //желаемая (меняемая из меню) температура распыления масла
const int MinimumFuelTemp = 10; //минимальная температура распыления масла
const int MaximumFuelTemp = 105; //максимальная температура распыления масла
const int CriticalFuelTemp = 115; //аварийная температура распыления масла
//все что относится к гистерезису масла
const int DesiredFuelGisteresisTemp = 2; //неизменный гистерезис распыления масла
//все что относится ко времени выполнения розжига
unsigned long FanStartUpTime = 10000; //10 сек время продувки перед запуском
unsigned long FanShutDownTime = 10000; //10 сек время работы вентилятора по завершению горения
unsigned long FanPreIgnitionPauseTime = 10000; //10 сек время паузы после продувки перед запуском
unsigned long IgnitionTime = 5000; //время работы искры на розжиг (больше 10 секунд опасно!)
unsigned long IgnitionPauseTime = 10000; //5 сек пауза после работы искры
unsigned long PreIgnitionFanDelay = 0; //время задержки вентилятора после работы искры
const int IgnitionAttempts = 5; // количество попыток розжига горелки
const int SensorErrorCount = 10; //количество игнорируемых ошибок датчиков температуры 5x10сек=50sec максимально простой с ребутом вочдогом 8сек
const int IgnitionImpulseFrequency = 155; //частота подачи импульса на искру (больше 180 опасно!)
int AmbientLumen = 0; //минимальное значение светимости пламени в единицах датчика
const int LumenDiff = 20; //разница между освещенностью помещения и горящим пламенем
//меню
unsigned long SetupWaitTime = 5000; //время ожидания нажатия кнопки сетапа (изменения настроек)
unsigned long SetupPushWaitTime = 2000; //время нажатия кнопки для входа в сетап в рабочем режиме (изменения настроек)
unsigned long TempSensErrorWaitTime = 30000; //30сек ждать пока не ребутнемся при ошибке датчика температуры


//проверка тена
int HeaterWatchdogCounter = 0; //обнуляем вочдог тена
int HeaterWatchdogTemp;
const int HeaterWatchdogMax = 100; //количество циклов запуска тена (приблизительно 5мин 1цикл=3сек) после которого если нагрева не произошло считать что тен сломался
////////////////////////// конец настроек которые можно менять //////////////////////
#include <avr/wdt.h>
#include <Bounce2.h>
#include <Rotary.h>
#include <EEPROM.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <NeoSWSerial.h>


//стираем коды ошибок в начальное положение false (0) - volatile для ответственных переменных;
volatile boolean EmergencyExitCode = 0; //общая критическая ошибка - проверяется везде!
boolean InteractiveMode = 0; // переменная при вызове которой переходим в режим ввода данных
boolean PumpIsRunning = 0; //насос по умолчанию выключен
boolean HeaterIsRunning = 0; //нагреватель по умолчанию выключен
boolean FireShouldBeRunning = 0; //огонь по умолчанию не горит
boolean IgnitionShouldBeRunning = 0; //зажигание по умолчание в позиции выключено
boolean PowerLost = 0; //пропало ли 220в ?
boolean ConfirmStartBySMS = 0;

//собираем данные с датчиков в эти переменные:
int WaterTemperatureSensorValue; //сюда пишем GetTemperature(OneWireWaterTempSensorAddress);
int FuelTemperatureSensorValue; //сюда пишем значение GetTemperature(OneWireFuelTempSensorAddress);
int FireSensorValue; // сюда пишем значние ReadOpticalSensor(OpticalSensor);
int FuelLevelValue; // сюда пишем значние digitalRead(FuelLevelSensor);

//цифровые выходы (прерывания только на 2 и 3 пине)
const int GSMTX = 2; //GSM пин с которого мы получаем входящие сообщения на ардуину от GSM асинхронно по прерываниям (!!!прерывание - ОБЯЗАТЕЛЬНО!!!).
const int EncoderButton = 3;  // кнопка энкодера
const int FuelPumpSwitch = 4;   // реле подкачки масла (маслонасоса)
const int HeaterSwitch = 5;  // реле тена
const int FanSwitch = 6;  // реле включения продувки вентиллятором
const int OneWireWaterTempSensorAddress = 7; //датчик температуры воды
const int PowerInput = 8; //5v входящие от сети (если нет то бить тревогу)
const int EncoderBack = 9; //поворотная кнопка против часовой стрелкe
const int EncoderForward = 10; //поворотная кнопка по часовой стрелке
const int IgnitionSwitch = 11;   // частотный выход для катушки зажигания ( analogWrite(IgnitionSwitch, 160) ШИМ широко-импульсная модуляция)
const int AirValve = 12; // клапан воздуха компрессора
const int GSMRX = 13; //GSM пин на который мы отправляем исходящие сообщения с ардуины (без прерывания)


//аналоговые выходы
const int OpticalSensor = A0; //фотодатчик (смотрим горит ли пламя)
const int OneWireFuelTempSensorAddress = A2; //датчик температуры топлива (масла/дизеля)
const int FuelLevelSensor = A3; // датчик уровня масла


//ПЗУ
const int WaterEepromAddress = 1;
const int WaterGisteresisEepromAddress = 2;
const int FuelEepromAddress = 3;
const int OperationEepromAddress = 4;
const int SMSOperationEepromAddress = 5;
const int InitEepromAddress = 6;
const int MessageNotifyEepromAddress = 7;
const int SMSEepromAddress = 100;
const int MessageEepromAddress = 200;

//по умолчанию горелка выключена(0) - включить(1).
boolean OperationMode = 0;

//по умолчанию sms отключены(0) - включить(1)
boolean GSMOperationMode = 0;

//переменные относящиеся к учету времени
unsigned long PumpStartTime; //время старта маслонасоса

//текстовые переменные
char* SystemMessage; //текстовая запись при аварийном выключении

//Пароль для управления по смс
int Password;

//настройка экрана 16x2 (новый адрес)
//LiquidCrystal_I2C lcd(0x03F, 16, 2);
//настройка экрана 16x2 (старый адрес)
LiquidCrystal_I2C lcd(16, 2);

int WaterTempSensorValue;
int WaterTempSensorErrorCount = 0;
int FuelTempSensorValue;
int FuelTempSensorErrorCount = 0;
int AirTempSensorValue;
int AirTempSensorErrorCount = 0;
int Temperature;

//инициируем объект дебаунсера
Bounce DebouncedEncoder = Bounce();

//Секция - все для СМС - начало

String inputString = "";   // a String to hold incoming data
boolean stringComplete = false;  // whether the string is complete

NeoSWSerial serialSIM800(GSMTX, GSMRX);

static void HandleSMSRXData( char c )
{
  // add it to the inputString:
  if (c == '\n' || c == '\r') {
    stringComplete = true;
  } else {
    inputString += c;
  }
}

String grepValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }

  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}



void AtRequest(String AtCommand = "", String AtResponse = "", int Retries = 5) {
  if (AtCommand.length() > 0 and AtResponse.length() == 0) {
    wdt_reset();
    serialSIM800.println(AtCommand);
    AtCommand = "";
  } else if (AtCommand.length() > 0 and AtResponse.length() > 0) {
    bool Finished = false;
    while (Retries > 0 and Finished != true) {
      wdt_reset();
      Retries--;
      serialSIM800.println(AtCommand);
      unsigned long CurrentTime = millis();
      while ( millis() < CurrentTime + 5000 and Finished != true) {
        wdt_reset();
        if (stringComplete) {
          if (inputString.length() != 0) {
            Serial.println(inputString);
            if (inputString.endsWith(AtResponse)) {
              //OK: AT command succeded.
              Finished = true;
            }
            inputString = "";
          }
          stringComplete = false;
        }
      }
    }
  } else {
    if (stringComplete) {
      if (inputString.length() != 0) {
        Serial.println(inputString);
        GetTempPhone();
        inputString = "";
      }
      stringComplete = false;
    }
  }
}

void GSMInit() {
  PrintMessage("GSM init...");
  AtRequest("AT", "OK"); //включен ли модем?
  AtRequest("ATE0", "OK"); //отключаем эхо
  AtRequest("AT+CLIP=1", "OK"); //включаем нотификацию на экран
  AtRequest("AT+CMGF=1", "OK"); //переходим в текстовый режим из бинарного
  AtRequest("AT+CNMI=2,2,0,0,0", "OK"); //отключаем хранение смс в памяти - показывать на экране и все
}

void SendSMS(char Phone[16], char Message[16]) {
  if (GSMOperationMode == 1) {
    GSMInit();
    PrintDualMessage(Message, "SMS SENT");
    AtRequest("AT+CMGS=\"" + String(Phone) + "\"\n" + String(Message) + "\x1A");
    //unsigned long CurrentTime = millis();
    //while ( millis() < CurrentTime + 1000) {
    //  wdt_reset();
    //}
  } else {
    PrintMessage(Message);
  }
}

void GetTempPhone() {
  //if (Message.substring(1, 5) == "CLIP" or Message.substring(1, 4) == "CMT" or Message.substring(0, 9) == "RING+CLIP") {
  //if (Message.substring(0, 9) == "RING+CLIP") {
  if (inputString.substring(1, 4) == "CMT") {
    String TempPhone = grepValue(inputString, '"', 1);
    TempPhone.reserve(24);
    if (TempPhone == String(SMSContact) ) {
      if (inputString.endsWith("on") or inputString.endsWith("On") or inputString.endsWith("ON"))  {
        EEPROM.update(OperationEepromAddress, 1);
        wdt_reset(); //сбрасываем вочдог
        EEPROM.update(MessageNotifyEepromAddress, 1);
        wdt_reset(); //сбрасываем вочдог
        char SMSMessage[16] = "ONLINE";
        EEPROM.put(MessageEepromAddress, SMSMessage);
        wdt_reset(); //сбрасываем вочдог
        PrintMessage("Switching ON...");
        delay(2000);
        SoftReset();
      } else if (inputString.endsWith("off") or inputString.endsWith("Off") or inputString.endsWith("OFF")) {
        EEPROM.update(OperationEepromAddress, 0);
        wdt_reset(); //сбрасываем вочдог
        EEPROM.update(MessageNotifyEepromAddress, 1);
        wdt_reset(); //сбрасываем вочдог
        char SMSMessage[16] = "OFFLINE";
        EEPROM.put(MessageEepromAddress, SMSMessage);
        wdt_reset(); //сбрасываем вочдог
        PrintMessage("Switching OFF...");
        delay(2000);
        SoftReset();
      }
    } else {
      if (inputString.endsWith(String(Password))) {
        char CharTempPhone[16];
        TempPhone.toCharArray(CharTempPhone, 16);
        EEPROM.put(SMSEepromAddress, CharTempPhone);
        wdt_reset(); //сбрасываем вочдог
        EEPROM.update(MessageNotifyEepromAddress, 1);
        wdt_reset(); //сбрасываем вочдог
        char SMSMessage[16] = "Welcome!";
        EEPROM.put(MessageEepromAddress, SMSMessage);
        wdt_reset(); //сбрасываем вочдог
        Serial.println(TempPhone + " is now the owner!");
        delay(2000);
        SoftReset();
      }
    }
  }
}
//Секция - все для СМС - конец


void Reset() {
  //чистим еепром для ресета в заводские настройки
  for (int i = 0 ; i < EEPROM.length() ; i++) {
    EEPROM.write(i, 0);
  }
}

void BurnerInit()
{
  //При включении инициализируется дисплей и датчики температуры
  //и выводятся на дисплей текущая температура масла и воды.

  //SERIAL PORT INIT
  Serial.begin(9600);

  //назначение вход-выход
  //output interfaces (switches)
  pinMode(AirValve, OUTPUT);
  pinMode(FanSwitch, OUTPUT);
  pinMode(HeaterSwitch, OUTPUT);
  pinMode(IgnitionSwitch, OUTPUT);
  pinMode(FuelPumpSwitch, OUTPUT);

  //input interfaces (sensors):
  pinMode(EncoderButton, INPUT_PULLUP);
  pinMode(FuelLevelSensor, INPUT);
  pinMode(OpticalSensor, INPUT);
  pinMode(ThermostatButton, INPUT);
  pinMode(PowerInput, INPUT);

  digitalWrite(FuelLevelSensor, HIGH);//включаем подтягивающий резистор к плюсу для фотодатчика.
  digitalWrite(ThermostatButton, HIGH);//включаем подтягивающий резистор к плюсу для кнопки.

  //цепляемся раздребезгом к кнопке
  DebouncedEncoder.attach(EncoderButton);
  DebouncedEncoder.interval(50);

  //для начала все релюхи в положение ВЫКЛЮЧЕНО
  digitalWrite(FanSwitch, LOW);
  digitalWrite (AirValve, LOW);
  digitalWrite (HeaterSwitch, LOW);
  digitalWrite (FuelPumpSwitch, LOW);



  //включить дисплей и подсветку дисплея
  lcd.init();
  lcd.backlight ();

  //Берем данные из ПЗУ если они там есть
  int EPrW = EEPROM.read(WaterEepromAddress);
  if (EPrW >= MinimumWaterTemp && EPrW <= MaximumWaterTemp ) {
    DesiredWaterTemp = EPrW;
  }

  int EPrWG = EEPROM.read(WaterGisteresisEepromAddress);
  if (EPrWG >= MinimumWaterGisteresisTemp && EPrWG <= MaximumWaterGisteresisTemp ) {
    DesiredWaterGisteresisTemp = EPrWG;
  }

  int EPrO = EEPROM.read(FuelEepromAddress);
  if (EPrO >= MinimumFuelTemp && EPrO <= MaximumFuelTemp ) {
    DesiredFuelTemp = EPrO;
  }

  int EPrOPER = EEPROM.read(OperationEepromAddress);
  if (EPrOPER >= 0 && EPrOPER <= 1 ) {
    OperationMode = EPrOPER;
  }

  int EPrSMSOPER = EEPROM.read(SMSOperationEepromAddress);
  if (EPrSMSOPER >= 0 && EPrSMSOPER <= 1 ) {
    GSMOperationMode = EPrSMSOPER;
  } else {
    EEPROM.update(SMSOperationEepromAddress, 0);
    GSMOperationMode = 0;
  }

  //initial EEPROM owner update
  int EPrINIT = EEPROM.read(InitEepromAddress);
  if (EPrINIT != 1) {
    EEPROM.put(SMSEepromAddress, SMSDefaultContact);
    EEPROM.update(InitEepromAddress, 1);
    delay(1000);
  }

  char EESMSContact[18];
  EEPROM.get(SMSEepromAddress, EESMSContact);
  int PhoneLength = strlen(EESMSContact);
  if (PhoneLength > 9 or PhoneLength <= 18) {
    strncpy(SMSContact, EESMSContact, PhoneLength);
    //Serial.println(String(EESMSContact) + " eeprom owner");
  } else {
    strncpy(SMSContact, SMSDefaultContact, 18);
    //Serial.println(String(SMSDefaultContact) + " default owner");
  }


  //устанавливаем значение светимости
  //считывание фотодатчика
  FireSensorValue = ReadOpticalSensor(OpticalSensor);
  AmbientLumen = (FireSensorValue + LumenDiff);


  //смс
  if (GSMOperationMode == 1) {

    inputString.reserve(100);
    serialSIM800.attachInterrupt(HandleSMSRXData);
    serialSIM800.begin(9600);

    GSMInit();

    randomSeed(analogRead(OpticalSensor));
    Password = random(9999);


    int EPrNOTIF = EEPROM.read(MessageNotifyEepromAddress);
    if (EPrNOTIF == 1) {
      ConfirmStartBySMS = 1;
      wdt_reset(); //сбрасываем вочдог
      char SMSMessage[16];
      EEPROM.get(MessageEepromAddress, SMSMessage);
      wdt_reset(); //сбрасываем вочдог
      EEPROM.update(MessageNotifyEepromAddress, 0);
      wdt_reset(); //сбрасываем вочдог
      SendSMS(SMSContact, SMSMessage);
    }

  }
  //смс


}

//инвертируем датчик света (работает от 1250 до нуля где 1250 это темно а 0 это сильный свет.)
//делаем наоборот - 0 минимум, 1250 - максимум.
int ReadOpticalSensor(int SensorPort) {
  int Value = (1024 - analogRead(SensorPort));
  if (Value < 0) {
    Value = 0;
  } else if (Value > 1024) {
    Value = 1250;
  }
  return Value;
}




void UpdateCurrentData() {
  wdt_reset(); //сбрасываем вочдог


  if (EmergencyExitCode != 1 ) {

    if (GSMOperationMode == 1) {
      //проверяем телефон
      AtRequest();
    }

    //считывание фотодатчика
    FireSensorValue = ReadOpticalSensor(OpticalSensor);

    //считываем поплавок
    FuelLevelValue = digitalRead(FuelLevelSensor); //HIGH(мало), LOW(много);

    // считывание датчиков температур
    WaterTemperatureSensorValue = GetTemperature(OneWireWaterTempSensorAddress);
    FuelTemperatureSensorValue = GetTemperature(OneWireFuelTempSensorAddress);

    //проверяем нажата ли кнопка энкодера
    DebouncedEncoder.update();

    if ( DebouncedEncoder.read() == LOW ) {
      unsigned long PushTime = millis();
      while ( millis() < PushTime + SetupPushWaitTime && EmergencyExitCode != 1 ) {
        wdt_reset(); //сбрасываем вочдог
        DebouncedEncoder.update();
        if (DebouncedEncoder.read() == HIGH) {
          goto press_cancelled;
        }
      }
      DebouncedEncoder.update();
      if ( DebouncedEncoder.read() == LOW && InteractiveMode != 1 && EmergencyExitCode != 1 ) {
        InteractiveMode = 1;
      }

      if ( InteractiveMode == 1) {
        InteractiveMode = 0;
        SelectMenu();
      }
    }
press_cancelled:;

    //есть ли питание на 8 пине (5v от сети 220v) если нет то остановить горелку и отправить смс
    if (digitalRead(PowerInput) == LOW) {
      if (PowerLost == 0) {
        PowerLost = 1;
        SendSMS(SMSContact, "PAUSE: NO 220v");
        PrintMessage("PAUSE: NO 220v");
      }
      //стоп машина - питания нету
      NormalStop();
      goto finish;
    } else {
      if (PowerLost == 1) {
        PowerLost = 0;
        ConfirmStartBySMS = 1;
        SendSMS(SMSContact, "START: 220v OK");
      }
    }

    //если огонь потух не во время розжига, а клапаны и вентилятор работают - выключить все.
    if ( (FireShouldBeRunning == 1) or (IgnitionShouldBeRunning = 0)  ) {
      if (FireSensorValue < AmbientLumen) {
        NormalStop();
      }
    }


    //проверяем температуру воды
    if (WaterTemperatureSensorValue >= CriticalWaterTemp) {
      EmergencyExitCode = 1;
      EmergencyStop("STOP: WATER TEMP");
      goto finish;
    }

    //проверяем температуру масла
    if (FuelTemperatureSensorValue >= CriticalFuelTemp) {
      //Serial.println(FuelTemperatureSensorValue);
      EmergencyExitCode = 1;
      EmergencyStop("STOP: OIL TEMP");
      goto finish;
    }

    lcd.setCursor(0, 0);
    Clean7();
    lcd.setCursor(0, 0);
    lcd.print("W:");
    lcd.setCursor(3, 0);
    lcd.print(WaterTemperatureSensorValue);

    lcd.setCursor(7, 0);
    Clean9();
    lcd.setCursor(7, 0);
    lcd.print("O:");
    lcd.setCursor(10, 0);
    lcd.print(FuelTemperatureSensorValue);

    if ( FuelLevelValue == HIGH ) {
      lcd.setCursor(0, 1);
      Clean7();
      lcd.setCursor(0, 1);
      lcd.print("L: OK ");
    }
    else {
      lcd.setCursor(0, 1);
      Clean7();
      lcd.setCursor(0, 1);
      lcd.print("L: LOW ");
    }
  }
finish:;
}



void PrintMessage(char SystemMessage[16]) {
  //показать сообщение на экране
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(SystemMessage);
  //Serial.println(SystemMessage);
}

void PrintDualMessage(char TopMessage[16], char BottomMessage[16]) {
  //показать сообщение на экране
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(TopMessage);
  //Serial.println(TopMessage);
  lcd.setCursor(0, 1);
  lcd.print(BottomMessage);
  //Serial.println(BottomMessage);
}

void Clean7() {
  lcd.print("       ");
}

void Clean9() {
  lcd.print("         ");
}

void Clean16() {
  lcd.print("                ");
}

void EmergencyStop(char SystemMessage[16]) {
  //экстренная остановка горелки, выключаем все агрегаты
  //кроме экрана на котором выводим причину отключения
  //
  //Serial.println("EMERGENCY STOP");
  //Serial.println(SystemMessage);
  wdt_disable(); //отклчаем ребут по вочдогу
  digitalWrite (FanSwitch, LOW);
  digitalWrite (AirValve, LOW);
  digitalWrite (HeaterSwitch, LOW);
  digitalWrite (FuelPumpSwitch, LOW);
  analogWrite(IgnitionSwitch, 0);
  EmergencyExitCode = 1;
  SendSMS(SMSContact, SystemMessage);
  //PrintMessage(SystemMessage);
}

int GetTemperature(int SensorPort) {
  if (EmergencyExitCode != 1 ) {
    OneWire oneWire(SensorPort);
    DallasTemperature sensors(&oneWire);
    DeviceAddress TempSensBus;
    sensors.begin();
    sensors.requestTemperatures();

    //Serial.println("MEMORY: Water/Fuel/Air temp sensors:");
    //Serial.println(WaterTempSensorValue);
    //Serial.println(FuelTempSensorValue);
    //Serial.println(AirTempSensorValue);

    if (!sensors.getAddress(TempSensBus, 0)) {

      if (SensorPort == OneWireWaterTempSensorAddress) {
        WaterTempSensorErrorCount++;
        Temperature = WaterTempSensorValue;
        //Serial.println("Error detected: Water temp sensor");
        //Serial.println(Temperature);
      }
      else if (SensorPort == OneWireFuelTempSensorAddress) {
        FuelTempSensorErrorCount++;
        Temperature = FuelTempSensorValue;
        //Serial.println("Error detected: Fuel temp sensor");
        //Serial.println(Temperature);
      }
      else {
        AirTempSensorErrorCount++;
        Temperature = AirTempSensorValue;
        //Serial.println("Error detected: Air temp sensor");
        //Serial.println(Temperature);
      }

      if (WaterTempSensorErrorCount > SensorErrorCount or FuelTempSensorErrorCount > SensorErrorCount or AirTempSensorErrorCount > SensorErrorCount) {

        if (SensorPort == OneWireWaterTempSensorAddress) {
          EmergencyExitCode = 1;
          EmergencyStop("STOP: WATER SENSOR");
          goto finish;
        }
        else if (SensorPort == OneWireFuelTempSensorAddress) {
          EmergencyExitCode = 1;
          EmergencyStop("STOP: OIL SENSOR");
          goto finish;
        }
        else {
          EmergencyExitCode = 1;
          EmergencyStop("STOP: AIR SENSOR");
          goto finish;
        }
      }
      if (Temperature > 125) {
        Temperature = 0;
      }
      return Temperature;
    } else {
      Temperature = sensors.getTempCByIndex(0);
      if (SensorPort == OneWireWaterTempSensorAddress) {
        WaterTempSensorValue = Temperature;
        WaterTempSensorErrorCount = 0;
      }
      else if (SensorPort == OneWireFuelTempSensorAddress) {
        FuelTempSensorValue = Temperature;
        FuelTempSensorErrorCount = 0;
      }
      else {
        AirTempSensorValue = Temperature;
        AirTempSensorErrorCount = 0;
      }
      return Temperature;
    }
  }
finish:;
}

void Pause() {
  //Serial.println("Pause");
  //остановка горелки на паузу (вода нагрелась), выключаем агрегаты отвечающие за огонь
  FireShouldBeRunning = 0;

  if (digitalRead(AirValve) == HIGH) {
    digitalWrite (AirValve, LOW);

  }
  analogWrite(IgnitionSwitch, 0);

  if (digitalRead(FanSwitch) == HIGH) {
    //digitalWrite (FanSwitch, LOW);
    //    продувка камеры сгорания свежим воздухом по завершению работы
    unsigned long StartFinalProduvkaTime = millis();
    while ( millis() < StartFinalProduvkaTime + FanShutDownTime && EmergencyExitCode != 1 ) {
      UpdateCurrentData();
    }
    digitalWrite (FanSwitch, LOW);
  }

  // PrintMessage(id);
  // delay(500);
  // lcd.clear();

}


//void NormalStop(char* id) {
void NormalStop() {
  //Serial.println("NormalStop");
  FireShouldBeRunning = 0;
  digitalWrite (FanSwitch, LOW);
  digitalWrite (AirValve, LOW);
  digitalWrite (HeaterSwitch, LOW);
  digitalWrite (FuelPumpSwitch, LOW);
  analogWrite(IgnitionSwitch, 0);

  //  PrintMessage(id);
  //  delay(500);
  //  lcd.clear();
}

void NormalStopWithText() {
  //Serial.println("NormalStopWithText");
  FireShouldBeRunning = 0;
  digitalWrite (FanSwitch, LOW);
  digitalWrite (AirValve, LOW);
  digitalWrite (HeaterSwitch, LOW);
  digitalWrite (FuelPumpSwitch, LOW);
  analogWrite(IgnitionSwitch, 0);
  lcd.setCursor(0, 1);
  Clean16();
  lcd.setCursor(0, 1);
  if (GSMOperationMode == 1) {
    lcd.print("BURN OFF, GSM ON");
  } else {
    lcd.print("BURN + GSM OFF");
  }
}


void PumpTheFuel() {
  //если значение датчика уровня = HIGH то масла достаточно.
  if ( EmergencyExitCode != 1 ) {
    if (PowerLost == 0) {
      if ( digitalRead(FuelLevelSensor) == LOW ) {
        //если первое включение насоса - установить временную метку
        if ( PumpIsRunning == 0 ) {
          PumpStartTime = millis();
        }
        if (millis() < PumpStartTime + PumpTimeout) {
          //если масла мало то включить насос (перевести насос в HIGH)
          digitalWrite(FuelPumpSwitch, HIGH);
          PumpIsRunning = 1;
          //выключить тен, если температура ниже 50
          if (FuelTemperatureSensorValue < 50) {
            HeaterIsRunning = 0;
            lcd.setCursor(7, 1);
            Clean9();
            lcd.setCursor(7, 1);
            lcd.print("O: PAUSE");
            digitalWrite(HeaterSwitch, LOW); //выключае тен
          }
        }
        else {
          //если неуспело накачать за PumpTimout то выдать ошибку и остановиться:
          digitalWrite(FuelPumpSwitch, LOW);
          EmergencyExitCode = 1;
          PumpIsRunning = 0;
          EmergencyStop("STOP: NO FUEL");
        }
      }
      else {
        //если масла достаточно то выключить насос и завершить цикл
        digitalWrite(FuelPumpSwitch, LOW);
        PumpIsRunning = 0;
      }
    }
  }
}

void HeatTheFuel() {
  if (EmergencyExitCode != 1) {
    if (PowerLost == 0) {
      //Выполниться дальше (нагреть масло) если: насос не качает ИЛИ температура масла выше 50 градусов цельсия.
      if (PumpIsRunning != 1 or FuelTemperatureSensorValue > 50 ) {
        if (FuelTemperatureSensorValue < DesiredFuelTemp ) {
          if ( FuelTemperatureSensorValue < (DesiredFuelTemp - DesiredFuelGisteresisTemp) ) {
            //когда эта переменная = 1 - блокируется розжиг
            HeaterIsRunning = 1;

            //добавляем вотчдог для тена (вдруг сломался, температура масла упала ниже 50 градусов и не греет в течении 5 минут (300сек)? - отправить смс!)
            if ( FuelTemperatureSensorValue < 50 ) {
              if (HeaterWatchdogCounter == 0) {
                HeaterWatchdogTemp = FuelTemperatureSensorValue;
                HeaterWatchdogCounter++;
                //Serial.print("HeaterWatchdogCounter: ");
                //Serial.println(HeaterWatchdogCounter);
              } else if (HeaterWatchdogCounter < HeaterWatchdogMax) {
                HeaterWatchdogCounter++;
                //Serial.print("HeaterWatchdogCounter: ");
                //Serial.println(HeaterWatchdogCounter);
              } else if (HeaterWatchdogCounter >= HeaterWatchdogMax) {
                if (FuelTemperatureSensorValue > (HeaterWatchdogTemp + 1)) {
                  HeaterWatchdogCounter = 0;
                } else {
                  EmergencyExitCode = 1;
                  EmergencyStop("STOP: TEN");
                  goto finish;
                }
              }
            } else {
              HeaterWatchdogCounter = 0;
            }

            lcd.setCursor(7, 1);
            Clean9();
            lcd.setCursor(7, 1);
            lcd.print("O: HEAT ");
            digitalWrite(HeaterSwitch, HIGH); //включаем тен
          }
        }
        else {
          HeaterIsRunning = 0;
          HeaterWatchdogCounter = 0; //обнуляем вочдог тена
          lcd.setCursor(7, 1);
          Clean9();
          lcd.setCursor(7, 1);
          lcd.print("O: PAUSE");
          digitalWrite(HeaterSwitch, LOW); //выключаем тен
        }
      }
      else {
        HeaterIsRunning = 0;
        HeaterWatchdogCounter = 0; //обнуляем вочдог тена
        lcd.setCursor(7, 1);
        Clean9();
        lcd.setCursor(7, 1);
        lcd.print("O: PAUSE");
        digitalWrite(HeaterSwitch, LOW); //выключаем тен
      }
    }
  }
finish:;
}



void Fire() {
  if (EmergencyExitCode != 1 ) {
    if (PowerLost == 0) {
      int IgnitionCounter = 0;
      //зажигание стартует
      IgnitionShouldBeRunning = 1;
      while ( FireSensorValue <= AmbientLumen && PumpIsRunning != 1 && HeaterIsRunning != 1 && digitalRead(FuelLevelSensor) != LOW  && IgnitionAttempts > IgnitionCounter && EmergencyExitCode != 1 && PowerLost == 0 ) {
        NormalStop();
        // запуск продувки камеры сгорание свежим воздухом
        unsigned long StartProduvkaTime = millis();
        digitalWrite (FanSwitch, HIGH);

        while ( millis() < StartProduvkaTime + FanStartUpTime && EmergencyExitCode != 1 && PowerLost == 0) {
          UpdateCurrentData();
        }

        // остановка продувки на паузу перед зажиганием пламени
        unsigned long StopProduvkaTime = millis();
        digitalWrite (FanSwitch, LOW);

        while ( millis() < StopProduvkaTime + FanPreIgnitionPauseTime && EmergencyExitCode != 1 && PowerLost == 0 ) {
          UpdateCurrentData();
        }


        unsigned long StartIgnitionTime = millis();
        //если огонь не горит - запустить искру и вентилятор с клапаном воздуха
        if ( FireSensorValue <= AmbientLumen ) {
          digitalWrite (AirValve, HIGH);
          delay(PreIgnitionFanDelay);
          analogWrite(IgnitionSwitch, IgnitionImpulseFrequency);
          delay(PreIgnitionFanDelay);
          digitalWrite (FanSwitch, HIGH);
        }

        //пауза горелки
        while ( millis() < StartIgnitionTime + IgnitionTime && EmergencyExitCode != 1 && PowerLost == 0) {
          UpdateCurrentData();
        }
        //если огонь не загорелся - выключить клапана и искру
        if ( FireSensorValue < AmbientLumen ) {
          Pause();
        } else {
          //если же загорелся - то выключить только искру а клапана не трогать
          analogWrite(IgnitionSwitch, 0);
          //поддерживать огонь - поставить переменную что надо следить за огнем
          FireShouldBeRunning = 1;
          //задача #1: писать sms при удачном старте старт ок после возобновления 220v например.
          if (ConfirmStartBySMS == 1) {
            ConfirmStartBySMS = 0;
            SendSMS(SMSContact, "OK: FIRE UP");
          }
        }
        UpdateCurrentData();
        IgnitionCounter++;
        if (IgnitionAttempts == IgnitionCounter && FireSensorValue <= AmbientLumen  && PowerLost == 0) {
          EmergencyExitCode = 1;
          EmergencyStop("STOP: IGNITION");
          goto finish;
        }
      }
    }
  }
finish:;
  //зажигание завершилось
  IgnitionShouldBeRunning = 0;
}


void SelectMenu() {
  wdt_disable(); //отклчаем ребут по вочдогу
  NormalStop();
  PrintMessage("      MENU     ");
  delay(1000);//wait encoder button to calm down)
  lcd.clear();

  int SelectedValue = 0;
  //инициализируем поворотную кнопку (энкодер)
  Rotary r = Rotary(EncoderBack, EncoderForward);
  boolean EncoderTurned = 0;
  PrintDualMessage("      MENU      ", "   <<- | ->>   " );
  while (digitalRead(EncoderButton) != LOW && EmergencyExitCode != 1) {
    unsigned char RotaryDirection = r.process();
    if (RotaryDirection) {

      EncoderTurned = 1;

      if (RotaryDirection == DIR_CCW) {
        SelectedValue--;
      }
      else {
        SelectedValue++;
      }

      if (SelectedValue < 0) {
        SelectedValue = 4;
      }

      if (SelectedValue > 4) {
        SelectedValue = 0;
      }


      if ((SelectedValue == 1) and (OperationMode == 1)) {
        PrintMessage("  BURN OFF?");
      }
      else if ((SelectedValue == 1) and (OperationMode == 0)) {
        PrintMessage("  BURN ON?");
      }
      else if (SelectedValue == 0) {
        PrintMessage("  SETUP?");
      }
      if ((SelectedValue == 2) and (GSMOperationMode == 1)) {
        PrintMessage("  GSM  OFF?");
      }
      else if ((SelectedValue == 2) and (GSMOperationMode == 0)) {
        PrintMessage("  GSM  ON?");
      }
      else if (SelectedValue == 3) {
        PrintMessage("  RESET?");
      }
      else if (SelectedValue == 4) {
        PrintMessage("  EXIT?");
      }

    }
  }
  if (( EmergencyExitCode != 1 ) and ( EncoderTurned == 1 )) {
    if ((SelectedValue == 1) and (OperationMode == 1)) {
      EEPROM.update(OperationEepromAddress, 0);
      PrintMessage("OK");
      delay(2000);
      lcd.clear();
      SoftReset();
    }
    else if ((SelectedValue == 1) and (OperationMode == 0)) {
      EEPROM.update(OperationEepromAddress, 1);
      PrintMessage("OK");
      delay(2000);
      lcd.clear();
      SoftReset();
    }
    else if (SelectedValue == 0) {
      PrintMessage("OK");
      delay(2000);
      lcd.clear();
      InteractiveInput();
    }
    else if ((SelectedValue == 2) and (GSMOperationMode == 1)) {
      EEPROM.update(SMSOperationEepromAddress, 0);
      PrintMessage("OK");
      delay(2000);
      lcd.clear();
      SoftReset();
    }
    else if ((SelectedValue == 2) and (GSMOperationMode == 0)) {
      EEPROM.update(SMSOperationEepromAddress, 1);
      PrintMessage("OK");
      delay(2000);
      lcd.clear();
      SoftReset();
    }
    else if (SelectedValue == 3) {
      PrintMessage("OK");
      Reset();
      delay(2000);
      lcd.clear();
      SoftReset();
    }
    else if (SelectedValue == 4) {
      PrintMessage("OK");
      delay(2000);
      lcd.clear();
      SoftReset();
    }
  }
  else {
    PrintMessage("OK");
    delay(2000);
    lcd.clear();
    SoftReset();
  }
}



void InteractiveInput() {
  wdt_disable(); //отклчаем ребут по вочдогу
  NormalStop();
  //инициализируем поворотную кнопку (энкодер)
  Rotary r = Rotary(EncoderBack, EncoderForward);

  //вводим желаемое значение температуры воды
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WATER TEMP:");
  lcd.setCursor(13, 0);
  lcd.print(DesiredWaterTemp);

  while (digitalRead(EncoderButton) != LOW && EmergencyExitCode != 1) {
    unsigned char RotaryDirection = r.process();
    if (RotaryDirection) {
      if (RotaryDirection == DIR_CCW) {
        DesiredWaterTemp--;
      }
      else {
        DesiredWaterTemp++;
      }
      if (DesiredWaterTemp <= MinimumWaterTemp) {
        DesiredWaterTemp = MinimumWaterTemp;
      }
      if (DesiredWaterTemp >= MaximumWaterTemp) {
        DesiredWaterTemp = MaximumWaterTemp;
      }
      lcd.setCursor(13, 0);
      lcd.print(DesiredWaterTemp);
    }
  }
  if (EmergencyExitCode != 1) {
    PrintMessage("Saving...");
    delay(500);
    EEPROM.update(WaterEepromAddress, DesiredWaterTemp);
    PrintMessage("Done.");
    delay(200);
    lcd.clear();
  }


  //воодим желаемое значение гистерезиса воды (масло у нас зашито постоянным значением)
  lcd.setCursor(0, 0);
  lcd.print("WATER GIS:");
  if (DesiredWaterGisteresisTemp < 10) {
    lcd.setCursor(12, 0);
    lcd.print(0);
    lcd.setCursor(13, 0);
  }
  else {
    lcd.setCursor(12, 0);
  }
  lcd.print(DesiredWaterGisteresisTemp);

  while (digitalRead(EncoderButton) != LOW && EmergencyExitCode != 1) {

    unsigned char RotaryDirection = r.process();
    if (RotaryDirection) {
      if (RotaryDirection == DIR_CCW) {
        DesiredWaterGisteresisTemp--;
      }
      else {
        DesiredWaterGisteresisTemp++;
      }
      if (DesiredWaterGisteresisTemp <= MinimumWaterGisteresisTemp) {
        DesiredWaterGisteresisTemp = MinimumWaterGisteresisTemp;
      }
      if (DesiredWaterGisteresisTemp >= MaximumWaterGisteresisTemp) {
        DesiredWaterGisteresisTemp = MaximumWaterGisteresisTemp;
      }
      if (DesiredWaterGisteresisTemp < 10) {
        lcd.setCursor(12, 0);
        lcd.print(0);
        lcd.setCursor(13, 0);
      }
      else {
        lcd.setCursor(12, 0);
      }
      lcd.print(DesiredWaterGisteresisTemp);
    }
  }

  if (EmergencyExitCode != 1) {
    PrintMessage("Saving...");
    delay(500);
    EEPROM.update(WaterGisteresisEepromAddress, DesiredWaterGisteresisTemp);
    PrintMessage("Done.");
    delay(200);
    lcd.clear();
  }



  //вводим желаемую температуру масла
  lcd.setCursor(0, 0);
  lcd.print("OIL TEMP:");
  lcd.setCursor(11, 0);
  lcd.print(DesiredFuelTemp);
  if (DesiredFuelTemp < 100) {
    lcd.setCursor(13, 0);
    lcd.print(" ");
  }

  while (digitalRead(EncoderButton) != LOW && EmergencyExitCode != 1) {

    unsigned char RotaryDirection = r.process();
    if (RotaryDirection) {
      if (RotaryDirection == DIR_CCW) {
        DesiredFuelTemp--;
      }
      else {
        DesiredFuelTemp++;
      }
      if (DesiredFuelTemp <= MinimumFuelTemp) {
        DesiredFuelTemp = MinimumFuelTemp;
      }
      if (DesiredFuelTemp >= MaximumFuelTemp) {
        DesiredFuelTemp = MaximumFuelTemp;
      }

      lcd.setCursor(11, 0);
      lcd.print(DesiredFuelTemp);
      if (DesiredFuelTemp < 100) {
        lcd.setCursor(13, 0);
        lcd.print(" ");
      }
    }
  }
  if (EmergencyExitCode != 1) {
    PrintMessage("Saving...");
    delay(500);
    EEPROM.update(FuelEepromAddress, DesiredFuelTemp);
    PrintMessage("Done.");
    delay(200);
    lcd.clear();
  }
  //завершаем интерактивный режим
  PrintMessage("FINISHING...");
  delay(1000);
  lcd.clear();
  SoftReset();
}

void setup() {
  Serial.begin(9600);
  BurnerInit();
  PrintDualMessage(SketchVersion, CopyrightData);
  delay(5000);
  if (GSMOperationMode == 1) {
    PrintDualMessage("Tel:", SMSContact);
    delay(5000);
    PrintMessage("Pass:");
    lcd.setCursor(0, 1);
    lcd.print(Password);
    //Serial.println(Password);
    delay(5000);
  } else {
    PrintMessage("GSM OFF");
    delay(5000);
  }

  while (EmergencyExitCode != 1) {
    wdt_enable(WDTO_8S); //устанавливаем вочдог на 8 секунд
    UpdateCurrentData();
    if (OperationMode == 1 && PowerLost == 0) {
      PumpTheFuel();
      FuelTemperatureSensorValue = GetTemperature(OneWireFuelTempSensorAddress);
      HeatTheFuel();

      //включаем подогрев воды только по гистерезису воды
      WaterTemperatureSensorValue = GetTemperature(OneWireWaterTempSensorAddress);

      //добавляем контроль от перегрева в комнатах - допустим если висит термостат в комнате
      //он может размыкать кнопку и горелка не будет греть воду пока воздух не упадет ниже определенного значения.
      if (WaterTemperatureSensorValue < DesiredWaterTemp) {
        if ( WaterTemperatureSensorValue < (DesiredWaterTemp - DesiredWaterGisteresisTemp)) {
          //условие: если термостат не активирован в системе - зажигаем горелку когда угодно.
          if (ThermostatActive == 0) {
            Fire();
          } else {
            //условие: если термостат задействован - ориентируемся по его контактам: - если они "LOW"
            //то зажигаем горелку и греем до установленной температуры, если "HIGH" - не зажигаем горелку.
            if (digitalRead(ThermostatButton) == LOW) {
              Fire();
            } else {
              Pause();
            }
          }
        }
      } else {
        Pause();
      }
    } else {
      NormalStopWithText();
    }

    while (EmergencyExitCode == 1) {
      delay(200);
      //проверяем телефон при аварии для перезапуска
      if (GSMOperationMode == 1) {
        AtRequest();
      }
    }
  }
}


void loop() {
}

void SoftReset() {
  wdt_reset();
  asm volatile ("  jmp 0");
}

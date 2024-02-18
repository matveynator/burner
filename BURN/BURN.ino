/*
   UNIVERSAL BURNER-STOVE-BOILER
   v1.34: english comments.
   - Fix 1.29
*/

////////////////////////// beginning of configurable settings //////////////////////
char SketchVersion[16] = "PORTNOV v1.34";
char CopyrightData[16] = "PORTNOV STOVE"; // copyright on the screen
char SMSDefaultContact[18] = "+79280061067"; // default recipient for SMS
char SMSContact[18]; // recipient for critical error notifications

// Thermostat
boolean ThermostatActive = 0; // Is thermostat active? 0 = no, 1 = yes;
const int ThermostatButton = A1; // Button to activate the burner by thermostat

// Oil pump
unsigned long PumpTimeout = 200000; // Emergency timeout for the oil pump (200 seconds).
// All related to water
int DesiredWaterTemp = 60; // Desired (changeable from the menu) water temperature in radiators
const int MinimumWaterTemp = 15; // Minimum water temperature in the radiator
const int MaximumWaterTemp = 80; // Maximum water temperature in the radiator
const int CriticalWaterTemp = 90; // Emergency water temperature
// All related to water hysteresis
int DesiredWaterGisteresisTemp = 5; // Desired (changeable from the menu) water hysteresis
const int MinimumWaterGisteresisTemp = 0; // Minimum water hysteresis
const int MaximumWaterGisteresisTemp = 10; // Maximum water hysteresis
// All related to oil
int DesiredFuelTemp = 86; // Desired (changeable from the menu) oil atomization temperature
const int MinimumFuelTemp = 10; // Minimum oil atomization temperature
const int MaximumFuelTemp = 105; // Maximum oil atomization temperature
const int CriticalFuelTemp = 115; // Emergency oil atomization temperature
// All related to oil hysteresis
const int DesiredFuelGisteresisTemp = 2; // Unchanged oil atomization hysteresis
// All related to ignition time
unsigned long FanStartUpTime = 10000; // 10 sec purge time before ignition
unsigned long FanShutDownTime = 10000; // 10 sec fan operation time after burning
unsigned long FanPreIgnitionPauseTime = 10000; // 10 sec pause after purge before ignition
unsigned long IgnitionTime = 5000; // Ignition spark duration (more than 10 seconds is dangerous!)
unsigned long IgnitionPauseTime = 10000; // 5 sec pause after spark operation
unsigned long PreIgnitionFanDelay = 0; // Fan delay time after spark operation
const int IgnitionAttempts = 5; // Number of burner ignition attempts
const int SensorErrorCount = 10; // Number of ignored temperature sensor errors 5x10sec=50sec maximum downtime with watchdog reboot 8sec
const int IgnitionImpulseFrequency = 155; // Frequency of spark pulse delivery (more than 180 is dangerous!)
int AmbientLumen = 0; // Minimum flame brightness value in light sensor units
const int LumenDiff = 20; // Difference between room light and burning flame
// Menu
unsigned long SetupWaitTime = 5000; // Time to wait for the setup button press (settings changes)
unsigned long SetupPushWaitTime = 2000; // Time to press the button to enter setup in working mode (settings changes)
unsigned long TempSensErrorWaitTime = 30000; // 30 sec wait for a reboot in case of temperature sensor error


// Checking the heating element
int HeaterWatchdogCounter = 0; // Reset the heater watchdog
int HeaterWatchdogTemp;
const int HeaterWatchdogMax = 100; // Number of cycles of heating element activation (approximately 5 minutes, 1 cycle = 3 sec) after which, if no heating occurs, consider that the heating element is broken
////////////////////////// end of configurable settings //////////////////////

#include <avr/wdt.h>
#include <Bounce2.h>
#include <Rotary.h>
#include <EEPROM.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <NeoSWSerial.h>

// Reset error codes to initial state (false - 0) - volatile for critical variables;
volatile boolean EmergencyExitCode = 0; // Common critical error - checked everywhere!
boolean InteractiveMode = 0; // Variable to switch to data input mode when called
boolean PumpIsRunning = 0; // Pump is initially turned off
boolean HeaterIsRunning = 0; // Heater is initially turned off
boolean FireShouldBeRunning = 0; // Fire is initially not burning
boolean IgnitionShouldBeRunning = 0; // Ignition is initially in the off position
boolean PowerLost = 0; // Has 220V power been lost?
boolean ConfirmStartBySMS = 0;

// Collect data from sensors into these variables:
int WaterTemperatureSensorValue; // Write GetTemperature(OneWireWaterTempSensorAddress) value here
int FuelTemperatureSensorValue; // Write GetTemperature(OneWireFuelTempSensorAddress) value here
int FireSensorValue; // Write ReadOpticalSensor(OpticalSensor) value here
int FuelLevelValue; // Write digitalRead(FuelLevelSensor) value here

// Digital outputs (interrupts only on pins 2 and 3)
const int GSMTX = 2; // GSM pin to receive incoming messages asynchronously on Arduino via interrupts (!!!interrupt - MANDATORY!!!).
const int EncoderButton = 3; // Encoder button
const int FuelPumpSwitch = 4; // Oil pump priming relay
const int HeaterSwitch = 5; // Heater relay
const int FanSwitch = 6; // Fan activation relay
const int OneWireWaterTempSensorAddress = 7; // Water temperature sensor
const int PowerInput = 8; // 5V input from the 220v network (trigger alarm if 220v is absent)
const int EncoderBack = 9; // Rotary button counterclockwise
const int EncoderForward = 10; // Rotary button clockwise
const int IgnitionSwitch = 11; // Frequency output for ignition coil (analogWrite(IgnitionSwitch, 160) PWM - Pulse Width Modulation)
const int AirValve = 12; // Compressor air valve
const int GSMRX = 13; // GSM pin to send outgoing messages from Arduino (without interrupt)

// Analog outputs
const int OpticalSensor = A0; // Photodetector (check if flame is present)
const int OneWireFuelTempSensorAddress = A2; // Fuel (oil/diesel) temperature sensor
const int FuelLevelSensor = A3; // Oil level sensor

// EEPROM
const int WaterEepromAddress = 1;
const int WaterGisteresisEepromAddress = 2;
const int FuelEepromAddress = 3;
const int OperationEepromAddress = 4;
const int SMSOperationEepromAddress = 5;
const int InitEepromAddress = 6;
const int MessageNotifyEepromAddress = 7;
const int SMSEepromAddress = 100;
const int MessageEepromAddress = 200;

// Burner is initially off (0) - turn on (1).
boolean OperationMode = 0;

// SMS is initially disabled (0) - enable (1)
boolean GSMOperationMode = 0;

// Time-related variables
unsigned long PumpStartTime; // Oil pump start time

// Text variables
char* SystemMessage; // Text record for emergency shutdown

// SMS control password
int Password;

// 16x2 screen setup (new address)
// LiquidCrystal_I2C lcd(0x03F, 16, 2);
// 16x2 screen setup (old address)
LiquidCrystal_I2C lcd(16, 2);

int WaterTempSensorValue;
int WaterTempSensorErrorCount = 0;
int FuelTempSensorValue;
int FuelTempSensorErrorCount = 0;
int AirTempSensorValue;
int AirTempSensorErrorCount = 0;
int Temperature;

// Initialize debouncer object
Bounce DebouncedEncoder = Bounce();

// Section - all for SMS - start

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
  // Function to extract a value from a string based on a separator and index
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
  // Function to send AT commands to the SIM800 module and wait for a specific response
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
              // OK: AT command succeeded.
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
  // Initialize GSM module with specific AT commands
  PrintMessage("GSM init...");
  AtRequest("AT", "OK"); // Is the modem on?
  AtRequest("ATE0", "OK"); // Turn off echo
  AtRequest("AT+CLIP=1", "OK"); // Enable notification on screen
  AtRequest("AT+CMGF=1", "OK"); // Switch to text mode from binary
  AtRequest("AT+CNMI=2,2,0,0,0", "OK"); // Disable storing SMS in memory - show on screen and discard
}

void SendSMS(char Phone[16], char Message[16]) {
  // Send SMS with specified phone number and message
  if (GSMOperationMode == 1) {
    GSMInit();
    PrintDualMessage(Message, "SMS SENT");
    AtRequest("AT+CMGS=\"" + String(Phone) + "\"\n" + String(Message) + "\x1A");
  } else {
    PrintMessage(Message);
  }
}

void GetTempPhone() {
  // Process temporary phone number received in SMS
  if (inputString.substring(1, 4) == "CMT") {
    String TempPhone = grepValue(inputString, '"', 1);
    TempPhone.reserve(24);
    if (TempPhone == String(SMSContact) ) {
      if (inputString.endsWith("on") or inputString.endsWith("On") or inputString.endsWith("ON"))  {
        // Switch ON
        EEPROM.update(OperationEepromAddress, 1);
        wdt_reset(); // reset watchdog
        EEPROM.update(MessageNotifyEepromAddress, 1);
        wdt_reset(); // reset watchdog
        char SMSMessage[16] = "ONLINE";
        EEPROM.put(MessageEepromAddress, SMSMessage);
        wdt_reset(); // reset watchdog
        PrintMessage("Switching ON...");
        delay(2000);
        SoftReset();
      } else if (inputString.endsWith("off") or inputString.endsWith("Off") or inputString.endsWith("OFF")) {
        // Switch OFF
        EEPROM.update(OperationEepromAddress, 0);
        wdt_reset(); // reset watchdog
        EEPROM.update(MessageNotifyEepromAddress, 1);
        wdt_reset(); // reset watchdog
        char SMSMessage[16] = "OFFLINE";
        EEPROM.put(MessageEepromAddress, SMSMessage);
        wdt_reset(); // reset watchdog
        PrintMessage("Switching OFF...");
        delay(2000);
        SoftReset();
      }
    } else {
      if (inputString.endsWith(String(Password))) {
        char CharTempPhone[16];
        TempPhone.toCharArray(CharTempPhone, 16);
        EEPROM.put(SMSEepromAddress, CharTempPhone);
        wdt_reset(); // reset watchdog
        EEPROM.update(MessageNotifyEepromAddress, 1);
        wdt_reset(); // reset watchdog
        char SMSMessage[16] = "Welcome!";
        EEPROM.put(MessageEepromAddress, SMSMessage);
        wdt_reset(); // reset watchdog
        Serial.println(TempPhone + " is now the owner!");
        delay(2000);
        SoftReset();
      }
    }
  }
}

// Section - all for SMS - end


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

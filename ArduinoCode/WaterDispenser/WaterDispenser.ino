#include <WiFiManager.h> 
#include <PubSubClient.h>
#include <HX711_ADC.h>
#if defined(ESP8266)|| defined(ESP32) || defined(AVR)
#include <EEPROM.h>
#endif

//pins:
#define HX711_dout 14 //mcu > HX711 dout pin
#define HX711_sck  12 //mcu > HX711 sck pin
#define WATER_LEVEL 13
#define HEATER 5    //d1
#define PUMP 4      //d2 //MUST PWM

//HX711 constructor:
HX711_ADC LoadCell(HX711_dout, HX711_sck);

const int calVal_eepromAdress = 0;
unsigned long t = 0;
float previousValue = 0;
unsigned long previousMilis = 0;
boolean isStableReading = false;
boolean isWaterOK = false;
boolean isStop = false;

//mqtt
const char* mqttServer = "192.168.31.88";
const int mqttPort = 1883;
const char* mqttUser = "syukur";
const char* mqttPassword = "syukur123***";
const char* topic = "web";

WiFiClient espClient;
PubSubClient client(espClient);

void setup()
{
  Serial.begin(115200);
  pinMode(WATER_LEVEL,INPUT);
  pinMode(HEATER,OUTPUT);
  pinMode(PUMP,OUTPUT);

  digitalWrite(PUMP,LOW);
  digitalWrite(HEATER,LOW);
  
  WiFiManager wm;

  bool res;
  res = wm.autoConnect("Water_Dispenser"); 
  
  if(!res) Serial.println("Failed to connect");
  else Serial.println("connected to wifi");
  
  LoadCell.begin();
  float calibrationValue; // calibration value (see example file "Calibration.ino")
  #if defined(ESP8266)|| defined(ESP32)
    EEPROM.begin(512); // uncomment this if you use ESP8266/ESP32 and want to fetch the calibration value from eeprom
  #endif
  
  EEPROM.get(calVal_eepromAdress, calibrationValue); // uncomment this if you want to fetch the calibration value from eeprom

  unsigned long stabilizingtime = 2000; // preciscion right after power-up can be improved by adding a few seconds of stabilizing time
  boolean _tare = true; //set this to false if you don't want tare to be performed in the next step
  
  LoadCell.start(stabilizingtime, _tare);
  if (LoadCell.getTareTimeoutFlag()) {
    Serial.println("Timeout, check MCU>HX711 wiring and pin designations");
    while (1);
  }
  else {
    LoadCell.setCalFactor(calibrationValue); // set calibration value (float)
    Serial.println("Startup is complete");
  }

  //MQTT
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);

  while (!client.connected()) 
  {
    Serial.println("Connecting to MQTT...");
    if (client.connect("ESP8266Client", mqttUser, mqttPassword )) 
    {
      Serial.println("connected");
    } 
    else
    {
      Serial.print("failed with state ");
      Serial.print(client.state());
      delay(2000);
    }
  }

  client.publish(topic, "/status/online");
  waterCheck();
  client.subscribe("waterdispenser");
}

void callback(char* topic, byte* payload, unsigned int length) {

  String Message = "";
  Serial.print("Message:");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    Message += (char)payload[i];
  }
  Serial.println();

  String command = getValue(Message,'/',1);
  String value   = getValue(Message,'/',2);
  
  Serial.println("Command: " + command);
  Serial.println("Value : " + value);

  if(command == "stop")
  {
    isStop = true;
  }
  if(command == "tare")
  {
    LoadCell.tareNoDelay();
  }
  if(command == "status")
  {
    sendMessage("/status/online");
    waterCheck();
  }
  if(command == "normal")
  {
    float fValue = value.toFloat();
    dispenseNormal(fValue,12.0);
  }
  if(command == "hot")
  {
    float fValue = value.toFloat();
    dispenseHot(fValue,12.0);
  }
  if(command == "warm")
  {
    float fValue = value.toFloat();
    dispenseWarm(fValue,12.0);
  }
}
bool waterCheck()
{
    if(digitalRead(WATER_LEVEL)) 
    {
      sendMessage("/water/0");
      return false;
    }
    if(!digitalRead(WATER_LEVEL)) 
    {
      sendMessage("/water/1");
      return true;
    }
}
void dispenseWarm(float volume,float offset)
{
 volume = volume - offset;
  bool isComplete = false;
  int percent,prevPercent = 0;
  char msg[30];
  float glassWeight = previousValue;
  pumpON(50);
  heaterON(true);
  previousMilis = millis();
  while(!isComplete)
  {
      client.loop();
      float currentValue = getWeight();
      
      if(currentValue >= glassWeight) 
      {
        percent = map(currentValue - glassWeight,0,volume,0,100);
        snprintf (msg, 13, "/filling/%ld", percent);
        if(abs(previousValue-currentValue) > 1.0 )
        {
          previousValue = currentValue;
          client.publish(topic,msg);
        }
      }
      
      //jika glass diangkat
      if((previousValue - currentValue) >= 2.0  || isStop)
      {
        heaterON(false);
        delay(2000);
        pumpON(0);
        isComplete = true;
        isStop = false;
        Serial.println("Stop");
        break;
      }

      if(abs((volume + glassWeight) - currentValue) <= 50.0)
      {
        heaterON(false);
        if(abs((volume + glassWeight) - currentValue) <= 10.0) pumpON(30);
      }
      else
      {
        if((millis() - previousMilis) > 5000)
        {
            pumpON(80);
        }
      }
      
      if(currentValue >= volume + glassWeight) 
      {
        pumpON(0);
        isComplete = true;
        Serial.println("Finish");
        heaterON(false);
      }
      if(digitalRead(WATER_LEVEL))
      {
        heaterON(false);
        pumpON(0);
        isComplete = true;
        Serial.println("No Water");
        break;
      }
      
      yield();
  }
}
void dispenseHot(float volume,float offset)
{
 volume = volume - offset;
  bool isComplete = false;
  int percent,prevPercent = 0;
  char msg[30];
  float glassWeight = previousValue;
  pumpON(45);
  heaterON(true);
  previousMilis = millis();
  while(!isComplete)
  {
      client.loop();
      float currentValue = getWeight();
      
      if(currentValue >= glassWeight) 
      {
        percent = map(currentValue - glassWeight,0,volume,0,100);
        snprintf (msg, 13, "/filling/%ld", percent);
        if(abs(previousValue-currentValue) > 1.0 )
        {
          previousValue = currentValue;
          client.publish(topic,msg);
        }
      }
      
      //jika glass diangkat
      if((previousValue - currentValue) >= 2.0  || isStop || digitalRead(WATER_LEVEL))
      {
        heaterON(false);
        delay(2000);
        pumpON(0);
        isComplete = true;
        isStop = false;
        Serial.println("Stop");
        break;
      }

      if((abs(volume + glassWeight) - currentValue) <= 50.0)
      {
        heaterON(false);
        if(abs((volume + glassWeight) - currentValue) <= 10.0) pumpON(30);
      }
      else
      {
        if((millis() - previousMilis) > 5000)
        {
            pumpON(55);
        }
      }
      
      if(currentValue >= volume + glassWeight) 
      {
        pumpON(0);
        isComplete = true;
        Serial.println("Finish");
        heaterON(false);
      }
      if(digitalRead(WATER_LEVEL))
      {
        heaterON(false);
        pumpON(0);
        isComplete = true;
        Serial.println("No Water");
        break;
      }
      yield();
  }
}
void dispenseNormal(float volume, float offset)
{
  volume = volume - offset;
  bool isComplete = false;
  int percent,prevPercent = 0;
  char msg[30];
  float glassWeight = previousValue;
  
  Serial.print("capacity request :");
  Serial.print(volume,0);
  Serial.println("ml");
  Serial.print("glass weight :");
  Serial.print(glassWeight);
  Serial.println("g");
  pumpON(50);
  while(!isComplete)
  {
      client.loop();
      float currentValue = getWeight();
      
      if(currentValue >= glassWeight) 
      {
        percent = map(currentValue - glassWeight,0,volume,0,100);
        snprintf (msg, 13, "/filling/%ld", percent);
        if(abs(previousValue-currentValue) > 1.0 )
        {
          previousValue = currentValue;
          client.publish(topic,msg);
        }
      }
      
      //jika glass diangkat
      if((previousValue - currentValue) >= 2.0 || isStop || digitalRead(WATER_LEVEL))
      {
        pumpON(0);
        isComplete = true;
        isStop = false;
        Serial.println("Stop");
        break;
      }

      if(abs((volume + glassWeight) - currentValue) <= 10.0)
      {
        pumpON(30);
      }
      
      if(currentValue >= volume + glassWeight) 
      {
        pumpON(0);
        isComplete = true;
        Serial.println("Finish");
      }
      if(digitalRead(WATER_LEVEL))
      {
        pumpON(0);
        isComplete = true;
        Serial.println("No Water");
        break;
      }
      yield();
  }
}

String getValue(String data, char separator, int index) {
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
float getWeight()
{
  static boolean newDataReady = 0;
  // check for new data/start next conversion:
  if (LoadCell.update()) newDataReady = true;

  // get smoothed value from the dataset:
  if (newDataReady) {
    if (millis() > t) {
      float i = LoadCell.getData();
      newDataReady = 0;
      t = millis();

      return i;
    }
  }
}
void sendMessage(char* mesg)
{
  char msg[30];
  int Len = strlen(mesg) + 1;
  snprintf (msg, Len , mesg , 20);  
  client.publish(topic,msg);
}
void sendMessageInFloat(String message, float value)
{
  String mesg ="/" + message + "/%.2f";
  char msgTemp[100];
  char msg[30];
  for(int i=0;i<mesg.length();i++)
  {
    msgTemp[i] = mesg[i];
  }
  int Len = mesg.length() + 2;
  snprintf (msg, Len, msgTemp , value);
  client.publish(topic,msg);
}
void heaterON(bool isON)
{
  if(isON) digitalWrite(HEATER,HIGH);
  if(!isON) digitalWrite(HEATER,LOW);
}
void pumpON(int percentSpeed)
{
  int pwmSpeed = map(percentSpeed,0,100,0,255);
  analogWrite(PUMP,pwmSpeed);
}
void loop() 
{
  client.loop();
  if(!digitalRead(WATER_LEVEL))
  {
    float currentValue = getWeight();
    if(abs(currentValue - previousValue) > 1.0)
    {
      previousValue = currentValue;
      sendMessageInFloat("measuring",currentValue);  
      previousMilis = millis();
      isStableReading = false;
    }
    if((millis() - previousMilis) >= 1000)
    {
        if(!isStableReading)
        {
          Serial.print("Glass Weight:");
          Serial.println(currentValue);
          isStableReading = true;
          previousValue = currentValue;
          sendMessageInFloat("glass",currentValue);
        }
    }
    if(!isWaterOK) 
    {
      sendMessage("/water/1");
      isWaterOK = true;
    }
  }
  else
  {
    isWaterOK = false;
    sendMessage("/water/0");
    delay(2000);
  }
  

   if (LoadCell.getTareStatus() == true) {
    Serial.println("Tare complete");
    client.publish(topic,"/tare/done");
  }
}

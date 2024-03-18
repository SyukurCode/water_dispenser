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
#define MQTT_KEEPALIVE 1000 // keep trying various numbers

//HX711 constructor:
HX711_ADC LoadCell(HX711_dout, HX711_sck);

const int calVal_eepromAdress = 0;
unsigned long t = 0;
float previousValue = 0;
unsigned long previousMilis = 0;
boolean isStableReading = false;
boolean isReadyToDispense = false;
boolean isWaterOK = false;
boolean isStop = false;
boolean inUse = false;
boolean isCalibrate = false;
long lastReconnectAttempt = 0;

//mqtt
const char* mqttServer = "192.168.0.88";
const int mqttPort = 1883;
const char* mqttUser = "syukur";
const char* mqttPassword = "syukur123***";
const char* topic = "web";

WiFiClient espClient;
PubSubClient client(espClient);

void setup()
{
  Serial.begin(115200);
  pinMode(WATER_LEVEL, INPUT);
  pinMode(HEATER, OUTPUT);
  pinMode(PUMP, OUTPUT);

  digitalWrite(PUMP, LOW);
  digitalWrite(HEATER, LOW);

  WiFiManager wm;
  String WIFI_HOSTNAME = "Water_Dispenser";
  WiFi.setHostname(WIFI_HOSTNAME.c_str()); //define hostname

  bool res;
  res = wm.autoConnect("Water_Dispenser");

  if (!res) Serial.println("Failed to connect");
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
  client.setKeepAlive(MQTT_KEEPALIVE);
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);

  while (!client.connected())
  {
    Serial.println("Connecting to MQTT...");
    if (client.connect("ESP8266Client", mqttUser, mqttPassword))
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

  //initial checking
  client.publish(topic, "/status/online");
  isWaterOK = waterCheck();
  if (isWaterOK) sendMessage("/water/1");
  if (!isWaterOK) sendMessage("/water/0");

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

  String command = getValue(Message, '/', 1);
  String value   = getValue(Message, '/', 2);

  Serial.println("Command: " + command);
  Serial.println("Value : " + value);

  if (command == "stop")
  {
    isStop = true;
  }
  if (command == "tare")
  {
    LoadCell.tareNoDelay();
  }
  if (command == "status")
  {
    sendMessage("/status/online");
    isWaterOK = waterCheck();
    if (isWaterOK)
      sendMessage("/water/1");
    else
      sendMessage("/water/0");
  }
  if (command == "normal")
  {
    float fValue = value.toFloat();
    dispenseNormal(fValue, 12.0);
  }
  if (command == "hot")
  {
    float fValue = value.toFloat();
    dispenseHot(fValue, 12.0);
  }
  if (command == "warm")
  {
    float fValue = value.toFloat();
    dispenseWarm(fValue, 12.0);
  }
  if (command == "check")
  {
    if (isReadyToDispense && isWaterOK && !inUse){
      client.publish("alexa", "/status/ready");
    }
    else if (!isReadyToDispense)
      client.publish("alexa", "/status/noglass");
    if (!isWaterOK)
      client.publish("alexa", "/status/nowater");
    if (inUse)
      client.publish("alexa", "/status/inuse");
  }
  if (command == "config")
  {
    WiFiManager wm;
    // set configportal timeout
    wm.setConfigPortalTimeout(120);
    if (!wm.startConfigPortal("OnDemandConfig")) {
      Serial.println("failed to connect and hit timeout");
      delay(3000);
      //reset and try again, or maybe put it to deep sleep
      ESP.restart();
      delay(5000);
    }
  }
  if(command == "calibrate"){
    float calibrate_value = value.toFloat();
    calibrate(calibrate_value);
    isCalibrate = false; 
  }
}
bool waterCheck()
{
  if (!digitalRead(WATER_LEVEL))
  {
    //sendMessage("/water/1");
    return true;
  }
  //sendMessage("/water/0");
  return false;
}
void dispenseWarm(float volume, float offset)
{
  isStop = false;
  inUse = true;
  volume = volume - offset;
  float dispenseSpeed = 0.0;
  bool isComplete = false;
  int percent, prevPercent = 0;
  char msg[50];
  float glassWeight = previousValue;
  String message = "/filling/" + String(percent) + "/type/warm/capacity/" + String(volume);

  pumpON(50);
  heaterON(true);
  previousMilis = millis();
  long prevTime = millis();
  long monitorTime = millis();
  while (!isComplete)
  {
    client.loop();
    float currentValue = getWeight();
    //fillter valid value
    if(currentValue != -10000000.0){
      if (currentValue >= glassWeight)
      {
        percent = map(currentValue - glassWeight, 0, volume, 0, 100);
        if ((millis() - prevTime) > 100 )
        {
          float time_taken = (millis() - prevTime)/1000.0;
          int dispenseVolume = abs(previousValue - currentValue);
          dispenseSpeed = dispenseVolume/time_taken;
          message = "/filling/" + String(percent) + "/type/warm/capacity/" + String(volume)+ "/speed/" + String(dispenseSpeed);
          prevTime = millis();
          previousValue = currentValue;
          message.toCharArray(msg, message.length());
          client.publish(topic, msg);
        }
      }
      if((previousValue - currentValue) >= 50.0) {
         isStop = true;
      }
      //monitor flow
      if(millis() - monitorTime > 2000){
          if(dispenseSpeed < 1.0){
            isStop = true;
          }
          monitorTime = millis();
      }
    }
    
    //jika glass diangkat atau tiada air
    if (isStop || digitalRead(WATER_LEVEL))
    {
      heaterON(false);
      delay(2000);
      pumpON(0);
      isComplete = true;
      isStop = false;
      Serial.println("Stop");
      client.publish(topic, "/status/stop");
      break;
    }

    //prepair to stop
    if (abs((volume + glassWeight) - currentValue) <= 50.0)
    {
      heaterON(false);
      if (abs((volume + glassWeight) - currentValue) <= 10.0) pumpON(30);
    }
    else
    {
      if ((millis() - previousMilis) > 5000)
      {
        pumpON(80);
      }
    }
    //finish
    if (currentValue >= volume + glassWeight)
    {
      pumpON(0);
      isComplete = true;
      Serial.println("Finish");
      heaterON(false);
    }
    if (digitalRead(WATER_LEVEL))
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
void dispenseHot(float volume, float offset)
{
  isStop = false;
  inUse = true;
  volume = volume - offset;
  float dispenseSpeed = 0.0;
  bool isComplete = false;
  int percent, prevPercent = 0;
  char msg[50];
  float glassWeight = previousValue;
  String message = "/filling/" + String(percent) + "/type/hot/capacity/" + String(volume);

  pumpON(45);
  heaterON(true);
  previousMilis = millis();
  long prevTime = millis();
  long monitorTime = millis();
  while (!isComplete)
  {
    client.loop();
    float currentValue = getWeight();
    if(currentValue != -10000000.0){
      if (currentValue >= glassWeight)
      {
        percent = map(currentValue - glassWeight, 0, volume, 0, 100);
        if ((millis() - prevTime) > 100 )
        {
          float time_taken = (millis() - prevTime)/1000.0;
          int dispenseVolume = abs(previousValue - currentValue);
          dispenseSpeed = dispenseVolume/time_taken;
          message = "/filling/" + String(percent) + "/type/hot/capacity/" + String(volume)+ "/speed/" + String(dispenseSpeed);
          prevTime = millis();
          previousValue = currentValue;
          message.toCharArray(msg, message.length());
          client.publish(topic, msg);
        }
      }
      if((previousValue - currentValue) >= 50.0) {
         isStop = true;
      }
      //monitor flow
      if(millis() - monitorTime > 2000){
          if(dispenseSpeed < 1.0){
            isStop = true;
          }
          monitorTime = millis();
      }
    }

    //jika glass diangkat atau tiada air
    if (isStop || digitalRead(WATER_LEVEL))
    {
      heaterON(false);
      delay(2000);
      pumpON(0);
      isComplete = true;
      isStop = false;
      Serial.println("Stop");
      client.publish(topic, "/status/stop");
      break;
    }

    //prepair to stop
    if ((abs(volume + glassWeight) - currentValue) <= 50.0)
    {
      heaterON(false);
      if (abs((volume + glassWeight) - currentValue) <= 10.0) pumpON(30);
    }
    else
    {
      if ((millis() - previousMilis) > 5000)
      {
        pumpON(55);
      }
    }
    //finish
    if (currentValue >= volume + glassWeight)
    {
      pumpON(0);
      isComplete = true;
      Serial.println("Finish");
      heaterON(false);
    }
    if (digitalRead(WATER_LEVEL))
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
  isStop = false;
  inUse = true;
  volume = volume - offset;
  float dispenseSpeed = 0.0;
  bool isComplete = false;
  int percent, prevPercent = 0;
  char msg[50];
  float glassWeight = previousValue;
  String message = "/filling/" + String(percent) + "/type/normal/capacity/" + String(volume);

  Serial.print("capacity request :");
  Serial.print(volume, 0);
  Serial.println("ml");
  Serial.print("glass weight :");
  Serial.print(glassWeight);
  Serial.println("g");
  pumpON(50);
  previousMilis = millis();
  long prevTime = millis();
  long monitorTime = millis();
  while (!isComplete)
  {
    client.loop();
    float currentValue = getWeight();
    if(currentValue != -10000000.0){
      if (currentValue >= glassWeight)
      {
        percent = map(currentValue - glassWeight, 0, volume, 0, 100);
        //snprintf (msg, 13, "/filling/%ld", percent);
        message = "/filling/" + String(percent) + "/type/normal/capacity/" + String(volume);
        if ((millis() - prevTime) > 100 )
        {
          float time_taken = (millis() - prevTime)/1000.0;
          int dispenseVolume = abs(previousValue - currentValue);
          dispenseSpeed = dispenseVolume/time_taken;
          message = "/filling/" + String(percent) + "/type/normal/capacity/" + String(volume)+ "/speed/" + String(dispenseSpeed);
          prevTime = millis();
          previousValue = currentValue;
          message.toCharArray(msg, message.length());
          client.publish(topic, msg);
        }
      }
      if((previousValue - currentValue) >= 50.0) {
         isStop = true;
      }
      //monitor flow
      if(millis() - monitorTime > 2000){
          if(dispenseSpeed < 1.0){
            isStop = true;
          }
          monitorTime = millis();
      }
    }

    //jika glass diangkat atau tiada air
    if (isStop || digitalRead(WATER_LEVEL))
    {
      pumpON(0);
      isComplete = true;
      isStop = false;
      Serial.println("Stop");
      client.publish(topic, "/status/stop");
      break;
    }
    
    //prepair to stop
    if (abs((volume + glassWeight) - currentValue) <= 10.0)
    {
      pumpON(30);
    }
    //finish
    if (currentValue >= volume + glassWeight)
    {
      pumpON(0);
      isComplete = true;
      Serial.println("Finish");
    }
    if (digitalRead(WATER_LEVEL))
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
  return -10000000.0;
}
void sendMessage(char* mesg)
{
  char msg[30];
  int Len = strlen(mesg) + 1;
  snprintf (msg, Len , mesg , 20);
  client.publish(topic, msg);
}
void sendMessageInFloat(String message, float value)
{ 
  String mesg = "/" + message + "/" + String(value);
  char msg[30];
  mesg.toCharArray(msg, mesg.length());
  client.publish(topic, msg);
}
void heaterON(bool isON)
{
  if (isON) digitalWrite(HEATER, HIGH);
  if (!isON) digitalWrite(HEATER, LOW);
}
void pumpON(int percentSpeed)
{
  int pwmSpeed = map(percentSpeed, 0, 100, 0, 255);
  analogWrite(PUMP, pwmSpeed);
}
boolean reconnect() {
  client.setKeepAlive(MQTT_KEEPALIVE);
  if (client.connect("ESP8266Client", mqttUser, mqttPassword))
  {
    client.publish(topic, "/status/online");
    waterCheck();
    client.subscribe("waterdispenser");
  }
  return client.connected();
}
// calibrate
void calibrate(float known_mass) {
  LoadCell.refreshDataSet(); //refresh the dataset to be sure that the known mass is measured correct
  float newCalibrationValue = LoadCell.getNewCalibration(known_mass); //get the new calibration value
  client.publish(topic, "/message/calibrating");
  delay(1000);

#if defined(ESP8266)|| defined(ESP32)
        EEPROM.begin(512);
#endif
        EEPROM.put(calVal_eepromAdress, newCalibrationValue);
#if defined(ESP8266)|| defined(ESP32)
        EEPROM.commit();
#endif
        EEPROM.get(calVal_eepromAdress, newCalibrationValue);

  client.publish(topic, "/message/calibrate_value_was_saved");
}
void loop()
{
    if (!client.connected())
    {
      long now = millis();
      if (now - lastReconnectAttempt > 5000)
      {
        lastReconnectAttempt = now;
        // Attempt to reconnect
        if (reconnect()) {
          lastReconnectAttempt = 0;
        }
      }
    }
    else
    {
      client.loop();
      //if waterlevel ok
      if (!digitalRead(WATER_LEVEL))
      {
        float currentValue = getWeight();
        //filter valid value
        if(currentValue != -10000000.0){
          if (abs(currentValue - previousValue) > 1.0)
          {
            previousValue = currentValue;
            sendMessageInFloat("measuring", currentValue);
            previousMilis = millis();
            isStableReading = false;
            delay(300);
          }
          if ((millis() - previousMilis) >= 1000)
          {
            if (!isStableReading)
            {
              isReadyToDispense = false;
              Serial.print("Glass Weight:");
              Serial.println(currentValue);
              isStableReading = true;
              previousValue = currentValue;
              sendMessageInFloat("glass", currentValue);
              if (currentValue >= 5.0) isReadyToDispense = true;
              inUse = false;
            }
          }
        }
        if (!isWaterOK)
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

      if (LoadCell.getTareStatus() == true)
      {
        Serial.println("Tare complete");
        client.publish(topic, "/tare/done");
      }
    }
}

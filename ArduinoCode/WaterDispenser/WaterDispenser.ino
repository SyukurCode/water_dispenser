
#include <WiFiManager.h>
#include <PubSubClient.h>
#if defined(ESP8266)|| defined(ESP32) || defined(AVR)
#include <EEPROM.h>
#endif

//pins:
#define WATER_LEVEL 13 //D7
#define HEATER 15    //D8
#define PUMP 4      //D2 //MUST PWM
#define TRIGGER_PIN 0 //D3
#define OPTICAL_SENSOR 14 //D5

#define MQTT_KEEPALIVE 1000 // keep trying various numbers

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
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(OPTICAL_SENSOR, INPUT);
  
  digitalWrite(PUMP, LOW);
  digitalWrite(HEATER, LOW);

  WiFiManager wm;
  String WIFI_HOSTNAME = "Water_Dispenser";
  WiFi.setHostname(WIFI_HOSTNAME.c_str()); //define hostname

  bool res;
  res = wm.autoConnect("Water_Dispenser");

  if (!res) Serial.println("Failed to connect");
  else Serial.println("connected to wifi");

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

void callback(char* topic, byte* payload, unsigned int length) 
{
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
    dispenseNormal(fValue, 0.0);
  }
  if (command == "hot")
  {
    float fValue = value.toFloat();
    dispenseHot(fValue, 0.0);
  }
  if (command == "warm")
  {
    float fValue = value.toFloat();
    dispenseWarm(fValue, 0.0);
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
}
bool waterCheck()
{
  if (!digitalRead(WATER_LEVEL))
  {
    return true;
  }
  return false;
}
void dispenseWarm(float volume, float offset)
{
  isStop = false;
  inUse = true;
  volume = volume - offset;
  bool isComplete = false;
  int percent = 0;
  char msg[50];
  
  pumpON(50);
  heaterON(true);
  previousMilis = millis();
  unsigned long startTime = millis();
  unsigned long prevTime = millis();
  unsigned long monitorTime = millis();
  unsigned long onFinish = volume * 77.77;
  unsigned long onHeaterOff = (volume - 50) * 77.77;
  String message = "/filling/" + String(percent) + "/type/warm/capacity/" + String(volume) + "/speed/" + String(millis() - monitorTime);
  while (!isComplete)
  {
    client.loop();
    //stage speed
    if(millis() - startTime > 5000)
    {
      pumpON(80);
    }
    if(millis() - startTime > onFinish)
    {
      pumpON(0);
      isComplete = true;
      Serial.println("Finish");
      message = "/filling/" + String(percent) + "/type/warm/capacity/" + String(volume)+ "/speed/" + String(millis() - monitorTime);
    }
    if(millis() - startTime >= onHeaterOff )
    {
      heaterON(false);
      sendMessage("/debug/Heater_off");
    }
    if ((millis() - prevTime) > 100 )
    {
      percent = map(millis() - monitorTime, 0, onFinish, 0, 100);
      message = "/filling/" + String(percent) + "/type/warm/capacity/" + String(volume)+ "/speed/" + String(millis() - monitorTime);
      prevTime = millis();
      message.toCharArray(msg, message.length());
      client.publish(topic, msg);
    }

    //jika glass diangkat atau tiada air
    if (isStop || digitalRead(WATER_LEVEL) || digitalRead(OPTICAL_SENSOR))
    {
      pumpON(0);
      heaterON(false);
      isComplete = true;
      isStop = false;
      Serial.println("Stop");
      client.publish(topic, "/status/stop");
      break;
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
void dispenseHot(float volume, float offset)
{
  isStop = false;
  inUse = true;
  volume = volume - offset;
  bool isComplete = false;
  int percent = 0;
  char msg[50];
  
  pumpON(45);
  heaterON(true);
  previousMilis = millis();
  unsigned long startTime = millis();
  unsigned long prevTime = millis();
  unsigned long monitorTime = millis();
  unsigned long onFinish = volume * 102.5;
  unsigned long onHeaterOff = (volume - 60) * 102.5;
  String message = "/filling/" + String(percent) + "/type/hot/capacity/" + String(volume) + "/speed/" + String(millis() - monitorTime);
  while (!isComplete)
  {
    client.loop();
    if(millis() - startTime > 5000)
    {
      pumpON(55);
    }
    if(millis() - startTime > onFinish)
    {
      pumpON(0);
      isComplete = true;
      Serial.println("Finish");
      message = "/filling/" + String(percent) + "/type/warm/capacity/" + String(volume)+ "/speed/" + String(millis() - monitorTime);
    }
    if(millis() - startTime >= onHeaterOff )
    {
      heaterON(false);
      sendMessage("/debug/Heater_off");
    }
    if ((millis() - prevTime) > 100 )
    {
      percent = map(millis() - monitorTime, 0, onFinish, 0, 100);
      message = "/filling/" + String(percent) + "/type/hot/capacity/" + String(volume)+ "/speed/" + String(millis() - monitorTime);
      prevTime = millis();
      message.toCharArray(msg, message.length());
      client.publish(topic, msg);
    }

    //jika glass diangkat atau tiada air
    if (isStop || digitalRead(WATER_LEVEL) || digitalRead(OPTICAL_SENSOR))
    {
      pumpON(0);
      heaterON(false);
      isComplete = true;
      isStop = false;
      Serial.println("Stop");
      client.publish(topic, "/status/stop");
      break;
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
void dispenseNormal(float volume, float offset)
{
  isStop = false;
  inUse = true;
  volume = volume - offset;
  bool isComplete = false;
  int percent = 0;
  char msg[50];
  
  pumpON(50);
  previousMilis = millis();
  unsigned long startTime = millis();
  unsigned long prevTime = millis();
  unsigned long monitorTime = millis();
  unsigned long onFinish = volume * 107.4;
  String message = "/filling/" + String(percent) + "/type/normal/capacity/" + String(volume) + "/speed/" + String(millis() - monitorTime);
  while (!isComplete)
  {
    client.loop();

    if(millis() - startTime > onFinish)
    {
      pumpON(0);
      isComplete = true;
      Serial.println("Finish");
      message = "/filling/" + String(percent) + "/type/warm/capacity/" + String(volume)+ "/speed/" + String(millis() - monitorTime);
    }
    
    if ((millis() - prevTime) > 100 )
    {
      percent = map(millis() - monitorTime, 0, onFinish, 0, 100);
      message = "/filling/" + String(percent) + "/type/normal/capacity/" + String(volume)+ "/speed/" + String(millis() - monitorTime);
      prevTime = millis();
      message.toCharArray(msg, message.length());
      client.publish(topic, msg);
    }

    //jika glass diangkat atau tiada air
    if (isStop || digitalRead(WATER_LEVEL) || digitalRead(OPTICAL_SENSOR))
    {
      pumpON(0);
      isComplete = true;
      isStop = false;
      Serial.println("Stop");
      client.publish(topic, "/status/stop");
      break;
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
void sendMessage(char* mesg)
{
  char msg[30];
  int Len = strlen(mesg) + 1;
  snprintf (msg, Len , mesg , 20);
  if(!client.connected()) Serial.println("MQTT not connect");
  else client.publish(topic, msg);
}
void sendMessageInFloat(String message, float value)
{ 
  String mesg = "/" + message + "/" + String(value);
  char msg[30];
  mesg.toCharArray(msg, mesg.length());
  if(!client.connected()) Serial.println("MQTT not connect");
  else client.publish(topic, msg);
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
      if(inUse) isReadyToDispense = false;
      if (!digitalRead(OPTICAL_SENSOR))
      {
        if(!isReadyToDispense)
        {
          isReadyToDispense = true;
          sendMessage("/status/ready");
          inUse = false;
        }
      }
      else{
        if(isReadyToDispense)
        {
          isReadyToDispense = false;
          sendMessage("/status/noglass");
        }
      }
        
      //if waterlevel ok
      if (!digitalRead(WATER_LEVEL))
      {
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
    }

   //Ondemand portal
   if ( digitalRead(TRIGGER_PIN) == LOW) 
   {
    
      WiFiManager wm;    

    //reset settings - for testing
    //wm.resetSettings();
  
    // set configportal timeout
     wm.setConfigPortalTimeout(1000);

     if (!wm.startConfigPortal("Water_Dispenser")) {
      Serial.println("failed to connect and hit timeout");
      delay(3000);
      //reset and try again, or maybe put it to deep sleep
      ESP.restart();
      delay(5000);
     }
   }
}

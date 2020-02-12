#include <Arduino_JSON.h>
#include <HCSR04.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <stdio.h>
#include <ArduinoOTA.h>

//--------------
//SETUP / CONFIG
//--------------

//OTA
const char* hostname = "garagedoors";
const char* ota_pass = "password";

//ULTRASOUND SENSORS
//this is for two garagedoors, add more here if you need them.
HCSR04 hc_right(12,14);//initialisation class HCSR04 (trig pin , echo pin)
HCSR04 hc_left(15,13); //(trigger,echo)

//LOOP TIMER
int wait_seconds= 30; //number of seconds between statusupdated from your ultrasound sensors
unsigned long time_now = 0;

// WIFI
#ifndef STASSID
#define STASSID "MyWiFi"
#define STAPSK "NotSecure"
#endif

const char* ssid = STASSID;
const char* password = STAPSK;

//MQTT
IPAddress server(192,168,1,2); //mqtt server IP
WiFiClient wclient;
PubSubClient client(wclient, server);
const char* mqttuser = "user";
const char* mqttpass = "password";
const char* inTopic = "garagedoors/command"; //topic to subscribe to to receive commands
//outtopics for two garagedoors, add more here if you need them.
const char* outTopicR = "garagedoors/right";
const char* outTopicL = "garagedoors/left";
char temp[10]; //enough to store 2 decimals, a decimal seperator and 7 digits for up to 1000000 meters (not that your sensor will reach that far).
char payload_str[35]; //exactly 35 characters max payload
JSONVar payload_json;

//RELAYS
//this defines two relays for two doors, add more here if you need them
const int relayPinR = D1; // relay for the right door is on D1
const int relayPinL = D2; // relay for the left door is on D2
// Duration to close the switch on the door opener. This should be long
// enough for the mechanism to start; typically it doesn't to remain 
// activated for the door to complete its motion. It is the same as the
// time you'd hold down the button to start the door moving. 
const int relayActivationPeriod = 600; //ms

//-------
//METHODS
//-------
//this is called in the callback to toggle a pin to high, wait and then to low
void toggle(int pin) {
  Serial.print(F("Toggling pin: "));
  Serial.println(pin);
  digitalWrite(pin,HIGH);
  delay(relayActivationPeriod);
  digitalWrite(pin,LOW);
}

//MQTT callback
//the code assumes a JSON: {"door": "left","command":"open"}
//door = left will map to the left garage door, door = right to the right one
//commands = open will open the door, command = close will close the door
//command is ignored since we only need to know which relay (door) to toggle
//this assumes you will never send a command while the door is still in motion. 
void callback(const MQTT::Publish& pub) {
  // handle message arrived
  Serial.print(pub.topic());
  Serial.print(" => ");
  /*if (pub.has_stream()) {
    Serial.println("stream");
    uint8_t buf[BUFFER_SIZE];
    int read;
    while (read = pub.payload_stream()->read(buf, BUFFER_SIZE)) {
      Serial.write(buf, read);
    }
    pub.payload_stream()->stop();
    Serial.println("");
  } else*/
  Serial.println(pub.payload_string());
  
  payload_json = JSON.parse(pub.payload_string());
  if (JSON.typeof(payload_json) == "undefined") {
    Serial.println(F("Parsing input failed!"));
    return;
  }
  else {
    if(payload_json.hasOwnProperty("door")) {
      Serial.print(F("Payload door: "));
      Serial.println(payload_json["door"]);
      //Serial.print(F("Payload command: "));
      //Serial.println(payload_json["command"]);
      
      //this if/else handles two doors, add more else statements if you need them.
      if(strcmp(payload_json["door"],"left")==0) {
      //toggle left relay
        toggle(relayPinL);
      }
      else if(strcmp(payload_json["door"],"right")==0) {
        //toggle right relay
        toggle(relayPinR);
      }
     }
  }
}

void connectMQTT() {
  Serial.println(F("Connecting to MQTT server"));
  if (client.connect(MQTT::Connect("arduinoClient")
     .set_auth(mqttuser, mqttpass))) {
     Serial.println(F("Connected to MQTT server"));
     client.set_callback(callback);
     client.subscribe(inTopic);  
  } else {
     Serial.println(F("Could not connect to MQTT server"));  
  }
}

void setup()
{ Serial.begin(115200); 
  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.print(F("Connecting to "));
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
  }
  Serial.println();
  Serial.println(F("WiFi connected"));
  Serial.println(WiFi.localIP());
 
  if (!client.connected()) {
      connectMQTT();
  }

  //OTA
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.setPassword(ota_pass);
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println(F("Auth Failed"));
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println(F("Begin Failed"));
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println(F("Connect Failed"));
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println(F("Receive Failed"));
    } else if (error == OTA_END_ERROR) {
      Serial.println(F("End Failed"));
    }
  });
  ArduinoOTA.begin();

  //Relays
  //add more here if you have more than 2 relays defined (more than two doors)
  pinMode(relayPinR,OUTPUT);digitalWrite(relayPinR,LOW);
  pinMode(relayPinL,OUTPUT);digitalWrite(relayPinL,LOW);
}

//this is called from the loop() for each garagedoor sensor.
void publishReading(const char* theTopic, float reading) {
  dtostrf(reading,6,2,temp);
  Serial.print(F("Publishing: " ));
  Serial.print(theTopic);
  Serial.print(F(" => "));
  Serial.println(temp);
  client.publish(theTopic,temp); //publish
}

void loop()
{ 
  //OTA
  ArduinoOTA.handle();
  
  //CONNECT TO MQTT
  if(client.connected()) {
    if(millis() > time_now + (wait_seconds*1000)) {
      time_now = millis();
      
      //this is for two garagedoors, add more here if you need them.
      Serial.print(F("reading right: "));
      Serial.println(hc_right.dist());
      Serial.print(F("reading left: "));
      Serial.println(hc_left.dist());
      publishReading(outTopicR,hc_right.dist()); //publish reading for right garagedoor
      publishReading(outTopicL,hc_left.dist()); //publish reading for left garagedoor
   }
        
    client.loop();
  }
  else {
    connectMQTT();
  }
  
}

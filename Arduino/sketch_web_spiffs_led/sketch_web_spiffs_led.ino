#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <ESP8266WiFiMulti.h> 
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <DNSServer.h>
#include <WebSocketsServer.h>


#define BME_SCK 13
#define BME_MISO 12
#define BME_MOSI 11
#define BME_CS 10


#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_BME280 bme;

ESP8266WebServer server;
uint8_t pin_led = 2;
char* ssid = "imola21";
char* password = "ingyenwifi";
bool ota_flag = true;
uint16_t time_elapsed = 0;
char* host = "esp8266";
unsigned long delayTime;
const char* ssid2 = "Esp8266";
const char* password2 = "pw";
const IPAddress ip(192, 168, 0, 1);
const IPAddress mask(255,255,255,0);
long mytime=0;
bool led=true;

DNSServer dns;
WebSocketsServer ws(81);
WebSocketsServer sensorws(82);
TwoWire twi;



void setup()
{
  //setup base
  SPIFFS.begin();
  pinMode(pin_led, OUTPUT);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid,password);
  Serial.begin(115200);
  initWS();
  mytime  = millis();
  while(WiFi.status()!=WL_CONNECTED)
  {
    Serial.print(".");
    delay(500);
     if (millis() - mytime > 5000) {
          WiFi.disconnect();
          WiFi.mode(WIFI_AP);
          WiFi.softAPConfig(ip, ip, mask);
          WiFi.softAP(ssid2, password2);
          dns.setErrorReplyCode(DNSReplyCode::NoError);
          dns.start(53, "*",  ip);
          server.on("/", HTTP_GET, []{
            File f = SPIFFS.open("/cp.html", "r");
            server.streamFile(f, "text/html");
            f.close();
          });

          server.on("/", HTTP_POST, []{
            File f = SPIFFS.open("/cppost.txt", "w");
            f.println(server.arg("SSID"));
            f.println(server.arg("PW"));
            f.close();

            server.send(200, "text/plain", "rst esp8266");
          });
        server.begin();
        while(true)
        {
          dns.processNextRequest();
          server.handleClient();
        }
     }
  }


  Serial.println("");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());


  bool status;
  twi = TwoWire();
  twi.begin(D3,D4);
  status = bme.begin(0x76, &twi);  
    if (!status) {
        Serial.println("Could not find a valid BME280 sensor, check wiring!");
        while (1);
    }

  //MDNS beállítása
  if (MDNS.begin(host)) {              
    Serial.println("mDNS responder started");
  } else {
    Serial.println("Error setting up MDNS responder!");
  }

  //OTA beállítása
  ArduinoOTA.setPassword((const char *)"000");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
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
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();

  
  server.on("/",serveIndexFile);
  server.on("/ledstate",getLEDState);
  server.onNotFound(handleNotFound); 
  server.begin();
  
}

void loop()
{
  server.handleClient();
  printValues();
    delay(delayTime);
  if (ota_flag) {
    while (time_elapsed < 150000) {
      ArduinoOTA.handle();
      time_elapsed = millis();
      delay(10);
    }
    ota_flag = false;
  }
  ws.loop();
}

void serveIndexFile()
{
  File f = SPIFFS.open("/index.html","r");
  server.streamFile(f, "text/html");
  f.close();
}

void toggleLED()
{
  digitalWrite(pin_led,!digitalRead(pin_led));
  Serial.println("toggleingLed");
  Serial.println(digitalRead(pin_led));
  Serial.println(WiFi.RSSI());
}

void handleNotFound(){
  server.send(404, "text/plain", "404: Not found"); 
}

void getLEDState()
{
  toggleLED();
  String led_state = digitalRead(pin_led) ? "OFF" : "ON";
  server.send(200,"text/plain", led_state);
 
}


void printValues() {
  File file = SPIFFS.open("/datas.txt", "w");
  if (!file) {
      Serial.println("file open failed");
  }
  Serial.println(" Writing... ");

  for (int i=1; i<=25; i++){
    file.println(millis());
    file.println(bme.readTemperature());
    file.println(bme.readPressure() / 100.0);
    file.println(bme.readHumidity());
  }

  file.close();
  Serial.println("datas file done");
}


void initWS(){
  ws.begin();
  ws.onEvent(wsHandler);
  sensorws.begin();
  sensorws.onEvent(sensorWsHandler);
}

void wsHandler(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght){
   switch (type) {
  case WStype_DISCONNECTED:           
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {             
        IPAddress ipWS = ws.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ipWS[0], ipWS[1], ipWS[2], ipWS[3], payload);
      }
      break;
    case WStype_TEXT: 
      Serial.printf("[%u] get Text: %s\n", num, payload);
      if (payload[0] == 'O') {
          digitalWrite(pin_led, led);
          led = !led;
      }
      break;
   }
}


void sensorWsHandler(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght){
   switch (type) {
  case WStype_DISCONNECTED:           
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {             
        IPAddress ipWS = ws.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ipWS[0], ipWS[1], ipWS[2], ipWS[3], payload);
        File f=SPIFFS.open("/datas.txt", "r");

        
        f.close();
      }
      break;
    case WStype_TEXT: 
      break;
   }
}

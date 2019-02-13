extern "C" {
  #include "user_interface.h"  // Required for wifi_station_connect() and RTC read/write to work
}
#include <OneWire.h>
#include <ESP8266WiFi.h>

#define FPM_SLEEP_MAX_TIME 0xFFFFFFF

OneWire ds(4);  // Temp on D1
IPAddress staticIP(192,168,1,203);
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);
IPAddress hubIP(192,168,1,14);
const unsigned int hubPort = 39500;

//Include wifi login info here
const char* wifiSSID = "placeholder";
const char* wifipassword = "placeholder";

const int refreshTime = 60; //Check temp every X seconds
const int maxRefreshCycles = 10; //Force update every x refreshes
float minTempDelta = 1.0; //Minimum deltaTemp to update

typedef struct {
  byte realFlag;
  int refreshCount;
  float oldtemp;
} rtcStore;

void setup(void) {
  byte addr[8];
  
  Serial.begin(115200);
  Serial.setTimeout(2000);
  //Wait for serial
  while(!Serial) {}

  Serial.println();
  Serial.println("I'm awake");

  rtcStore rtcMem;
  bool updateTemp = false;
  system_rtc_mem_read(64, &rtcMem, sizeof(rtcMem)); //Get saved memory from RTC

  //Tell sensor to get temp reading
  startTempRead(addr);
  delay(800);
  float currenttemp = readSensor(addr);
  Serial.print("Current temp is ");
  Serial.println(currenttemp);
  Serial.print("Last sent temp is ");
  Serial.println(rtcMem.oldtemp);
  Serial.print("Last transmitted ");
  Serial.print(rtcMem.refreshCount);
  Serial.println(" refreshes ago");

  //Use 126 as marker to check if this is the first boot
  if (rtcMem.realFlag != 126) {
    rtcMem.realFlag = 126;
    rtcMem.refreshCount = 0;
    Serial.println("First boot detected");
    updateTemp = true;
  }

  if (~updateTemp && (abs(currenttemp - rtcMem.oldtemp) >= minTempDelta)) {
    Serial.println("Temperature delta reached");
    updateTemp = true;
  }

  if (~updateTemp && (rtcMem.refreshCount >= maxRefreshCycles)) {
    Serial.println("Max refreshes reached");
    updateTemp = true;
  }

  if (updateTemp) {
    rtcMem.oldtemp = currenttemp;
    rtcMem.refreshCount = 0;
    Serial.println("Preparing to send temp to ST");

    //Connect to wifi
    WiFiOn();
    WifiConnect();
    WiFiClient st_client;
  
    String message = String(currenttemp);
    message = String(message + ',');
  
    if (st_client.connect(hubIP, hubPort)) { //Connect to hub. Return false if connect failed
      Serial.print("Sending ");
      Serial.println(message);
      st_client.println(F("POST / HTTP/1.1"));
      st_client.print(F("HOST: "));
      st_client.print(hubIP);
      st_client.print(F(":"));
      st_client.println(hubPort);
      st_client.println(F("CONTENT-TYPE: text"));
      st_client.print(F("CONTENT-LENGTH: "));
      st_client.println(message.length());
      st_client.println();
      st_client.println(message);
    }
    else {
      Serial.println("Failed to connect to ST");
    }
  }
  else {
    rtcMem.refreshCount += 1;
    Serial.println("No reason to update. Going back to sleep");
  }

  WiFiOff();

  system_rtc_mem_write(64, &rtcMem, sizeof(rtcMem)); //Write memory to RTC

  Serial.println("Going to sleep...");
  Serial.println();
  ESP.deepSleep(1e6 * refreshTime, WAKE_RF_DEFAULT);
}


void loop() {}

bool startTempRead(byte addr[8]) {
  ds.reset_search();
  if (!ds.search(addr)) {
    Serial.println("No sensors found");
    return false;
  }
  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);
  pinMode(4, OUTPUT);
  return true;
}

float readSensor(byte addr[8]) {
  byte data[12];
  pinMode(4, INPUT);
  ds.reset();
  ds.select(addr);
  ds.write(0xBE);         // Read Scratchpad

  data[0] = ds.read();
  data[1] = ds.read();
  ds.reset();

  int16_t raw = (data[1] << 8) | data[0];
  
  float fahrenheit = ((float)raw / 16.0)*9.0/5.0+32.0;
  //Serial.print("  Temperature = ");
  //Serial.print(fahrenheit);
  //Serial.println(" Fahrenheit");
  return fahrenheit;
}

void WifiConnect() {
  Serial.print("Connecting");
  WiFi.begin(wifiSSID, wifipassword);
  WiFi.config(staticIP, gateway, subnet);
  while (WiFi.status() != WL_CONNECTED)
  {
    if (WiFi.status() == WL_CONNECT_FAILED) {
      Serial.println();
      Serial.println("Failed to connect to WiFi.");
      return;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC Addr: ");
  Serial.println(WiFi.macAddress());
}

void WiFiOn() {

  wifi_fpm_do_wakeup();
  wifi_fpm_close();

  //Serial.println("Reconnecting");
  wifi_set_opmode(STATION_MODE);
  wifi_station_connect();
}


void WiFiOff() {

  //Serial.println("diconnecting client and wifi");
  //client.disconnect();
  wifi_station_disconnect();
  wifi_set_opmode(NULL_MODE);
  wifi_set_sleep_type(MODEM_SLEEP_T);
  wifi_fpm_open();
  wifi_fpm_do_sleep(FPM_SLEEP_MAX_TIME);

}

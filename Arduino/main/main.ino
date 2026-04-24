
#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <time.h>

#include "dmi_prognose.h"
#include "wind_sensor.h" 

#define WIND_SENSOR_PIN 34 

// WiFi legualiteter
#define WIFI_SSID "OnePlus 8 Pro"
#define WIFI_PASSWORD "123456789"

// Firebase legualiteter
#define Web_API_KEY "AIzaSyCkUX5c1GTO6CX1z1ar1f_S0aaAFJVeCsg"
#define DATABASE_URL "https://wind-alarm-default-rtdb.europe-west1.firebasedatabase.app"
#define USER_EMAIL "asger.frimor@gmail.com"
#define USER_PASS "123456789"

void processData(AsyncResult &aResult);

UserAuth user_auth(Web_API_KEY, USER_EMAIL, USER_PASS);
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;

// --- TIMERE ---
unsigned long lastDmiAttempt = 0;
const unsigned long dmiNormalInterval = 3600000; // 1 time
const unsigned long dmiRetryInterval = 120000;   // 5 minutter ved fejl (ændret til 5 min)

unsigned long lastSensorTime = 0;
const unsigned long sensorInterval = 5000;       // 5 sekunder

unsigned long lastWifiCheck = 0;                 // Ny timer til at tjekke WiFi
const unsigned long wifiCheckInterval = 10000;   // Vent 10 sekunder mellem reconnect-forsøg

// --- SENSOR VARIABLER ---
float currentWindSpeed = 0.0;
bool alarmActive = false;

bool forceFirstDmi = true;
bool previousDmiFailed = false;

const char* ntpServer = "pool.ntp.org";

void initTime() {
  Serial.print("Henter tid fra NTP...");
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", ntpServer); 
  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 10) {
    Serial.print(".");
    delay(1000);
    retry++;
  }
  if (retry < 10) {
    Serial.println("\nTid synkroniseret!");
  }
}

void setup(){
  Serial.begin(115200);

  // Slå strømbesparelse fra og bed ESP32 om automatisk at genoprette WiFi
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true); 

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println("\nForbundet til WiFi!");
  initWindSensor(WIND_SENSOR_PIN); 
  
  initTime();
  
  ssl_client.setInsecure();
  ssl_client.setConnectionTimeout(15000); 
  ssl_client.setHandshakeTimeout(15);
  
  initializeApp(aClient, app, getAuth(user_auth), processData, "🔐 authTask");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);
}

void loop(){
  app.loop();
  
  unsigned long currentTime = millis();

  // --- 0. SIKKERHEDSNET: TJEK WIFI FORBINDELSEN ---
  if (WiFi.status() != WL_CONNECTED) {
    // Hvis vi har mistet forbindelsen, og der er gået 10 sekunder siden sidst vi prøvede
    if (currentTime - lastWifiCheck >= wifiCheckInterval) {
      Serial.println("Advarsel: WiFi mistet! Prøver at genoprette...");
      WiFi.disconnect();
      WiFi.reconnect();
      lastWifiCheck = currentTime;
    }
    // Return afbryder resten af loopet, så vi ikke prøver at hente DMI eller sende til Firebase uden net
    return; 
  }

  // --- NÅR VI HAR WIFI, KØRER RESTEN AF PROGRAMMET ---
  if (app.ready()){ 
    
    // --- 1. LIVE SENSOR DATA (Hvert 5. sekund) ---
    // --- 1. LIVE SENSOR DATA (Hvert 5. sekund) ---
    if (currentTime - lastSensorTime >= sensorInterval) {
      lastSensorTime = currentTime;

      // Hent den faktiske vindhastighed fra vores sensor-fil
      currentWindSpeed = getWindSpeed(WIND_SENSOR_PIN); // <-- ÆNDRET LINJE
      alarmActive = (currentWindSpeed > 10);  

      Serial.printf("Live data: %.2f m/s, Alarm: %s\n", currentWindSpeed, alarmActive ? "JA" : "NEJ");

      Database.set<float>(aClient, "/live/wind_speed", currentWindSpeed, processData, "RTDB_Live_Wind");
      Database.set<bool>(aClient, "/live/alarm_triggered", alarmActive, processData, "RTDB_Live_Alarm");
      
      // Valgfri besked opdatering
      String alarmMsg = alarmActive ? "Kritisk vindhastighed overskredet!" : "Vindhastighed normal";
      Database.set<String>(aClient, "/live/message", alarmMsg, processData, "RTDB_Live_Alarm_Msg");
    }

    // --- 2. DMI PROGNOSE ---
    unsigned long currentDmiInterval = previousDmiFailed ? dmiRetryInterval : dmiNormalInterval;
    
    if (currentTime - lastDmiAttempt >= currentDmiInterval || forceFirstDmi){
      lastDmiAttempt = currentTime;
      forceFirstDmi = false;
      
      bool success = send_prognose(aClient, Database, processData);
      
      if (success) {
        previousDmiFailed = false; 
      } else {
        previousDmiFailed = true;  
      }
    }
  }
}

void processData(AsyncResult &aResult) {
  if (!aResult.isResult()) return;
  
  if (aResult.isError()) {
    Firebase.printf("Firebase Fejl: %s, code: %d\n", aResult.error().message().c_str(), aResult.error().code());
  }
}
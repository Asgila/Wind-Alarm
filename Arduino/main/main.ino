#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <time.h>

#include "dmi_prognose.h"
#include "wind_sensor.h" 
#include "mqtt.h"            // Tilføjet MQTT
#include "stepper_control.h" // Tilføjet Stepper motor

#define WIND_SENSOR_PIN 34 

// WiFi og Firebase legitimationsoplysninger
#define WIFI_SSID "Helens iPhone"
#define WIFI_PASSWORD "ABCD2310"
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
const unsigned long dmiNormalInterval = 3600000; 
const unsigned long dmiRetryInterval = 120000;  

unsigned long lastSensorTime = 0;
const unsigned long sensorInterval = 5000;       

unsigned long lastWifiCheck = 0;                 
const unsigned long wifiCheckInterval = 10000;   

// --- SENSOR & ALARM VARIABLER ---
float currentWindSpeed = 0.0;
bool alarmActive = false; // Den faktiske alarm state vi sender til Firebase

bool forceFirstDmi = true;
bool previousDmiFailed = false;

// --- (Vind logik over tid) ---
int highWindCount = 0;
unsigned long cooldownUntil = 0;
bool previousCombinedAlarm = false; // Bruges til at trigge stepperen, når status ændres

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
  
  // init stepper og MQTT
  initStepper();
  mqtt_setup();

  ssl_client.setInsecure();
  ssl_client.setConnectionTimeout(15000); 
  ssl_client.setHandshakeTimeout(15);
  
  initializeApp(aClient, app, getAuth(user_auth), processData, "🔐 authTask");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);
}

void loop(){
  app.loop();
  mqtt_loop();
  loopStepper(); // Styrer motor bevægelsen trin for trin
  
  unsigned long currentTime = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (currentTime - lastWifiCheck >= wifiCheckInterval) {
      Serial.println("Advarsel: WiFi mistet! Prøver at genoprette...");
      WiFi.disconnect();
      WiFi.reconnect();
      lastWifiCheck = currentTime;
    }
    return; 
  }

  if (app.ready()){ 
    
    // --- 1. LIVE SENSOR DATA (Hvert 5. sekund) ---
    if (currentTime - lastSensorTime >= sensorInterval) {
      lastSensorTime = currentTime;

      currentWindSpeed = getWindSpeed(WIND_SENSOR_PIN); 
      
      // --- LOGIK FOR 10 MÅLINGER & 1 TIMES COOLDOWN ---
      if (currentWindSpeed > 5.0) {
        highWindCount++;
      } else {
        highWindCount = 0; 
      }

      bool windAlarmActive = false;
      if (highWindCount >= 5) {
        // Vi har ramt 10 målinger i træk med blæst. Start 1 times cooldown (3.600.000 ms)
        cooldownUntil = currentTime + 3600000;
        highWindCount = 0; 
      }
      
      if (currentTime < cooldownUntil) {
        windAlarmActive = true;
      }

      // --- SAMLET ALARM VURDERING ---
      bool combinedAlarm = windAlarmActive || dashboardSwitchState;
      alarmActive = combinedAlarm; 

      // Tjek om der er sket en ændring i tilstanden. Hvis ja, igangsæt motoren.
      if (combinedAlarm != previousCombinedAlarm) {
        setStepperTarget(combinedAlarm, dashboardSliderValue);
        previousCombinedAlarm = combinedAlarm;
      }

      Serial.printf("Live data: %.2f m/s | Vindalarm: %s | MQTT Switch: %s | Samlet Alarm: %s\n", 
                    currentWindSpeed, 
                    windAlarmActive ? "JA" : "NEJ", 
                    dashboardSwitchState ? "JA" : "NEJ",
                    alarmActive ? "JA" : "NEJ");

      // Opdatering af Firebase
      Database.set<float>(aClient, "/live/wind_speed", currentWindSpeed, processData, "RTDB_Live_Wind");
      Database.set<bool>(aClient, "/live/alarm_triggered", alarmActive, processData, "RTDB_Live_Alarm");
      
      String alarmMsg = alarmActive ? "Kritisk vind/MQTT aktiveret!" : "Alt er normalt";
      Database.set<String>(aClient, "/live/message", alarmMsg, processData, "RTDB_Live_Alarm_Msg");
    }

    // --- 2. DMI PROGNOSE ---
    unsigned long currentDmiInterval = previousDmiFailed ? dmiRetryInterval : dmiNormalInterval;
    
    if (currentTime - lastDmiAttempt >= currentDmiInterval || forceFirstDmi){
      lastDmiAttempt = currentTime;
      forceFirstDmi = false;
      
      bool success = send_prognose(aClient, Database, processData);
      previousDmiFailed = !success;
    }
  }
}

void processData(AsyncResult &aResult) {
  if (!aResult.isResult()) return;
  
  if (aResult.isError()) {
    Firebase.printf("Firebase Fejl: %s, code: %d\n", aResult.error().message().c_str(), aResult.error().code());
  }
}
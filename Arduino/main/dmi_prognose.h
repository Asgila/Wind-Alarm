#ifndef DMI_PROGNOSE_H
#define DMI_PROGNOSE_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <FirebaseClient.h>
#include <time.h>

time_t parseDMITime(String dmiTimeStr) {
  struct tm t = {0};
  t.tm_year = dmiTimeStr.substring(0, 4).toInt() - 1900;
  t.tm_mon  = dmiTimeStr.substring(5, 7).toInt() - 1;
  t.tm_mday = dmiTimeStr.substring(8, 10).toInt();
  t.tm_hour = dmiTimeStr.substring(11, 13).toInt();
  t.tm_min  = 0; 
  t.tm_sec  = 0;

  setenv("TZ", "UTC", 1);
  tzset();
  time_t epoch = mktime(&t); 
  
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  
  return epoch; 
}

bool send_prognose(AsyncClientClass &aClient, RealtimeDatabase &Database, AsyncResultCallback processData) {
  Serial.println("\n--- Starter hentning af DMI prognose ---");

  time_t now;
  time(&now);
  
  if (now < 1704067200) { 
    Serial.println("Fejl: ESP32 uret er ikke synkroniseret endnu. Prøver igen senere.");
    return false;
  }

  
  // Vi bruger gmtime() for at få UTC-tid, som DMI's API kræver
  struct tm *tm_utc_now = gmtime(&now);
  char startStr[24];
  // Formaterer som DMI forventer det, f.eks.: "2026-04-18T08:00:00Z"
  strftime(startStr, sizeof(startStr), "%Y-%m-%dT%H:00:00Z", tm_utc_now);

  // Find tidspunktet for præcis 24 timer ude i fremtiden
  time_t tomorrow = now + (24 * 3600); 
  struct tm *tm_utc_tomorrow = gmtime(&tomorrow);
  char endStr[24];
  strftime(endStr, sizeof(endStr), "%Y-%m-%dT%H:00:00Z", tm_utc_tomorrow);

  // Sæt det hele sammen til en URL
  String dmi_url = "https://opendataapi.dmi.dk/v1/forecastedr/collections/harmonie_dini_sf/position?coords=POINT%2812.07%2055.61%29&crs=crs84&parameter-name=wind-speed,gust-wind-speed-10m&f=GeoJSON&datetime=";
  dmi_url += startStr;
  dmi_url += "/";
  dmi_url += endStr;

  Serial.println("Spørger DMI om perioden: " + String(startStr) + " til " + String(endStr));

  WiFiClientSecure client;
  client.setInsecure(); 
  HTTPClient http;
  
  http.setTimeout(45000); 
  
  // Vi kalder nu med vores nye URL
  if (http.begin(client, dmi_url)) {
    int httpCode = http.GET();
    
    if (httpCode > 0) {
      String payload = http.getString();
      
      if (httpCode == HTTP_CODE_OK || httpCode == 200) {
        Serial.println("DMI API svarede med succes (HTTP 200)! Mindre datasæt modtaget.");
        
        JsonDocument doc; 
        DeserializationError error = deserializeJson(doc, payload);
        
        if (error) {
          Serial.print("JSON fejl: ");
          Serial.println(error.c_str());
          http.end();
          return false; 
        }

        JsonDocument firebasePayload;
        JsonArray features = doc["features"];
        int timerGemt = 0;
        
        for (JsonObject feature : features) {
          String timestampStr = feature["properties"]["step"].as<String>();
          time_t forecastTime = parseDMITime(timestampStr); 
          
          if (forecastTime >= (now - 3600)) {
            if (timerGemt >= 24) break; 
            
            float windSpeed = feature["properties"]["wind-speed"].as<float>();
            float gustWind = feature["properties"]["gust-wind-speed-10m"].as<float>();
            
            struct tm *local_tm = localtime(&forecastTime);
            char timeString[24];
            strftime(timeString, sizeof(timeString), "%Y-%m-%dT%H:%M:%S", local_tm);
            String firebaseKey = String(timeString);
            
            JsonObject timeNode = firebasePayload[firebaseKey].to<JsonObject>();
            timeNode["wind"] = windSpeed;
            timeNode["gust"] = gustWind;
            
            timerGemt++;
          }
        }
        
        if (timerGemt > 0) {
          String jsonString;
          serializeJson(firebasePayload, jsonString);
          Serial.printf("Sender %d timers DANSK prognose til Firebase... \n", timerGemt);
          Database.set<object_t>(aClient, "/vejr_prognose", object_t(jsonString), processData, "RTDB_Set_Prognose");
        }
        
        http.end();
        return true; 
        
      } else {
        Serial.printf("DMI afviste anmodningen med HTTP kode: %d\n", httpCode);
        http.end();
        return false; 
      }
      
    } else {
      Serial.printf("Intern ESP32 HTTP fejl, kode: %d (%s)\n", httpCode, http.errorToString(httpCode).c_str());
      http.end();
      return false; 
    }
  } else {
    Serial.println("Kunne ikke oprette den grundlæggende forbindelse til DMI's server.");
    return false;
  }
}

#endif
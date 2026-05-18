#ifndef WIND_SENSOR_H
#define WIND_SENSOR_H

#include <Arduino.h>

// Konstanter for vores setup
const float MAX_SENSOR_VOLTAGE = 3.8;
const float MAX_WIND_SPEED = 90; //Bil kalibrering 
const float R1 = 10000.0; // 10k ohm
const float R2 = 47000.0; // 47k ohm
const float ESP32_REF_VOLTAGE = 3.3; // ESP32's reference spænding for ADC

void initWindSensor(uint8_t pin) {
  pinMode(pin, INPUT);
}

float getWindSpeed(uint8_t pin) {
  // 1. Tag gennemsnittet af 10 målinger for at undgå "støj"
  long sum = 0;
  for(int i = 0; i < 10; i++) {
    sum += analogRead(pin);
    delay(5);
  }
  float avgAdcValue = sum / 10.0;


  // ESP32 har en 12-bit ADC, hvilket betyder værdier fra 0 til 4095
  float pinVoltage = (avgAdcValue / 4095.0) * ESP32_REF_VOLTAGE;

  float sensorVoltage = pinVoltage * ((R1 + R2) / R2);

  // Sørg for at vi ikke regner med mere end max spændingen ved udsving
  if (sensorVoltage > MAX_SENSOR_VOLTAGE) {
    sensorVoltage = MAX_SENSOR_VOLTAGE;
  }

  // 4. Konverter sensorens spænding (0-3.8V) til vindhastighed (0-30 m/s)
  float windSpeed = (sensorVoltage / MAX_SENSOR_VOLTAGE) * MAX_WIND_SPEED;

  // Fjern eventuelle negative værdier der kan opstå pga. elektrisk støj ved 0 m/s
  if (windSpeed < 0) {
    return 0.0;
  }

  return windSpeed;
}

#endif
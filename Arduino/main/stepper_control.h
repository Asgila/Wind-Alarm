#ifndef STEPPER_CONTROL_H
#define STEPPER_CONTROL_H

#include <AccelStepper.h>

// 1. Forbindelser til ESP32 til ULN2003 boardet:
#define IN1 19
#define IN2 21
#define IN3 22
#define IN4 23

// 2. Her bytter vi om på IN2 og IN3 i selve kodelinjen for korrekt rækkefølge
AccelStepper stepper(AccelStepper::FULL4WIRE, IN1, IN3, IN2, IN4);

const int stepsPerRevolution = 2048; 

void initStepper() {
  stepper.setMaxSpeed(300.0);    
  stepper.setAcceleration(100.0); 
}

// Funktionen modtager nu en 'int' (f.eks. 5, hvilket betyder 0.5 omgange)
void setStepperTarget(bool isAlarm, int inputFactor10) {
  if (isAlarm) {
    float actualRevolutions = inputFactor10 * 0.1; 
    
    long targetSteps = actualRevolutions * stepsPerRevolution; 
    
    stepper.moveTo(targetSteps);
    Serial.printf("Hejser ned: %.1f omdrejninger (%ld steps)\n", actualRevolutions, targetSteps);
  } else {
    stepper.moveTo(0);
    Serial.println("Hejser op: Returnerer til start");
  }
}

void loopStepper() {
  stepper.run(); 
}

#endif
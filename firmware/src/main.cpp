#include <Arduino.h>
#include "estimator/IStateEstimator.h"
#include "drivers/IImuSource.h"

using namespace revali;

void setup() {
    Serial.begin(115200);
    VehicleState s;              // default-constructed, all zeros
    Serial.println("Revali hopcopter FC starting...");
    Serial.printf("state valid=%d yaw=%.2f height=%.2f contact=%d\n",
                  s.valid, s.yaw_rad, s.height_m, s.ground_contact);
}

void loop() {}
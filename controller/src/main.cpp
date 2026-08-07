#include <Arduino.h>
#include "link/protocol.h"

using namespace revali::link;

void setup() {
    Serial.begin(115200);
    CmdControl cmd;  // default-constructed, all neutral
    Serial.println("Revali controller starting...");
    Serial.printf("CmdControl wire size=%u bytes, request=%d\n",
                  (unsigned)sizeof(cmd), cmd.request);
}

void loop() {}

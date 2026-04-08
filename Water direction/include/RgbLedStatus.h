#ifndef RGB_LED_STATUS_H
#define RGB_LED_STATUS_H

#include <Arduino.h>

void rgbLedStatusInit();
void rgbLedStatusUpdate(uint32_t nowMs);

#endif

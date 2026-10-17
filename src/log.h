// log.h - minimal serial logging helpers.
#pragma once
#include <Arduino.h>

#define LOG(fmt, ...)  Serial.printf_P(PSTR(fmt "\n"), ##__VA_ARGS__)

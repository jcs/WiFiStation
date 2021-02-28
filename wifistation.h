#ifndef __WIFISTATION_H__
#define __WIFISTATION_H__

#include <ESP8266WiFi.h>

/* these are ESP8266 pins */
const int pRedLED   =  0;
const int pBlueLED  =  2;

/* cache data pin direction */
static int data_mode;

/* wifistation.ino */
void exec_cmd(char *cmd, size_t len);

/* util.cpp */
void led_setup(void);
void led_reset(void);
void error_flash(void);
size_t outputf(const char *format, ...);
int output(char c);
int output(char *str);
int output(String str);

#endif

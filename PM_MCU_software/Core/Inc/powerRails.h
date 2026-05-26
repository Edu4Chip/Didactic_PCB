/*
 * powerRails.h
 *
 *  Created on: Jan 23, 2026
 *      Author: keskiner
 */

#ifndef INC_POWERRAILS_H_
#define INC_POWERRAILS_H_

#include <stdint.h>
#include "stm32g4xx_hal.h"
#include "main.h"

#define SAMPLE_COUNT 40
#define POWER_RAIL_COUNT 3
#define CURRENT_AMPLIFICATION 200.0
#define CURRENT_CHANNEL_COUNT 3




/*
 * powerRails.c
 *
 *  Created on: Jan 23, 2026
 *      Author: keskiner
 */

#include "powerRails.h"
#include <string.h>
#include <stdarg.h> //for va_list var arg functions
#include <math.h>
#include <stdio.h>
#include "structs.h"

uint8_t getPowerStatus();
void setPrintInfoFlag();
void setReset(uint8_t state);
void setEnable(struct powerRail *rail, uint8_t state);
void turnOff();
void shutDown(uint8_t ID);
void startUp();
void addString(char *dest, char *source, uint8_t len);
void addFloat(char *dest, float num, uint8_t len);
void addInt(char *dest, uint32_t num, uint8_t len);
void printInfo();
void printVoltages();
void greenLEDLogic();
void redLEDLogic();
void USB_receive();
uint8_t enableClock();
void disableClock();
void setTargetVoltage(uint8_t railID, float voltage);
void setMaxCurrent(uint8_t railID, float current);
void setTolerance(uint8_t railID, float tolerance);







#endif /* INC_POWERRAILS_H_ */

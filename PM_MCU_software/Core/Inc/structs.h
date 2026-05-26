/*
 * structs.h
 *
 *  Created on: May 22, 2026
 *      Author: roope
 */

#ifndef INC_STRUCTS_H_
#define INC_STRUCTS_H_

#include "stm32g4xx_hal.h"

enum EN_IDs {
 CORE = 0, IO = 1, IO_3V3 = 2, ANALOG = 3
};
enum POWER_RAIL_STAUS {
	OFF, STARTING, ON, FAILURE, NOTUSED
};

typedef struct powerRail {
	uint8_t ID;
	char name[20];
	GPIO_TypeDef *EnPort;
	uint16_t EnPin;
	float volatge;
	float voltageMultiplier;
	float targetVoltage;
	uint16_t startUpDelay;
	float current;
	float maxCurrent;
	uint8_t status;
	float tolerance;
	float shuntValue;
	uint8_t voltageADCIndex;
	uint8_t currentADCIndex;

} powerRail_t;



#endif /* INC_STRUCTS_H_ */

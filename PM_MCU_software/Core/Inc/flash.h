/*
 * flash.h
 *
 *  Created on: May 22, 2026
 *      Author: roope
 */

#ifndef INC_FLASH_H_
#define INC_FLASH_H_

#include "structs.h"

void writePowerRailDataToFlash(powerRail_t *powerRails[], uint8_t count);
void readPowerRailDataFromFlash(powerRail_t *powerRails[], uint8_t count);
void writeFloatToFlash(uint32_t address, float val);
void saveDefaultValues(powerRail_t * powerRails[], uint8_t count);
void restoreDefaultValues(powerRail_t * powerRails[], uint8_t count);



#endif /* INC_FLASH_H_ */

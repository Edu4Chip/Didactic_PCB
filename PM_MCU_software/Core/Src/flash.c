/*
 * flash.c
 *
 *  Created on: May 22, 2026
 *      Author: roope
 */

#include "flash.h"
#include "stm32g4xx_hal.h"
#include "usefulFunctions.h"

#define FlashPage 63
#define FlashWriteOffset 8
#define FlastStartAddress 0x0801F800
#define FlashPowerRailOffset 16

extern volatile int32_t requestedClockSpeed;

float defaultValues[30];

void saveDefaultValues(powerRail_t *powerRails[], uint8_t count) {
	for (uint8_t powerRailID = 0; powerRailID < count; powerRailID++) {
		defaultValues[powerRailID * 10 + 0] =
				powerRails[powerRailID]->targetVoltage;
		defaultValues[powerRailID * 10 + 1] =
				powerRails[powerRailID]->maxCurrent;
		defaultValues[powerRailID * 10 + 2] =
				powerRails[powerRailID]->tolerance;
	}
	smartUSBPrint("\r\n Default values saved \r\n");
}

void restoreDefaultValues(powerRail_t *powerRails[], uint8_t count) {
	for (uint8_t powerRailID = 0; powerRailID < count; powerRailID++) {
		powerRails[powerRailID]->targetVoltage = defaultValues[powerRailID * 10
				+ 0];
		powerRails[powerRailID]->maxCurrent =
				defaultValues[powerRailID * 10 + 1];
		powerRails[powerRailID]->tolerance =
				defaultValues[powerRailID * 10 + 2];
	}
	smartUSBPrint("\r\n Default values restored \r\n");
}

void writeFloatToFlash(uint32_t address, float val) {
	uint32_t data = *((uint32_t*) (&val));
	HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, data);
}

float readFloatFromFlash(uint32_t address) {
	uint32_t temp = *(__IO uint32_t*) address;
	float dataf = *((float*) (&temp));
	return dataf;
}

void writePowerRailDataToFlash(powerRail_t *powerRails[], uint8_t count) {

	static FLASH_EraseInitTypeDef EraseInitStruct;
	uint32_t PAGEError;

	HAL_FLASH_Unlock();
	__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

	EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
	EraseInitStruct.Page = 63;
	EraseInitStruct.NbPages = 1;
	if (HAL_FLASHEx_Erase(&EraseInitStruct, &PAGEError) != HAL_OK) {
		/*Error occurred while page erase.*/
		return HAL_FLASH_GetError();
	}

	HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, 0x0801F800, 0xDEADBEEF);
	HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, 0x0801F800 + 8,
			(uint32_t) requestedClockSpeed);
	for (uint8_t powerRailID = 0; powerRailID < count; powerRailID++) {

		writeFloatToFlash(
				0x0801F800
						+ (FlashWriteOffset * FlashPowerRailOffset
								* (powerRailID + 1)),
				powerRails[powerRailID]->targetVoltage);
		writeFloatToFlash(
				0x0801F800
						+ (FlashWriteOffset * FlashPowerRailOffset
								* (powerRailID + 1) + FlashWriteOffset),
				powerRails[powerRailID]->maxCurrent);
		writeFloatToFlash(
				0x0801F800
						+ (FlashWriteOffset * FlashPowerRailOffset
								* (powerRailID + 1) + FlashWriteOffset * 2),
				powerRails[powerRailID]->tolerance);
	}

	HAL_FLASH_Lock();
	smartUSBPrint("\r\n Config saved to Flash\r\n");
}

void readPowerRailDataFromFlash(powerRail_t *powerRails[], uint8_t count) {

	if (*(__IO uint32_t*) 0x0801F800 != 0xDEADBEEF) {
		smartUSBPrint(
				"\r\n Can't load config from Flash. Loading default config\r\n");
	} else {

		requestedClockSpeed = *(__IO uint32_t*) (0x0801F800 + 8);
		for (uint8_t powerRailID = 0; powerRailID < count; powerRailID++) {
			powerRails[powerRailID]->targetVoltage = readFloatFromFlash(
					0x0801F800
							+ (FlashWriteOffset * FlashPowerRailOffset
									* (powerRailID + 1)));
			powerRails[powerRailID]->maxCurrent = readFloatFromFlash(
					0x0801F800
							+ (FlashWriteOffset * FlashPowerRailOffset
									* (powerRailID + 1) + FlashWriteOffset));
			powerRails[powerRailID]->tolerance =
					readFloatFromFlash(
							0x0801F800
									+ (FlashWriteOffset * FlashPowerRailOffset
											* (powerRailID + 1)
											+ FlashWriteOffset * 2));

		}

		smartUSBPrint("\r\n Config loaded from Flash\r\n");
	}

}

void Flash_Read_Data(uint32_t StartPageAddress, uint32_t *RxBuf,
		uint16_t numberofwords) {
	while (1) {
		*RxBuf = *(__IO uint32_t*) StartPageAddress;
		StartPageAddress += 4;
		RxBuf++;
		if (!(numberofwords--))
			break;
	}
}

uint32_t Flash_Write_Data(uint32_t StartPageAddress, uint32_t *Data,
		uint16_t numberofwords) {

	static FLASH_EraseInitTypeDef EraseInitStruct;
	uint32_t PAGEError;
	int sofar = 0;

	/* Unlock the Flash to enable the flash control register access *************/
	HAL_FLASH_Unlock();
	__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

	/* Erase the user Flash area*/

	//uint32_t StartPage = GetPage(StartPageAddress);
	//uint32_t EndPageAdress = StartPageAddress + numberofwords*4;
	//uint32_t EndPage = GetPage(EndPageAdress);

	/* Fill EraseInit structure*/
	EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
	EraseInitStruct.Page = 63;
	EraseInitStruct.NbPages = 1;

	if (HAL_FLASHEx_Erase(&EraseInitStruct, &PAGEError) != HAL_OK) {
		/*Error occurred while page erase.*/
		return HAL_FLASH_GetError();
	}
	HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, 0x0801F800,
			0xDEADBEEFDEADBEEF);

	/* Program the user Flash area word by word*/
	/*

	 while (sofar<numberofwords)
	 {
	 if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, StartPageAddress, Data[sofar]) == HAL_OK)
	 {
	 StartPageAddress += 4;  // use StartPageAddress += 2 for half word and 8 for double word
	 sofar++;
	 }
	 else
	 {
	 //Error occurred while writing data in Flash memory
	 return HAL_FLASH_GetError ();
	 }
	 }
	 */

	/* Lock the Flash to disable the flash control register access (recommended
	 to protect the FLASH memory against possible unwanted operation) *********/
	HAL_FLASH_Lock();

	return 0;
}


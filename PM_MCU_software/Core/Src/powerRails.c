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
#include "si5351.h"
#include "flash.h"

#define SI5351_ADDRESS 0x60
#define DEFAULT_CLOCK_SPEED 100000000

extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;
extern I2C_HandleTypeDef hi2c2;

extern void USBprintf(const char *fmt, ...);

powerRail_t sCore = { CORE, "Core", EN_CVDD_GPIO_Port, EN_CVDD_Pin, 0.0,
		1.0, 0.9, 5, 0, 0.5, OFF, 0.15, 0.033, 0, 0 };

powerRail_t sIO = { IO, "IO", EN_IOVDD_GPIO_Port,
EN_IOVDD_Pin, 0.0, 1.0, 1.8, 5, 0, 0.8, OFF, 0.250, 0.016, 1, 1 };

powerRail_t sIO3V3 = { IO_3V3, "IO 3V3", EN_IOVDD3V3_GPIO_Port,
		EN_IOVDD3V3_Pin, 0.0, 1.0, 3.3, 0, 0, 1, OFF, 0.09, 0.016, 2, 2 };

powerRail_t *powerRails[POWER_RAIL_COUNT] = { &sCore, &sIO, &sIO3V3 };

extern volatile uint16_t adcBufferVoltage[SAMPLE_COUNT * POWER_RAIL_COUNT]; //Buffer for reading ADC1 values (Voltages)
extern volatile uint16_t adcBufferCurrent[SAMPLE_COUNT * CURRENT_CHANNEL_COUNT]; //Buffer for reading ADC2 values
extern volatile uint8_t startUpPressed;
extern volatile uint8_t startClock;
extern volatile uint8_t stopClock;


uint8_t powerStatus = OFF;
volatile uint8_t powerRailFailed = 0;
powerRail_t *failedRail;
uint8_t printInfoFlag = 0;
volatile uint8_t noFault = 0;
volatile uint8_t clockStatus = 0;
volatile int32_t clockSpeed = 0;
volatile int32_t requestedClockSpeed = DEFAULT_CLOCK_SPEED;
uint8_t resetStatus = 0;

//LED variables
uint32_t redLedTimer = 0;
uint32_t redLedNextBlinkTimer = 0;
uint8_t redLedBlinkState = 0;
uint32_t greenLEDNextBlinkTime = 1000;
uint32_t greenLEDLastBlinkTime = 0;

extern uint8_t USBReceiveBuffer[64];

inline uint8_t getPowerStatus() {
	return powerStatus;
}

inline void setPrintInfoFlag() {
	printInfoFlag = 1;
}

void setTargetVoltage(uint8_t railID, float voltage){
 powerRails[railID]->targetVoltage = voltage;
}
void setMaxCurrent(uint8_t railID, float current){
 powerRails[railID]->maxCurrent = current;
}
void setTolerance(uint8_t railID, float tolerance){
 powerRails[railID]->tolerance = tolerance;
}

uint8_t enableClock() {

	if(requestedClockSpeed < 10000){
		return 1;
	}



	HAL_Delay(50);
	if(HAL_I2C_IsDeviceReady(&hi2c2, (uint16_t) (SI5351_ADDRESS << 1), 3,
			50) != HAL_OK) {
		smartUSBPrint("Clock generator not ready \r\n");
		shutDown(0);
		return 1;
	}
	clockSpeed = requestedClockSpeed;
	smartUSBPrint("Clock generator ready \r\n");
	si5351_Init(0);
	si5351_SetupCLK0(clockSpeed, SI5351_DRIVE_STRENGTH_8MA);
	//si5351_SetupCLK0(10000000, SI5351_DRIVE_STRENGTH_4MA);
	si5351_EnableOutputs(1<<0);
	clockStatus = 1;
	smartUSBPrint("Clock generator started \r\n");
	return 0;
}

void disableClock() {
	clockSpeed = 0;
	clockStatus = 0;
	si5351_EnableOutputs(0);
	smartUSBPrint("Clock generator stopped \r\n");
}

/**
 * @brief Sets the state of Tackles reset pin
 * @param state is the state to which set the reset pin. 0 = Tackle is reset, 1 = reset is released
 */

void setReset(uint8_t state) {
	if (state == 1) {
		resetStatus = 1;
		DIDACTIC_RSTN_GPIO_Port->BSRR = (uint32_t) DIDACTIC_RSTN_Pin;
	} else {
		resetStatus = 0;
		DIDACTIC_RSTN_GPIO_Port->BRR = (uint32_t) DIDACTIC_RSTN_Pin;
	}

}

/**
 * @brief Sets the state of power rail enable pin
 * @param rail is pointer to a power rail struct
 * @param state is the state to which set the enable pin. 0 = power rail is disabled, 1 = power rail is enabled
 */
void setEnable(struct powerRail *rail, uint8_t state) {
	if (state == 1) {
		rail->EnPort->BSRR = rail->EnPin;
	} else {
		rail->EnPort->BRR = rail->EnPin;
	}
}

/**
 * @brief Turns off the power rails in the reverse order of start up.
 */
void turnOff() {
	setReset(0);
	disableClock();
	powerStatus = OFF;

	uint32_t largest = 0;
	//Find the power rail with largest startup delay
	for (uint8_t i = 0; i < POWER_RAIL_COUNT; i++) {
		if (powerRails[i]->startUpDelay > largest) {
			largest = powerRails[i]->startUpDelay;
		}
	}

	uint32_t startTime = HAL_GetTick();
	while (1) {

		//Turn on powerrails
		for (uint8_t i = 0; i < POWER_RAIL_COUNT; i++) {
			if (((HAL_GetTick() - startTime)
					>= (largest - powerRails[i]->startUpDelay))
					&& powerRails[i]->status != OFF
					&& powerRails[i]->status != NOTUSED) {
				setEnable(powerRails[i], 0);
				powerRails[i]->status = OFF;
				smartUSBPrint("%i: Power rail: %s turned off\r\n",
						HAL_GetTick() - startTime, powerRails[i]->name);
			}
		}

		//Check if power rails are ready
		uint8_t allReady = 0;
		for (uint8_t i = 0; i < POWER_RAIL_COUNT; i++) {
			if (powerRails[i]->status == OFF
					|| powerRails[i]->status == NOTUSED) {
				allReady++;
			}
		}
		if (allReady == POWER_RAIL_COUNT) {
			smartUSBPrint("Startup success \r\n");
			powerStatus = OFF;
			return;
		}
	}

}

/**
 * @brief Instantly turns of all power rails
 */
void shutDown(uint8_t ID) {
	smartUSBPrint("Shutdown\r\n");
	setReset(0);
	clockSpeed = 0;
	clockStatus = 0;
	powerStatus = OFF;
	for (uint8_t i = 0; i < POWER_RAIL_COUNT; i++) {
		setEnable(powerRails[i], 0);
		if (powerRails[i]->status != NOTUSED) {
			powerRails[i]->status = OFF;
		}
	}
	powerRailFailed = 1;
	failedRail = powerRails[ID];
	printInfoFlag = 1;
}

/**
 * @brief Turns on all power rails
 */
void startUp() {

	smartUSBPrint("Startup\r\n");

	HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_RESET);
	redLedBlinkState = 0;

	for (uint8_t i = 0; i < POWER_RAIL_COUNT; i++) {
		if (powerRails[i]->status != NOTUSED) {
			powerRails[i]->status = OFF;
		}
	}
	powerRailFailed = 0;

	uint32_t startTime = HAL_GetTick();
	uint32_t startupTimes[POWER_RAIL_COUNT] = { 0 };

	while (1) {

		//Turn on powerrails
		for (uint8_t i = 0; i < POWER_RAIL_COUNT; i++) {
			if (HAL_GetTick() - startTime >= powerRails[i]->startUpDelay
					&& powerRails[i]->status == OFF
					&& powerRails[i]->status != NOTUSED) {

				setEnable(powerRails[i], 1);
				powerRails[i]->status = STARTING;
				startupTimes[i] = HAL_GetTick();
				smartUSBPrint("%i: Power rail: %s enabled\r\n",
						HAL_GetTick() - startTime, powerRails[i]->name);
			}
		}

		//Check if power rail failed to start

		for (uint8_t i = 0; i < POWER_RAIL_COUNT; i++) {
			if (powerRails[i]->status == STARTING
					&& ((HAL_GetTick() - startupTimes[i]) > 10) && !noFault) {

				powerRails[i]->status = FAILURE;
				smartUSBPrint("Startup failed \r\n");
				shutDown(i);
				return;
			}
		}

		//Check if power rails are ready
		uint8_t allReady = 0;
		for (uint8_t i = 0; i < POWER_RAIL_COUNT; i++) {
			if ((powerRails[i]->status == ON || powerRails[i]->status == NOTUSED)
					|| (powerRails[i]->status == STARTING && noFault)) {
				allReady++;
			}
		}
		if (allReady == POWER_RAIL_COUNT) {
			if(enableClock()){
				smartUSBPrint("Startup failed \r\n");
				return;
			}
			HAL_Delay(10);
			setReset(1);
			smartUSBPrint("Startup success \r\n");
			powerStatus = ON;
			return;
		}
	}
}

/**
 * @brief Adds source string to destination string. String is filled with spaces to the given length
 * @param dest is the destination string to which source string is added
 * @param source is the source string which is added to the dest string
 * @param len is the maximum length of the dest string
 */
void addString(char *dest, char *source, uint8_t len) {
	uint8_t length = strlen(source);
	strncat(dest, source, len);
	for (uint8_t i = 0; i < len - length; i++) {
		strcat(dest, " ");
	}
}

/**
 * @brief Adds float to destination string. String is filled with spaces to the given length
 * @param dest is the destination string to which source string is added
 * @param num is the float which is added to the dest string
 * @param len is the maximum length of the dest string
 */
void addFloat(char *dest, float num, uint8_t len) {
	char c[20];
	sprintf(c, "%.3f", num);
	addString(dest, c, len);
}

/**
 * @brief Adds int to destination string. String is filled with spaces to the given length
 * @param dest is the destination string to which source string is added
 * @param num is the uint16_t which is added to the dest string
 * @param len is the maximum length of the dest string
 */
void addInt(char *dest, uint32_t num, uint8_t len) {
	char c[20];
	sprintf(c, "%i", num);
	addString(dest, c, len);
}

/**
 * @brief Prints info about the power management system
 */
void printInfo() {

	char line[255];

	smartUSBPrint(
			"\r\n\r\n\r\n\r\n\r\n\r\n");
	smartUSBPrint("Commands: \r\n");

	memset(line, 0, sizeof(line));
	addString(line, "pwr", 20);
	addString(line, "Toggle all power rails and clock", 100);
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "nofault", 20);
	addString(line, "Toggle fault monitoring", 100);
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "enableX", 20);
	addString(line, "Turn on power rail. x=power rail ID", 100);
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "disableX", 20);
	addString(line, "Turn off power rail. x=power rail ID", 100);
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "clkX", 20);
	addString(line, "Enable/disable clock. x=0 or x=1", 100);
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "cspeedX", 20);
	addString(line, "Set clock speed. 10000 < x < 125000000", 100);
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "rstX", 20);
	addString(line, "Set reset HIGH or LOW. x=0 or x=1", 100);
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "settvYX", 20);
	addString(line, "Set target voltage. y=power rail ID, x = target voltage 0 <= x <= 3300 mV", 100);
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "setmcYX", 20);
	addString(line, "Set max current. y=power rail ID, x = max current 0 <= x <= 1000 mA", 100);
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "settolYX", 20);
	addString(line, "Set tolerance. y=power rail ID, x = tolerance 0 <= x <= 3300 mV", 100);
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "save", 20);
	addString(line, "Save config to flash", 100);
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "load", 20);
	addString(line, "Load config from flash (Automatically done at start-up)", 100);
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "default", 20);
	addString(line, "Load default config", 100);
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "restart", 20);
	addString(line, "Restart the whole board", 100);
	smartUSBPrint("%s\r\n", line);

	smartUSBPrint("\r\n\r\n\r\n");

	if (noFault) {
		USBprintf("Fault checking disabled\r\n");
	}
	memset(line, 0, sizeof(line));
	addString(line, "Clock: ", 7);
	if(clockStatus == 0){
		addString(line, "OFF", 13);
	}
	else{
		addString(line, "ON", 13);
	}
	addString(line, "Clock speed: ", 13);
	addInt(line, clockSpeed, 17);

	addString(line, "Requested clock speed: ", 23);
	addInt(line, requestedClockSpeed, 20);
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "RST: ", 5);
	if(resetStatus == 0){
		addString(line, "LOW", 13);
	}
	else{
		addString(line, "HIGH", 13);
	}
	smartUSBPrint("%s\r\n", line);


	memset(line, 0, sizeof(line));
	addString(line, "Name:", 20);
	for (uint8_t i = 0; i < POWER_RAIL_COUNT; i++) {
		addString(line, powerRails[i]->name, 10);
		addString(line, "ID:", 3);
		addInt(line, powerRails[i]->ID + 1, 7);
	}
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "Status:", 20);
	for (uint8_t i = 0; i < POWER_RAIL_COUNT; i++) {
		if (powerRails[i]->status == ON) {
			addString(line, "ON", 20);
		} else if (powerRails[i]->status == OFF) {
			addString(line, "OFF", 20);
		} else if (powerRails[i]->status == FAILURE) {
			addString(line, "FAILURE", 20);
		} else if (powerRails[i]->status == NOTUSED) {
			addString(line, "NOT USED", 20);
		} else if (powerRails[i]->status == STARTING) {
			addString(line, "STARTING", 20);
		}
	}
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "Sequence (ms):", 20);
	for (uint8_t i = 0; i < POWER_RAIL_COUNT; i++) {
		addInt(line, powerRails[i]->startUpDelay, 20);
	}
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "Target voltage (V):", 20);
	for (uint8_t i = 0; i < POWER_RAIL_COUNT; i++) {
		addFloat(line, powerRails[i]->targetVoltage, 20);
	}
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "Tolerance (V):", 20);
	for (uint8_t i = 0; i < POWER_RAIL_COUNT; i++) {
		addFloat(line, powerRails[i]->tolerance, 20);
	}
	smartUSBPrint("%s\r\n", line);

	memset(line, 0, sizeof(line));
	addString(line, "Max current (A):", 20);
	for (uint8_t i = 0; i < POWER_RAIL_COUNT; i++) {
		addFloat(line, powerRails[i]->maxCurrent, 20);
	}
	smartUSBPrint("%s\r\n", line);

}

/**
 * @brief Prints voltages and currents of the power rails
 */
void printVoltages() {
	static uint32_t lastPrintTime = 0;

	if (printInfoFlag) {
		printInfoFlag = 0;
		printInfo();
	}

	if (HAL_GetTick() - lastPrintTime > 333) {
		lastPrintTime = HAL_GetTick();
		char line[255] = { '\0' };

		addString(line, "Voltage (V):", 20);
		for (uint8_t i = 0; i < POWER_RAIL_COUNT; i++) {
			addFloat(line, powerRails[i]->volatge, 5);
			addString(line, "V/", 2);
			addFloat(line, powerRails[i]->current, 5);
			addString(line, "A", 8);

		}
		addString(line, "Command:", 8);
		addString(line, USBReceiveBuffer, 20);
		smartUSBPrint("\r%s", line);

	}
}

/**
 * @brief Blinks the green LED.
 * The LED is blinks slowly when power rails are disabled
 * The LED is fully on when power rails are turned on and fault monitoring is enabled
 * The LED blinks rapidly when power rails are enabled and fault monitoring is disabled
 */
void greenLEDLogic() {

	if (powerStatus == ON) {

		if (!noFault) {
			HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
		} else if (HAL_GetTick() - greenLEDLastBlinkTime
				> greenLEDNextBlinkTime) {
			greenLEDNextBlinkTime = 100;
			greenLEDLastBlinkTime = HAL_GetTick();
			HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
		}
	} else {

		if (!powerRailFailed) {
			if (HAL_GetTick() - greenLEDLastBlinkTime > greenLEDNextBlinkTime) {
				greenLEDNextBlinkTime = 1000;
				greenLEDLastBlinkTime = HAL_GetTick();
				HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
			}
		} else {
			HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin,
					GPIO_PIN_RESET);
		}
	}
}

void redLEDLogic(){
	//Blink the red LED if any of the power rails has failed
	if (powerRailFailed) {
		if (HAL_GetTick() - redLedTimer > redLedNextBlinkTimer) {
			redLedTimer = HAL_GetTick();

			if (redLedBlinkState % 2 == 0) {
				HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin,
						GPIO_PIN_SET);
			} else {
				HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin,
						GPIO_PIN_RESET);
			}
			redLedBlinkState++;

			if (redLedBlinkState == (failedRail->ID + 1) * 2) {
				redLedNextBlinkTimer = 1000;
				redLedBlinkState = 0;
			} else {
				redLedNextBlinkTimer = 200;
			}

		}

	}
}

/**
 * @brief parses the received USB data for commands
 */
void USB_receive() {

	if (strstr(USBReceiveBuffer, "nofault") != NULL) {
		noFault = !noFault;
	}
	else if (strstr(USBReceiveBuffer, "pwr") != NULL) {
		startUpPressed = 1;
	} else if (strstr(USBReceiveBuffer, "enable") != NULL) {
		uint8_t ID = USBReceiveBuffer[6] - '0' - 1;
		if (ID < POWER_RAIL_COUNT) {
			setEnable(powerRails[ID], 1);
			powerRails[ID]->status = STARTING;
		}
	} else if (strstr(USBReceiveBuffer, "disable") != NULL) {
		uint8_t ID = USBReceiveBuffer[7] - '0' - 1;
		if (ID < POWER_RAIL_COUNT) {
			setEnable(powerRails[ID], 0);
			powerRails[ID]->status = OFF;
		}
	} else if (strstr(USBReceiveBuffer, "clk") != NULL) {
		uint8_t status = USBReceiveBuffer[3] - '0';

		if(status == 1){
			startClock = 1;
		}
		else{
			stopClock = 1;
		}
	}else if (strstr(USBReceiveBuffer, "cspeed") != NULL) {
		uint32_t temp = atoi(USBReceiveBuffer+6);
		if(temp >= 10000 && temp <= 125000000){
			requestedClockSpeed = temp;
		}
	}
	else if (strstr(USBReceiveBuffer, "rst") != NULL) {
		uint8_t status = USBReceiveBuffer[3] - '0';
		if(status == 1){
			setReset(1);
		}
		else{
			setReset(0);
		}
	}
	else if (strstr(USBReceiveBuffer, "save") != NULL) {
		writePowerRailDataToFlash(powerRails, POWER_RAIL_COUNT);
	}

	else if (strstr(USBReceiveBuffer, "load") != NULL) {
		readPowerRailDataFromFlash(powerRails, POWER_RAIL_COUNT);
	}

	else if (strstr(USBReceiveBuffer, "default") != NULL) {
		restoreDefaultValues(powerRails, POWER_RAIL_COUNT);
		requestedClockSpeed = DEFAULT_CLOCK_SPEED;
	}
	else if (strstr(USBReceiveBuffer, "settv") != NULL) {
		uint8_t ID = USBReceiveBuffer[5] - '0' - 1;
		if (ID < POWER_RAIL_COUNT) {
			uint32_t temp = atoi(USBReceiveBuffer+6);
			if(temp >= 0 && temp <= 3300){
				setTargetVoltage(ID, ((float)temp)/1000);
			}
		}
	}
	else if (strstr(USBReceiveBuffer, "setmc") != NULL) {
		uint8_t ID = USBReceiveBuffer[5] - '0' - 1;
		if (ID < POWER_RAIL_COUNT) {
			uint32_t temp = atoi(USBReceiveBuffer+6);
			if(temp >= 0 && temp <= 1000){
				setMaxCurrent(ID, ((float)temp)/1000);
			}
		}
	}
	else if (strstr(USBReceiveBuffer, "settol") != NULL) {
		uint8_t ID = USBReceiveBuffer[6] - '0' - 1;
		if (ID < POWER_RAIL_COUNT) {
			uint32_t temp = atoi(USBReceiveBuffer+7);
			if(temp >= 0 && temp <= 3300){
				setTolerance(ID, ((float)temp)/1000);
			}
		}
	}
	else if (strstr(USBReceiveBuffer, "restart") != NULL) {
		shutDown(0);
		while(1){
			//Force watchdog reset
		}
	}
	memset(USBReceiveBuffer, '\0', 64);

	printInfoFlag = 1;
}

/**
 * @brief ADC callback function which is called after all ADC samples are done
 * @param hadc pointer to adc which caused the function call
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {

	//HAL_GPIO_TogglePin(LED_RED_GPIO_Port, LED_RED_Pin);

	//Calculate power rail voltages
	if (hadc->Instance == ADC1) {

		uint32_t averages[POWER_RAIL_COUNT];
		memset(averages, 0, POWER_RAIL_COUNT * sizeof(uint32_t));
		float averageFloats[POWER_RAIL_COUNT];
		float adcConst = 3.30 / 4096;

		for (uint16_t j = 0; j < SAMPLE_COUNT; j++) {
			for (uint16_t i = 0; i < POWER_RAIL_COUNT; i++) {
				averages[i] += adcBufferVoltage[j * POWER_RAIL_COUNT + i];
			}
		}

		for (uint16_t i = 0; i < POWER_RAIL_COUNT; i++) {
			averages[i] = (averages[i] / SAMPLE_COUNT);
			averageFloats[i] = adcConst * (float) (averages[i]);
		}

		for (uint16_t i = 0; i < POWER_RAIL_COUNT; i++) {
			float val = averageFloats[powerRails[i]->voltageADCIndex];
			if (val < 3.3) {
				powerRails[i]->volatge = val;
			}
		}

		//USBprintf("%i %i %i %i %i %i\r\n",adcBufferVoltage[0],adcBufferVoltage[1],adcBufferVoltage[2],adcBufferVoltage[3],adcBufferVoltage[4],adcBufferVoltage[5] );
		memset((void*) adcBufferVoltage, 0,
				SAMPLE_COUNT * POWER_RAIL_COUNT * sizeof(uint16_t));
		//MX_ADC1_Init();
		//HAL_ADC_Start_DMA(&hadc1, (uint32_t*) adcBufferVoltage, POWER_RAIL_COUNT*SAMPLE_COUNT);
		HAL_ADC_Start_DMA(&hadc1, (uint32_t*) adcBufferVoltage,
				POWER_RAIL_COUNT * SAMPLE_COUNT);

	}
	//Calculate power rail currents
	if (hadc->Instance == ADC2) {

		uint32_t averages[POWER_RAIL_COUNT];
		memset(averages, 0, CURRENT_CHANNEL_COUNT * sizeof(uint32_t));
		float averageFloats[CURRENT_CHANNEL_COUNT];
		float adcConst = 3.30 / 4095;

		for (uint16_t i = 0; i < SAMPLE_COUNT; i++) {
			for (uint16_t j = 0; j < CURRENT_CHANNEL_COUNT; j++) {
				averages[j] +=
						adcBufferCurrent[i * (CURRENT_CHANNEL_COUNT) + j];
			}
		}

		for (uint8_t i = 0; i < CURRENT_CHANNEL_COUNT; i++) {
			averages[i] = (averages[i] / SAMPLE_COUNT);
			averageFloats[i] = adcConst * (float) (averages[i]);

		}
		for (uint16_t i = 0; i < CURRENT_CHANNEL_COUNT; i++) {
			powerRails[i]->current = (averageFloats[i] / CURRENT_AMPLIFICATION)
					/ powerRails[i]->shuntValue;
		}

		memset((void*) adcBufferCurrent, 0,
				SAMPLE_COUNT * (CURRENT_CHANNEL_COUNT) * sizeof(uint16_t));
		//MX_ADC2_Init();
		HAL_ADC_Start_DMA(&hadc2, (uint32_t*) adcBufferCurrent,
				SAMPLE_COUNT * (CURRENT_CHANNEL_COUNT));

	}

	//Check if voltages and current are within allowed ranges

	for (uint8_t i = 0; i < POWER_RAIL_COUNT; i++) {

		float voltageDifference = fabs(
				powerRails[i]->targetVoltage - powerRails[i]->volatge);
		if ((voltageDifference > powerRails[i]->tolerance)
				|| (powerRails[i]->current > powerRails[i]->maxCurrent)) {
			if (powerRails[i]->status == ON) {
				//USBprintf("\r\nFAILURE %f %f %f %f\r\n",powerRails[i]->volatge,powerRails[i]->targetVoltage,voltageDifference,powerRails[i]->current);
				powerRails[i]->status = FAILURE;
				if (!noFault) {
					shutDown(i);
				}
			}
		} else if (powerRails[i]->status == STARTING) {
			powerRails[i]->status = ON;
		}
	}

}


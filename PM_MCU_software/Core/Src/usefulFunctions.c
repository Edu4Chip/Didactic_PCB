/*
 * usefulFunctions.cpp
 *
 *  Created on: Sep 5, 2024
 *      Author: roope
 */

#include "usefulFunctions.h"
#include "stm32g4xx_hal.h"
#include "usbd_cdc_if.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h> //for va_list var arg functions

#define BUFFERSIZE 64


char smartUSBPrintArray[BUFFERSIZE][256];
uint8_t smartUSBPrintArrayReadIndex = 0;
uint8_t smartUSBPrintArrayWriteIndex = 0;
uint8_t smartUSBPrintArrayBufferLength = 0;


void USBprintf(const char *fmt, ...) {

	static char buffer[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	int len = strlen(buffer);
	//HAL_UART_Transmit(&huart2, (uint8_t*) buffer, len, -1);
	CDC_Transmit_FS((uint8_t*) buffer, len);
}

void smartUSBPrintNext() {
	if (smartUSBPrintArrayBufferLength > 0) {

		if (CDC_Transmit_FS(
				(uint8_t*) smartUSBPrintArray[smartUSBPrintArrayReadIndex],
				strlen(smartUSBPrintArray[smartUSBPrintArrayReadIndex]))
				== USBD_OK) {

			smartUSBPrintArrayBufferLength--;
			smartUSBPrintArrayReadIndex++;
			if (smartUSBPrintArrayReadIndex == BUFFERSIZE) {
				smartUSBPrintArrayReadIndex = 0;
			}
		}
	}
}

void smartUSBPrint(const char *fmt, ...) {

	static char buffer[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	int len = strlen(buffer);

	if (smartUSBPrintArrayBufferLength < BUFFERSIZE) {
		memset(smartUSBPrintArray[smartUSBPrintArrayWriteIndex], '\0', 256);
		memcpy(smartUSBPrintArray[smartUSBPrintArrayWriteIndex], buffer, len);
		smartUSBPrintArrayWriteIndex++;
		smartUSBPrintArrayBufferLength++;
		if (smartUSBPrintArrayWriteIndex == BUFFERSIZE) {
			smartUSBPrintArrayWriteIndex = 0;
		}
		smartUSBPrintNext();
	}
}


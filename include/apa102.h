/*
 *	apa102.h
 *
 *	Created on: Nov 15, 2017
 *		Author: Sterna
 */
#ifndef APA102_H_
#define APA102_H_

#include "stm32f10x.h"
#include <stdbool.h>
#include <string.h>
#include "utils.h"
#include "APA102Conf.h"

typedef struct
{
	uint8_t global;
	uint8_t b;
	uint8_t g;
	uint8_t r;
}apa102Pixel_t;

typedef struct
{
	uint16_t start;
	uint16_t stop;
	uint8_t r;
	uint8_t g;
	uint8_t b;
}apa102ScaleSegment_t;

//Add the start bits to the global setting
#define APA_ADD_GLOBAL_BITS(x) (x | 0b11100000)
#define APA_REMOVE_GLOBAL_BITS(x) (x & 0b00011111)
#define APA_MAX_GLOBAL_SETTING 31

//Calculate the number of data to be transmitted base on the number of pixels
#define APA_DATA_SIZE(x)	(4*(x+2))

#define APA_MOSI_SET()		(APA_MOSI_PORT->ODR |= 1<<APA_MOSI_PIN)
#define APA_MOSI_CLEAR()	(APA_MOSI_PORT->ODR &= ~(1<<APA_MOSI_PIN))
#define APA_SCK_SET()		(APA_SCK_PORT->ODR |= 1<<APA_SCK_PIN)
#define APA_SCK_CLEAR()		(APA_SCK_PORT->ODR &= ~(1<<APA_SCK_PIN))


//Functions
void apa102Init(uint8_t strip, uint16_t nofLeds);
void apa102SetDefaultGlobal(uint8_t global);
uint8_t apa102GetDefaultGlobal();
void apa102SetPixel(uint8_t strip, uint16_t pixel, uint8_t r, uint8_t g, uint8_t b, bool force);
void apa102SetPixelWithGlobal(uint8_t strip, uint16_t pixel, uint8_t r, uint8_t g, uint8_t b, uint8_t global, bool force);
bool apa102GetPixel(uint8_t strip, uint16_t pixel, apa102Pixel_t* out);
bool apa102UpdateStrip(uint8_t strip);
bool apa102DMABusy(uint8_t strip);
void apa102FillRange(uint8_t strip, uint16_t start, uint16_t stop, uint8_t r, uint8_t g, uint8_t b, uint8_t global);
void apa102FillStrip(uint8_t strip, uint8_t r, uint8_t g, uint8_t b, uint8_t global);
void apa102ClearStrip(uint8_t strip);
void apa102UpdateStripBitbang(uint8_t strip);
bool apa102IsValidPixel(uint8_t strip, uint16_t pixel);

#endif /* APA102_H_ */

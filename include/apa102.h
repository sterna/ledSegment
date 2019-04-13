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

//The maximum numbers of LEDs per strip
#define APA_MAX_NOF_LEDS 200
//The number of strips (all these are handled in a hardcoded way)
#define APA_NOF_STRIPS 3
//Keyword to use when trying to adress all strips (might not work for all functions)
#define APA_ALL_STRIPS 255

typedef struct
{
	uint8_t global;
	uint8_t b;
	uint8_t g;
	uint8_t r;
}apa102Pixel_t;

//The first (main) strip uses SPI1
#define APA_SPI				SPI1
#define APA_SPI_DR			(SPI1_BASE+0x0C)
#define APA_DMA_CH 			DMA1_Channel3
#define APA_DMA_TC_FLAG		DMA1_FLAG_TC3
#define APA_DMA_TE_FLAG		DMA1_FLAG_TE3
#define APA_DMA_IRQ			DMA1_Channel3_IRQHandler
#define APA_DMA_IRQn		DMA1_Channel3_IRQn

//Todo: This needs to be updated when I remap SPI1 for the real board
#define APA_MOSI_PIN 	7
#define APA_MOSI_PORT	GPIOA
#define APA_SCK_PIN 	5
#define APA_SCK_PORT	GPIOA

//The second strip uses SPI2
#define APA2_SPI			SPI2
#define APA2_SPI_DR			(SPI2_BASE+0x0C)
#define APA2_DMA_CH 		DMA1_Channel5
#define APA2_DMA_TC_FLAG	DMA1_FLAG_TC5
#define APA2_DMA_TE_FLAG	DMA1_FLAG_TE5
#define APA2_DMA_IRQ		DMA1_Channel5_IRQHandler
#define APA2_DMA_IRQn		DMA1_Channel5_IRQn

#define APA2_MOSI_PIN 	15
#define APA2_MOSI_PORT	GPIOB
#define APA2_SCK_PIN 	13
#define APA2_SCK_PORT	GPIOB

//The third strip uses USART2 in synch mode
#define APA3_SPI			USART2
#define APA3_SPI_DR			(USART2_BASE+0x04)
#define APA3_DMA_CH 		DMA1_Channel7
#define APA3_DMA_TC_FLAG	DMA1_FLAG_TC7
#define APA3_DMA_TE_FLAG	DMA1_FLAG_TE7
#define APA3_DMA_IRQ		DMA1_Channel7_IRQHandler
#define APA3_DMA_IRQn		DMA1_Channel7_IRQn

#define APA3_MOSI_PIN 	2
#define APA3_MOSI_PORT	GPIOA
#define APA3_SCK_PIN 	4
#define APA3_SCK_PORT	GPIOA


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

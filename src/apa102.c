/*
 *	apa102.c
 *
 *	Created on: Nov 15, 2017
 *		Author: Sterna
 *
 *	Low-level driver for apa102.
 *	Contains a model for a the LEDs, as a single strip
 *	SPI1 pins are:
 *	MOSI: PA7
 *	SCK: PA5
 */

#include "apa102.h"

//Add some dummy comment here (to be removed...)

/*
 * A union to convert the apa102Pixel struct to uint32_t
 */
union flatPixel_u
{
	uint32_t i;
	apa102Pixel_t pix;
};

//Internal variables

//Contains information about all the pixels
//First pixel is the start frame (all 0) and the last frame is the stop frame (all 1)
static apa102Pixel_t pixels[APA_NOF_STRIPS][APA_MAX_NOF_LEDS+2];
//The number of pixels currently used
static uint16_t currentNofPixels[APA_NOF_STRIPS];
//Indicates if we actually need to update
static bool newData[APA_NOF_STRIPS];
//The default global setting shared by all LEDs (if not given). This value can be 0-0b00011111 (31)
static uint8_t defaultGlobal=APA_ADD_GLOBAL_BITS(APA_MAX_GLOBAL_SETTING);
//Indicates if the DMA is currently transmitting
static volatile bool DMABusy[APA_NOF_STRIPS];

/* ---- Internal functions ---- */
//Returns true if the pixel is a valid pixel (if it is active in a strip)
static bool isValidStrip(uint8_t strip);
//Return true if the pixel already has this colour
static bool pixelNeedsUpdate(uint8_t strip, uint16_t pixel,uint8_t r, uint8_t g, uint8_t b);


/*
 * Inits an apa102 strip
 * strip specifies which strip shall be initialized, counted from 1. If 0 or larger than APA_NOF_STRIPS, it will be ignored.
 * nofLeds is the number of LEDs in the strip. If larger than APA_MAX_NOF_LEDS, it will be capped
 * speed is speed in Hz
 */
void apa102Init(uint8_t strip, uint16_t nofLeds)
{
	if(strip>APA_NOF_STRIPS || strip==0)
	{
		return;
	}
	strip--;
	if(nofLeds>APA_MAX_NOF_LEDS)
	{
		nofLeds=APA_MAX_NOF_LEDS;
	}
	currentNofPixels[strip]=nofLeds;
	newData[strip]=false;
	DMABusy[strip]=false;
	//Clear pixel list
	memset(pixels[strip],0,APA_MAX_NOF_LEDS*sizeof(apa102Pixel_t));
	//Populate start frame
	memset(&pixels[strip][0],0,sizeof(apa102Pixel_t));
	//Populate end frame (only 1 s)
	memset(&pixels[strip][nofLeds+1],0xFF,sizeof(apa102Pixel_t));
	//Set the default global setting to all LEDs
	for(uint16_t i=1;i<=nofLeds;i++)
	{
		pixels[strip][i].global=defaultGlobal;	//Use max power for global setting for now
	}

	//Load specific hardware per strip
	GPIO_TypeDef* tmpGPIOPort=0;
	uint16_t tmpGPIOPins=0;
	DMA_Channel_TypeDef* tmpDMA_CH=0;
	uint32_t tmpDMAPeriphBaseAdr=0;
	uint8_t tmpDMAIRQn=0;
	SPI_TypeDef* tmpSPI=0;
	USART_TypeDef* tmpUSART=0;
	uint32_t tmpRemap=0;
	uint32_t tmpSpeedSetting=0;

	if(strip==0)
	{
		tmpGPIOPort = APA_MOSI_PORT;
		tmpGPIOPins = _BV(APA_MOSI_PIN) | _BV(APA_SCK_PIN);
		tmpDMA_CH=APA_DMA_CH;
		tmpDMAPeriphBaseAdr=(uint32_t)(APA_SPI_DR);
		tmpDMAIRQn=APA_DMA_IRQn;
		tmpSPI=APA_SPI;
		tmpRemap=APA_REMAP_CONFIG;
		tmpSpeedSetting= APA_SPEED_SETTING;

	}
	else if(strip==1)
	{
		tmpGPIOPort = APA2_MOSI_PORT;
		tmpGPIOPins = _BV(APA2_MOSI_PIN) | _BV(APA2_SCK_PIN);
		tmpDMA_CH=APA2_DMA_CH;
		tmpDMAPeriphBaseAdr=(uint32_t)(APA2_SPI_DR);
		tmpDMAIRQn=APA2_DMA_IRQn;
		tmpSPI=APA2_SPI;
		tmpRemap=APA2_REMAP_CONFIG;
		tmpSpeedSetting= APA2_SPEED_SETTING;
	}
	else if(strip==2)
	{
		//This strip runs on USART (in spite of the naming it SPI)
		tmpGPIOPort = APA3_MOSI_PORT;
		tmpGPIOPins = _BV(APA3_MOSI_PIN) | _BV(APA3_SCK_PIN);
		tmpDMA_CH=APA3_DMA_CH;
		tmpDMAPeriphBaseAdr=(uint32_t)(APA3_SPI_DR);
		tmpDMAIRQn=APA3_DMA_IRQn;
		tmpUSART=APA3_SPI;
		tmpRemap=APA3_REMAP_CONFIG;
		tmpSpeedSetting= APA3_SPEED_SETTING;
	}
	else
	{
		//Something went wrong. Fail silently.
		return;
	}
	//Init clocks (we will always use AFIO)
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO,ENABLE);

	utilSetClockGPIO(tmpGPIOPort,ENABLE);
	utilSetClockDMA(tmpDMA_CH,ENABLE);
	if(tmpSPI)
	{
		utilSetClockSPI(tmpSPI,ENABLE);
	}
	if(tmpUSART)
	{
		utilSetClockUSART(tmpUSART,ENABLE);
	}
	if(tmpRemap)
	{
		GPIO_PinRemapConfig(tmpRemap,ENABLE);
		GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable,ENABLE);
	}
	//Init pins
	GPIO_InitTypeDef GPIOInitStruct;
	GPIOInitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIOInitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIOInitStruct.GPIO_Pin = tmpGPIOPins;
	GPIO_Init(tmpGPIOPort,&GPIOInitStruct);

	//Init DMA
	DMA_InitTypeDef DMAInitStruct;
	DMA_DeInit(tmpDMA_CH);
	DMAInitStruct.DMA_PeripheralBaseAddr = tmpDMAPeriphBaseAdr;
	DMAInitStruct.DMA_MemoryBaseAddr = (uint32_t)(pixels[strip]);
	DMAInitStruct.DMA_DIR = DMA_DIR_PeripheralDST;
	DMAInitStruct.DMA_BufferSize = APA_DATA_SIZE(nofLeds);
	DMAInitStruct.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	DMAInitStruct.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMAInitStruct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMAInitStruct.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	DMAInitStruct.DMA_Mode = DMA_Mode_Normal;
	DMAInitStruct.DMA_Priority = DMA_Priority_VeryHigh;
	DMAInitStruct.DMA_M2M = DMA_M2M_Disable;
	DMA_Init(tmpDMA_CH, &DMAInitStruct);

	//Init DMA interrupt
	NVIC_InitTypeDef nvicInitStructure;
	nvicInitStructure.NVIC_IRQChannel = tmpDMAIRQn;
	nvicInitStructure.NVIC_IRQChannelPreemptionPriority = 2;
	nvicInitStructure.NVIC_IRQChannelSubPriority = 1;
	nvicInitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&nvicInitStructure);

	//Enable interrupt on transfer complete and transfer error
	DMA_ITConfig(tmpDMA_CH,DMA_IT_TC | DMA_IT_TE,ENABLE);

	//Init SPI (this is only valid for strip 1 and 2 (index 0 and 1))
	if(strip ==0 || strip == 1)
	{
		SPI_InitTypeDef SPIInitStruct;
		SPIInitStruct.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
		SPIInitStruct.SPI_Mode = SPI_Mode_Master;
		SPIInitStruct.SPI_DataSize = SPI_DataSize_8b;
		SPIInitStruct.SPI_CPOL = SPI_CPOL_High;
		SPIInitStruct.SPI_CPHA = SPI_CPHA_2Edge;
		SPIInitStruct.SPI_NSS = SPI_NSS_Soft;
		SPIInitStruct.SPI_BaudRatePrescaler = (uint16_t)tmpSpeedSetting;
		SPIInitStruct.SPI_FirstBit = SPI_FirstBit_MSB;
		SPIInitStruct.SPI_CRCPolynomial = SPI_CRCPR_CRCPOLY;
		SPI_Init(tmpSPI, &SPIInitStruct);

		SPI_SSOutputCmd(tmpSPI,ENABLE);

		SPI_I2S_DMACmd(tmpSPI,SPI_I2S_DMAReq_Tx,ENABLE);

		SPI_Cmd(tmpSPI,ENABLE);
	}
	else if(strip==2) //This strip uses USART
	{
		//Todo: Strip=2 doesn't work
		USART_InitTypeDef USARTInitStruct;
		USARTInitStruct.USART_BaudRate = tmpSpeedSetting;
		USARTInitStruct.USART_WordLength = USART_WordLength_8b;
		USARTInitStruct.USART_StopBits = USART_StopBits_0_5;
		USARTInitStruct.USART_Parity = USART_Parity_No;
		USARTInitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
		USARTInitStruct.USART_Mode = USART_Mode_Tx;
		USART_Init(tmpUSART, &USARTInitStruct);

		USART_ClockInitTypeDef USARTClockInitStruct;
		USARTClockInitStruct.USART_Clock = USART_Clock_Enable;
		USARTClockInitStruct.USART_LastBit = USART_LastBit_Enable;
		USARTClockInitStruct.USART_CPHA = USART_CPHA_1Edge;
		USARTClockInitStruct.USART_CPOL = USART_CPOL_Low;
		USART_ClockInit(tmpUSART,&USARTClockInitStruct);

		USART_DMACmd(tmpUSART,USART_DMAReq_Tx,ENABLE);

		USART_Cmd(tmpUSART, ENABLE);
	}
}

/*
 * Sets a pixel to a certain colour
 * Pixels are indexed from 1
 * If force is true, the checks for validPixel and right colour is skipped
 */
void apa102SetPixel(uint8_t strip, uint16_t pixel, uint8_t r, uint8_t g, uint8_t b, bool force)
{
	if(!force && pixelNeedsUpdate(strip,pixel,r,g,b))
	{
		return;
	}
	strip--;
	pixels[strip][pixel].r=r;
	pixels[strip][pixel].g=g;
	pixels[strip][pixel].b=b;
	pixels[strip][pixel].global=defaultGlobal;
	newData[strip]=true;
}

/*
 * Sets the default global value
 * Can be 0-31 (if set to 0, LEDs will be off?)
 */
void apa102SetDefaultGlobal(uint8_t global)
{
	defaultGlobal=APA_ADD_GLOBAL_BITS(global);
}

/*
 * Returns the current default global (without the extra bits)
 */
uint8_t apa102GetDefaultGlobal()
{
	return APA_REMOVE_GLOBAL_BITS(defaultGlobal);
}
/*
 * Set the pixel to a colour, and include the global setting
 * Global can be 0-31
 * If force is true, the checks for validPixel and right colour is skipped
 */
void apa102SetPixelWithGlobal(uint8_t strip, uint16_t pixel, uint8_t r, uint8_t g, uint8_t b, uint8_t global, bool force)
{
	if(!force && pixelNeedsUpdate(strip,pixel,r,g,b))
	{
		return;
	}
	strip--;
	pixels[strip][pixel].r=r;
	pixels[strip][pixel].g=g;
	pixels[strip][pixel].b=b;
	pixels[strip][pixel].global=APA_ADD_GLOBAL_BITS(global);
	newData[strip]=true;
}

/*
 * Returns information about the given pixel
 * Will perform a deep copy
 * Returns false if pixel does not exist
 */
bool apa102GetPixel(uint8_t strip, uint16_t pixel, apa102Pixel_t* out)
{
	if(apa102IsValidPixel(strip,pixel))
	{
		return false;
	}
	memcpy(out,&pixels[strip][pixel],sizeof(apa102Pixel_t));
	return true;
}

/*
 * Pushes the current LED setting to the strip (restarts the DMA)
 * Only updates if there is new data
 */
bool apa102UpdateStrip(uint8_t strip)
{
	if(strip==APA_ALL_STRIPS)
	{
		for(uint8_t i=1;i<=APA_NOF_STRIPS;i++)
		{
			apa102UpdateStrip(i);
		}
	}
	if(!isValidStrip(strip))
	{
		return false;
	}
	strip--;
	if(!newData[strip] || apa102DMABusy(strip+1))
	{
		return false;
	}
	DMA_Channel_TypeDef* tmpDMACH;
	if(strip==0)
	{
		tmpDMACH=APA_DMA_CH;
	}
	else if(strip==1)
	{
		tmpDMACH=APA2_DMA_CH;
	}
	else if(strip==2)
	{
		tmpDMACH=APA3_DMA_CH;
	}
	else
	{
		return false;
	}
	DMA_Cmd(tmpDMACH,DISABLE);
	DMA_SetCurrDataCounter(tmpDMACH,APA_DATA_SIZE(currentNofPixels[strip]));
	DMA_Cmd(tmpDMACH,ENABLE);
	DMABusy[strip]=true;
	newData[strip]=false;
	return true;
}

/*
 * Bitbangs the SPI protocol, instead of using DMA and SPI unit
 * Used for debugging
 * Note: This will re-init the pins and turn off spi. Run apa102init if you want to use DMA after using this.
 * Note2: This will not work for all strips, only the first one
 */
void apa102UpdateStripBitbang(uint8_t strip)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	GPIO_InitTypeDef GPIOInitStruct;
	GPIOInitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIOInitStruct.GPIO_Pin =  _BV(APA_MOSI_PIN) | _BV(APA_SCK_PIN);
	GPIOInitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(APA_MOSI_PORT,&GPIOInitStruct);

	SPI_Cmd(APA_SPI,DISABLE);
	strip--;
	for(uint16_t i = 0;i<=currentNofPixels[strip];i++)
	{
		union flatPixel_u fp;
		fp.pix = pixels[strip][i];
		for(uint8_t bit=0;bit<32;bit++)
		{
			//Clock low
			APA_SCK_CLEAR();
			__NOP();
			__NOP();
			__NOP();
			//Setup pin
			if(fp.i>>(31-i))
			{
				APA_MOSI_SET();
			}
			else
			{
				APA_MOSI_CLEAR();
			}
			//Clock high
			__NOP();
			__NOP();
			__NOP();
			APA_SCK_SET();
			__NOP();
			__NOP();
			__NOP();
		}
	}
}

/*
 * Indicates if a transfer is currently in progress
 * If strip == APA_ALL_STRIPS, it's true if any of the strips is busy
 */
bool apa102DMABusy(uint8_t strip)
{
	if(strip==APA_ALL_STRIPS)
	{
		for(uint8_t i=0;i<APA_NOF_STRIPS;i++)
		{
			if(DMABusy[i])
			{
				return true;
			}
		}
		return false;
	}
	if(isValidStrip(strip))
	{
		return DMABusy[strip-1];
	}
	else
	{
		return false;
	}
}

/*
 * Sets all pixels in a range to the same colour
 * If start=stop, only one pixel is set
 * If global is larger than MAX_GLOBAL, default global will be used (this also speeds the setting of LEDs up slightly)
 */
void apa102FillRange(uint8_t strip, uint16_t start, uint16_t stop, uint8_t r, uint8_t g, uint8_t b, uint8_t global)
{
	if(!apa102IsValidPixel(strip,start) || !apa102IsValidPixel(strip,stop))
	{
		return;
	}
	do
	{
		if(global>APA_MAX_GLOBAL_SETTING)
		{
			apa102SetPixel(strip,start,r,g,b,true);
		}
		else
		{
			apa102SetPixelWithGlobal(strip,start,r,g,b,global,true);
		}
		start++;
	}while(start<=stop);
}

/*
 * Fills the whole strip with the same colour
 */
void apa102FillStrip(uint8_t strip, uint8_t r, uint8_t g, uint8_t b, uint8_t global)
{
	apa102FillRange(strip,1,currentNofPixels[strip-1],r,g,b,global);
}

/*
 * Sets all LEDs to 0
 */
void apa102ClearStrip(uint8_t strip)
{
	apa102FillStrip(strip, 0,0,0,0);
}

/*
 * Returns if a pixel is valid or not
 */
bool apa102IsValidPixel(uint8_t strip, uint16_t pixel)
{
	if(!isValidStrip(strip))
	{
		return false;
	}
	if(pixel>currentNofPixels[strip-1] || pixel==0)
	{
		return false;
	}
	return true;
}

/*
 * Checks if the strip is valid
 */
static bool isValidStrip(uint8_t strip)
{
	if(strip==APA_ALL_STRIPS)
	{
		return true;
	}
	else if(strip==0 || strip>APA_NOF_STRIPS)
	{
		return false;
	}
	return true;
}

/*
 * Checks if the pixel needs an update (will also check if it's a valid pixel)
 */
static bool pixelNeedsUpdate(uint8_t strip, uint16_t pixel,uint8_t r, uint8_t g, uint8_t b)
{
	if(apa102IsValidPixel(strip,pixel))
	{
		strip--;
		if(pixels[strip][pixel].r==r && pixels[strip][pixel].g==g && pixels[strip][pixel].b==b)
		{
			return false;
		}
	}
	return true;
}

/*
 * APA102 strip 1 DMA interrupt
 */
void APA_DMA_IRQ()
{
	if(DMA_GetFlagStatus(APA_DMA_TE_FLAG))
	{
		DMA_ClearFlag(APA_DMA_TE_FLAG);
	}
	else if(DMA_GetFlagStatus(APA_DMA_TC_FLAG))
	{
		DMA_ClearFlag(APA_DMA_TC_FLAG);
		DMABusy[0]=false;
	}
	DMA_Cmd(APA_DMA_CH,DISABLE);
}

/*
 * APA102 strip 2 DMA interrupt
 */
void APA2_DMA_IRQ()
{
	if(DMA_GetFlagStatus(APA2_DMA_TE_FLAG))
	{
		DMA_ClearFlag(APA2_DMA_TE_FLAG);
	}
	else if(DMA_GetFlagStatus(APA2_DMA_TC_FLAG))
	{
		DMA_ClearFlag(APA2_DMA_TC_FLAG);
		DMABusy[1]=false;
	}
	DMA_Cmd(APA2_DMA_CH,DISABLE);
}

/*
 * APA102 strip 3 DMA interrupt
 */
void APA3_DMA_IRQ()
{
	if(DMA_GetFlagStatus(APA3_DMA_TE_FLAG))
	{
		DMA_ClearFlag(APA3_DMA_TE_FLAG);
	}
	else if(DMA_GetFlagStatus(APA3_DMA_TC_FLAG))
	{
		DMA_ClearFlag(APA3_DMA_TC_FLAG);
		DMABusy[2]=false;
	}
	DMA_Cmd(APA3_DMA_CH,DISABLE);
}

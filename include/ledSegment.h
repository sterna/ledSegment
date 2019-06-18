/*
 *	ledSegment.h
 *
 *	Created on: Dec 29, 2017
 *		Author: Sterna
 */

#ifndef LEDSEGMENT_H_
#define LEDSEGMENT_H_

#include "apa102.h"
#include "utils.h"
#include "time.h"

//The maximum number of LED segments allowed (each segment costs almost 100 byte of RAM)
//Without some library rewriting, this value cannot be larger than 254
#define LEDSEG_MAX_SEGMENTS	30
//The time between each full strip update (in ms)
#define LEDSEG_UPDATE_PERIOD_TIME 25
//The number of calculation sub-cycles per update period
#define LEDSEG_CALCULATION_CYCLES 4

/*
 * The modes the ledSegment controller can use
 * Todo: Add mode end (specifically for pulse, when it runs to the end and stops there (as if running off screen)
 */
typedef enum
{
	LEDSEG_MODE_LOOP=0,
	LEDSEG_MODE_LOOP_END,
	LEDSEG_MODE_BOUNCE,
	LEDSEG_MODE_TIMED_PULSE,
	LEDSEG_MODE_NOF_MODES
}ledSegmentMode_t;

/*
 * This struct describes a setting used for the a ledSegmentPulse
 */
typedef struct
{
	ledSegmentMode_t mode;	//The current mode for this pulse

	uint8_t r_max;		//Pulse max colour
	uint8_t g_max;		//Pulse max colour
	uint8_t b_max;		//Pulse max colour

	//Number of LEDs in the pulse. The total number of lit LEDs in a pulse is the sum of all these.
	uint16_t ledsMaxPower;		//The number of LEDs that shall be the middle of the pulse (the number of LEDs using max power)
	uint16_t ledsFadeBefore;	//The number of LEDs to be faded before the max LED segment start
	uint16_t ledsFadeAfter;		//The number of LEDs to be faded after the max LED segment end
	uint16_t startLed;			//The LED to start with. If larger than the total LEDs in the segment, it will start from the top

	int8_t startDir;				//The direction we shall start in (+1 or -1)
	uint16_t pixelsPerIteration;	//The number of pixels the pulse shall move per iteration
	uint16_t pixelTime;				//The number of multiples of LEDSEG_UPDATE_PERIOD_TIME between moving the pulse forward
	uint32_t cycles;				//If cycles=0, it will run forever. Cycles only has effect in MODE_LOOP_END
	uint8_t globalSetting;			//The global setting to be used

}ledSegmentPulseSetting_t;

/*
 * Describes the fade setting for an LED segment
 */
typedef struct
{
	ledSegmentMode_t mode;		//The current mode for this fade
	//All min/max are the top and bottom values of the colour for all LEDs
	uint8_t r_min;
	uint8_t r_max;
	uint8_t g_min;
	uint8_t g_max;
	uint8_t b_min;
	uint8_t b_max;

	uint32_t fadeTime;				//The time to fade from min to max
	uint16_t fadePeriodMultiplier;	//This is used for long fades to avoid capping the rate
	int8_t startDir;				//The direction we shall start in (+1 or -1)
	uint32_t cycles;				//If cycles=0, it will run forever (or rather for max uint32 cycles)
	uint8_t globalSetting;			//The global setting to be used
}ledSegmentFadeSetting_t;

/*
 * This struct describes the state of an LED segment.
 */
typedef struct
{
	//These are used for the fade setting
	uint8_t r;
	uint8_t g;
	uint8_t b;
	//The increase/decrease each iteration
	uint8_t r_rate;
	uint8_t g_rate;
	uint8_t b_rate;
	int8_t fadeDir;				//The current direction of fade

	int8_t pulseDir;				//The wander direction for the LED
	uint16_t currentLed;			//The current first LED in the pulse (the most faded LED before the start of max)
	uint16_t cyclesToPulseMove;		//The number of cycles left to pulse movement
	uint16_t cyclesToFadeChange;	//The number of cycles left to fade update (used to emulate fractional rates). This does not need to be set
	uint32_t pulseCycle;			//The current cycle of the animation
	uint32_t fadeCycle;				//The current cycle of the animation (strictly speaking, this is number of updates left (one cycleFade is much smaller than a setting cycle
	bool fadeActive;				//Indicates if the strip has an active fade
	bool pulseActive;				//Indicates if the strip has an active pulse
	ledSegmentPulseSetting_t confPulse;	//All information about the pulse
	ledSegmentFadeSetting_t confFade;		//All information about the fadeö

}ledSegmentState_t;

//Todo: implement pulse restart time

/*
 * Describes an LED segment
 */
typedef struct
{
	uint8_t strip;
	uint16_t start;
	uint16_t stop;
	ledSegmentState_t state;
}ledSegment_t;

uint8_t ledSegInitSegment(uint8_t strip, uint16_t start, uint16_t stop,ledSegmentPulseSetting_t* pulse, ledSegmentFadeSetting_t* fade);
bool ledSegExists(uint8_t seg);
bool ledSegSetPulse(uint8_t seg, ledSegmentPulseSetting_t* ps);
bool ledSegSetFade(uint8_t seg, ledSegmentFadeSetting_t* fs);
void ledSegRunIteration();
bool ledSegSetFadeMode(uint8_t seg, ledSegmentMode_t mode);
bool ledSegSetPulseMode(uint8_t seg, ledSegmentMode_t mode);
bool ledSegSetLed(uint8_t seg, uint16_t led, uint8_t r, uint8_t g, uint8_t b);

bool ledSegGetPulseActiveState(uint8_t seg);
bool ledSegSetPulseActiveState(uint8_t seg, bool state);
bool ledSegGetFadeActiveState(uint8_t seg);
bool ledSegSetFadeActiveState(uint8_t seg, bool state);
bool ledSegClearFade(uint8_t seg);
bool ledSegClearPulse(uint8_t seg);
bool ledSegSetGlobal(uint8_t seg, uint8_t fadeGlobal, uint8_t pulseGlobal);

bool ledSegRestart(uint8_t seg, bool restartFade, bool restartPulse);


#endif /* LEDSEGMENT_H_ */

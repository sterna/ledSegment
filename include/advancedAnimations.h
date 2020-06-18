/*
 * advancedAnimations.h
 *
 *  Created on: 16 Apr 2020
 *      Author: Sterna
 */

#ifndef INCLUDE_ADVANCEDANIMATIONS_H_
#define INCLUDE_ADVANCEDANIMATIONS_H_

#include "ledSegment.h"
#include "utils.h"
#include "time.h"

//The call period for the animation task in ms
#define ANIM_TASK_PERIOD	55

/*
 * Handles the types of the different advanced animation modes
 */
typedef enum
{
	ANIM_NO_ANIMATION,
	ANIM_SWITCH_MODE,
	ANIM_DISCO,
	ANIM_NOF_MODES
}animMode_t;

typedef enum
{
	SIMPLE_COL_RED,
	SIMPLE_COL_GREEN,
	SIMPLE_COL_BLUE,
	SIMPLE_COL_PURPLE,
	SIMPLE_COL_CYAN,
	SIMPLE_COL_YELLOW,
	SIMPLE_COL_WHITE,
	SIMPLE_COL_NOF_COLOURS,
	SIMPLE_COL_RANDOM,
	SIMPLE_COL_OFF,
	SIMPLE_COL_NO_CHANGE
}simpleCols_t;


typedef enum
{
	PRIDE_COL_RED,
	PRIDE_COL_ORANGE,
	PRIDE_COL_YELLOW,
	PRIDE_COL_GREEN,
	PRIDE_COL_INDIGO,
	PRIDE_COL_PURPLE,
	PRIDE_COL_NOF_COLOURS,
}prideCols_t;

RGB_t animGetColour(simpleCols_t col);
void animLoadLedSegFadeColour(simpleCols_t col,ledSegmentFadeSetting_t* st, uint8_t minScale, uint8_t maxScale);
void animLoadLedSegPulseColour(simpleCols_t col,ledSegmentPulseSetting_t* st, uint8_t maxScale);
void animLoadLedSegFadeBetweenColours(simpleCols_t colFrom, simpleCols_t colTo, ledSegmentFadeSetting_t* st, uint8_t fromScale, uint8_t toScale);
void animSetModeChange(simpleCols_t col, ledSegmentFadeSetting_t* fs, uint8_t seg, bool switchAtMax, uint8_t minScale, uint8_t maxScale);

void animSetPrideWheel(ledSegmentFadeSetting_t* fs, uint8_t seg);
void animSetPrideWheelState(bool active);
bool animPrideWheelGetDone();

void animTask();

#endif /* INCLUDE_ADVANCEDANIMATIONS_H_ */

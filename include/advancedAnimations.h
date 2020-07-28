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

//The maximum number of allowed animation points in one sequence (each point consumes 60B of SRAM)
#define ANIM_SEQ_MAX_POINTS	10
//The maximum possible of saved animation sequences (each animation sequence uses ANIM_SEQ_MAX_POINTS*60B+12B SRAM)
#define ANIM_SEQ_MAX_SEQS	5

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
	SIMPLE_COL_RED=0,
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
	PRIDE_COL_RED=0,
	PRIDE_COL_ORANGE,
	PRIDE_COL_YELLOW,
	PRIDE_COL_GREEN,
	PRIDE_COL_INDIGO,
	PRIDE_COL_PURPLE,
	PRIDE_COL_NOF_COLOURS
}prideCols_t;

typedef enum
{
	PAN_COL_PINK=0,
	PAN_COL_YELLOW,
	PAN_COL_BLUE,
	PAN_COL_NOF_COLOURS
}panCols_t;

/*
 * Defines a single point of animation setting
 * the mode and cycles of the fade and pulse controls how long this point runs for
 */
typedef struct
{
	ledSegmentFadeSetting_t fade;	//The fade setting used for this specific point
	bool fadeUsed;
	ledSegmentPulseSetting_t pulse;	//The fade setting used for this specific point
	bool pulseUsed;
	uint32_t waitAfter;				//The time the final state (after both fade/pulse is done) shall persist (in ms)
	bool waitForTrigger;			//Set the point to wait for a trigger to initiate switching to next. Once trigger is received, waitAfter time will start
	bool switchAtMax;
	bool fadeToNext;				//Indicates if we should fade into the next point or not Todo: Consider renaming to fadeToThis or something
}animSeqPoint_t;


extern const RGB_t coloursSimple[SIMPLE_COL_NOF_COLOURS];
extern const RGB_t coloursPride[PRIDE_COL_NOF_COLOURS];
extern const RGB_t coloursPan[PAN_COL_NOF_COLOURS];

RGB_t animGetColour(simpleCols_t col, uint8_t normalize);
RGB_t animGetColourPride(prideCols_t col, uint8_t normalize);
RGB_t animGetColourFromSequence(RGB_t* colourList, uint8_t num, uint8_t normalize);
RGB_t animNormalizeColours(const RGB_t* cols, uint8_t normalVal);
void animLoadLedSegFadeColour(simpleCols_t col,ledSegmentFadeSetting_t* st, uint8_t minScale, uint8_t maxScale);
void animLoadLedSegPulseColour(simpleCols_t col,ledSegmentPulseSetting_t* st, uint8_t maxScale);
void animLoadLedSegFadeBetweenColours(simpleCols_t colFrom, simpleCols_t colTo, ledSegmentFadeSetting_t* st, uint8_t fromScale, uint8_t toScale);
void animSetModeChange(simpleCols_t col, ledSegmentFadeSetting_t* fs, uint8_t seg, bool switchAtMax, uint8_t minScale, uint8_t maxScale, bool updateSetting);

void animSetPrideWheel(ledSegmentFadeSetting_t* fs, uint8_t seg);
void animSetPrideWheelState(bool active);
bool animPrideWheelGetDone();
prideCols_t animLoadNextRainbowWheel(ledSegmentFadeSetting_t* fs, uint8_t seg, prideCols_t colIndex);

uint8_t animSeqInit(uint8_t seg, bool isSyncGroup, uint32_t cycles, animSeqPoint_t* points, uint8_t nofPoints);
void animSeqFillPoint(animSeqPoint_t* point, ledSegmentFadeSetting_t* fs, ledSegmentPulseSetting_t* ps, uint32_t waitAfter, bool waitForTrigger, bool fadeToNext, bool switchAtMax);
bool animSeqExists(uint8_t seqNum);
bool animSeqAppendPoint(uint8_t seqNum, animSeqPoint_t* point);
bool animSeqRemovePoint(uint8_t seqNum, uint8_t n);
void animSeqSetRestart(uint8_t seqNum);
bool animSeqTrigReady(uint8_t seqNum);
void animSeqTrigTransition(uint8_t seqNum);
void animSeqSetActive(uint8_t seqNum, bool active);
bool animSeqIsActive(uint8_t seqNum);

void animTask();

#endif /* INCLUDE_ADVANCEDANIMATIONS_H_ */

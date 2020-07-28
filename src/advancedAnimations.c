/*
 * advancedAnimations.c
 *
 *  Created on: 16 Apr 2020
 *      Author: Sterna
 *
 * Handles more advanced animations that utilize he built-in animations of the ledSegment.c/h.
 *
 * Animations include:
 * - Fade from one colour mode to another (soft switching of modes)
 * - Better colour handling
 * - Pre-defined settings and patterns
 * - Rainbow colour
 * - Disco mode
 * - Animation sequencing (with and without trig)
 * (Basically a lot of the stuff that was done within the main loop of the software)
 *
 * Animation sequence usage:
 * - A sequence consists of multiple animation points. An animation point is defined with a pulse and a fade setting (same as usual).
 * - A point also has some other settings:
 * 	- waitAfter - sets the time to wait until loading the next point
 * 	- waitForTrig - waits for an external trig before loading the next point
 * 	- fadeToNext - if the fade shall fade from the current colour to the next colour. Also supports the setting of switching at max/min for this
 * The program will run a the sequence and load a new point (new settings) whenever each point is done (when both fade and pulse are done).
 * The sequence has a cycle counter itself, and can be set to run for any number of cycles. As usual, if 0 is set, it will run forever.
 * Animation sequence supports running using LESEG_ALL.
 *
 */

#include "advancedAnimations.h"

typedef enum
{
	ANIM_TRIG_NOT_READY=0,	//Trigger is not ready (fade/pulse are not done yet)
	ANIM_TRIG_READY=1,		//Trigger is ready to be activated
	ANIM_TRIG_ACTIVATED=2	//Trigger is activated
}animTriggerState_t;

/*
 * Defines a full animation program
 */
typedef struct
{
	animSeqPoint_t points[ANIM_SEQ_MAX_POINTS];	//A list of the animation points used for this animation sequence
	uint8_t currentPoint;		//The current point of animation we're one
	uint8_t nofPoints;
	uint32_t cyclesSetting;		//The number of cycles the animation shall run for. If cycles = 0 from the start, it will loop indefinitely
	uint32_t cyclesLeft;		//The number of cycles the animation has left
	uint8_t seg;				//Segment to run this animation sequence on. If isSyncGroup is true, this is instead the sync group
	bool isSyncGroup;			//Indicates if sync group is used
	bool isActive;				//Indicates if this sequence is currently running (this means that the current pulse and fade will run until done)
	uint32_t waitReleaseTime;	//The time at which the next point shall be loaded (set internally)
	animTriggerState_t waitReleaseTrigger;	//Is true if we're waiting for a manual trigger (set internally)
	bool isFadingToNextPoint;	//Indicates if we're currently fading to the next point (set internally)
}animSequence_t;

static void animSeqLoadCurrentPoint(animSequence_t* seq, bool firstPoint);


static animSequence_t animSeqs[ANIM_SEQ_MAX_SEQS];
static uint8_t animSeqsNofSeqs=0;


const RGB_t coloursSimple[SIMPLE_COL_NOF_COLOURS]=
{
	{255,0,0},			//Red
	{0,255,0},			//Green
	{0,0,255},			//Blue
	{255,0,255},		//Purple
	{0,255,255},		//Cyan
	{255,255,0},		//Yellow
	{255,255,255}		//White
};


const RGB_t coloursPride[PRIDE_COL_NOF_COLOURS]=
{
	{0xE7,0,0},			//Red
	{0xFF,0x60,0},		//Orange
	{0xFF,0xEF,0},		//Yellow
	{0,0xFF,0x10},		//Green
	{0,0x20,0xFF},		//Indigo
	{0x76,0,0x79},		//Purple
};

const RGB_t coloursPan[PAN_COL_NOF_COLOURS]=
{
	{0xFF,0x1B,0x8D},		//Pink
	{0xFF,0xDA,0x00},		//Yellow
	{0x1B,0xB3,0xFF},		//Blue
};

/*
 * Returns a given simple colour
 * If normalize is given as larger than 0, the colour will be normalized to produce a total output of that value
 */
RGB_t animGetColour(simpleCols_t col, uint8_t normalize)
{
	RGB_t temp={0,0,0};
	if(col == SIMPLE_COL_RANDOM)
	{
		temp=coloursSimple[utilRandRange(SIMPLE_COL_NOF_COLOURS-1)];
	}
	else if(col < SIMPLE_COL_NOF_COLOURS)
	{
		temp=coloursSimple[col];
	}
	if(normalize>0)
	{
		return animNormalizeColours(&temp,normalize);
	}
	return temp;
}

/*
 * Returns the a colour from the pride list
 * If normalize is given as larger than 0, the colour will be normalized to produce a total output of that value
 */
RGB_t animGetColourPride(prideCols_t col, uint8_t normalize)
{
	RGB_t temp={0,0,0};
	temp=coloursPride[col];
	if(normalize>0)
	{
		return animNormalizeColours(&temp,normalize);
	}
	return temp;
}

/*
 * Extracts and normalizes (if given) a colour from a given colour list.
 * Will NOT check if num is out of sequence.
 */
RGB_t animGetColourFromSequence(RGB_t* colourList, uint8_t num, uint8_t normalize)
{
	RGB_t tmp={0,0,0};
	if(colourList==NULL)
	{
		return tmp;
	}
	tmp.r=colourList[num].r;
	tmp.g=colourList[num].g;
	tmp.b=colourList[num].b;
	if(normalize)
	{
		animNormalizeColours(&tmp,normalize);
	}
	return tmp;
}

/*
 * Normalizes the given colour to the given maximum power output.
 * That is, the sum of the power of all colours shall be the given normalVal.
 * This ensures that all combinations of colours are the same total PWM
 */
RGB_t animNormalizeColours(const RGB_t* cols, uint8_t normalVal)
{
	uint32_t totalLight=cols->r + cols->g + cols->b;
	RGB_t tmpCols;
	tmpCols.r=(uint8_t)utilScale(cols->r,totalLight,normalVal);
	tmpCols.g=(uint8_t)utilScale(cols->g,totalLight,normalVal);
	tmpCols.b=(uint8_t)utilScale(cols->b,totalLight,normalVal);
	return tmpCols;
}

/*
 * Load new colours for a given ledFadeSegment
 * minScale/maxScale are scaling factors (0-255) for the max and min value
 */
void animLoadLedSegFadeColour(simpleCols_t col,ledSegmentFadeSetting_t* st, uint8_t minScale, uint8_t maxScale)
{
	RGB_t tmpCol=animGetColour(col,255);
	st->r_max = (uint8_t)utilScale(tmpCol.r,255,maxScale);
	st->r_min = (uint8_t)utilScale(tmpCol.r,255,minScale);
	st->g_max = (uint8_t)utilScale(tmpCol.g,255,maxScale);
	st->g_min = (uint8_t)utilScale(tmpCol.g,255,minScale);
	st->b_max = (uint8_t)utilScale(tmpCol.b,255,maxScale);
	st->b_min = (uint8_t)utilScale(tmpCol.b,255,minScale);
}

/*
 * Load new colours for a given ledFadeSegment
 */
void animLoadLedSegPulseColour(simpleCols_t col,ledSegmentPulseSetting_t* st, uint8_t maxScale)
{
	RGB_t tmpCol=animGetColour(col,maxScale);
	st->r_max = tmpCol.r;
	st->g_max = tmpCol.g;
	st->b_max = tmpCol.b;
}

/*
 * Sets up a fade from one colour to another one
 * MaxScale indicates what the max colour of each colour should be scaled by
 */
void animLoadLedSegFadeBetweenColours(simpleCols_t colFrom, simpleCols_t colTo, ledSegmentFadeSetting_t* st, uint8_t fromScale, uint8_t toScale)
{
	RGB_t from=animGetColour(colFrom,fromScale);
	RGB_t to=animGetColour(colTo,toScale);
	st->r_min = from.r;
	st->r_max = to.r;
	st->g_min = from.g;
	st->g_max = to.g;
	st->b_min = from.b;
	st->b_max = to.b;
}

/*
 * Sets up a mode where you switch from one mode to another (soft fade between the two fade colours)
 * fs is the fade setting to fade TO (this just loads the correct colour)
 * switchAtMax indicates of the modeChange-animation shall end on min or max
 * Note that this will actually perform the fadeSet, so a new fadeSet of this fadesetting with this segment should not be performed
 * If updateSetting is given, the fadeSetting is changed/colour is loaded)
 */
void animSetModeChange(simpleCols_t col, ledSegmentFadeSetting_t* fs, uint8_t seg, bool switchAtMax, uint8_t minScale, uint8_t maxScale, bool updateSetting)
{
	ledSegmentFadeSetting_t fsTmp;
	if(updateSetting)
	{
		//Load the setting as normal (will give us the max setting, as fade by default starts from max)
		if(col!=SIMPLE_COL_NO_CHANGE)
		{
			animLoadLedSegFadeColour(col,fs,minScale,maxScale);
		}
		//Send the change we want to have to the ledSeg, as no-one else should access and change the state
		ledSegSetModeChange(fs,seg,switchAtMax);
	}
	else
	{
		memcpy(&fsTmp,fs,sizeof(ledSegmentFadeSetting_t));
		//Load the setting as normal (will give us the max setting, as fade by default starts from max)
		if(col!=SIMPLE_COL_NO_CHANGE)
		{
			animLoadLedSegFadeColour(col,&fsTmp,minScale,maxScale);
		}
		//Send the change we want to have to the ledSeg, as no-one else should access and change the state
		ledSegSetModeChange(&fsTmp,seg,switchAtMax);
	}
}

static bool prideWheelActive=false;
static bool prideWheelDone=false;
static ledSegmentFadeSetting_t prideWheelSetting;
static uint8_t prideWheelSeg=0;
static prideCols_t prideWheelIndex=0;
static uint32_t prideCycles=0;

/*
 * Sets up a rainbow wheel fade given the settings given. Only the following settings are valid:
 * - Global setting (normal min/max scale is not possible, since these are set colour ratios)
 * - Cycles (one cycle is one full cycle through the whole rainbow)
 * - FadeTime (the time it takes to from one colour to the next)
 * - syncGroup
 * All other settings are generated internally and overwritten.
 *
 * Procedure:
 *  1. Load a fade the goes from colour 0 to colour 1.
 *  2. Setup a mode change to this setting.
 *  3. When fade is done, we are AT colour 1. Then, setup the next fade as in step 1, but fade from colour 2 to 3.
 *
 */
void animSetPrideWheel(ledSegmentFadeSetting_t* fs, uint8_t seg)
{
	//Copy setting and setup for rainbow wheel (
	memcpy(&prideWheelSetting,fs,sizeof(ledSegmentFadeSetting_t));
	prideCycles=PRIDE_COL_NOF_COLOURS*fs->cycles;
	//Min/Max scale doesn't matter, as we only do one cycle
	prideWheelIndex=animLoadNextRainbowWheel(&prideWheelSetting,seg,PRIDE_COL_RED);
	prideWheelSeg=seg;
	prideWheelActive=true;
	prideWheelDone=false;
}

/*
 * Loads the next colour into the fade setting for pride wheel
 */
prideCols_t animLoadNextRainbowWheel(ledSegmentFadeSetting_t* fs, uint8_t seg, prideCols_t colIndex)
{
	const RGB_t tmpCol1=animGetColourPride(colIndex,255);//animNormalizeColours(&prideColours[colIndex],255);
	colIndex=utilIncLoopSimple(colIndex,(PRIDE_COL_NOF_COLOURS-1));
	const RGB_t tmpCol2=animGetColourPride(colIndex,255);
	colIndex=utilIncLoopSimple(colIndex,(PRIDE_COL_NOF_COLOURS-1));
	fs->r_min=tmpCol1.r;
	fs->g_min=tmpCol1.g;
	fs->b_min=tmpCol1.b;
	fs->r_max=tmpCol2.r;
	fs->g_max=tmpCol2.g;
	fs->b_max=tmpCol2.b;
	fs->startDir=1;
	fs->cycles=1;
	fs->mode=LEDSEG_MODE_LOOP_END;
	//Since both colours are already loaded, animSetModeChange shall not load any new colour.
	//We need to switch at max, since there's only one fade cycle going from min to max (and then a new switch is loaded)
	animSetModeChange(SIMPLE_COL_NO_CHANGE,fs,seg,false,0,255,false);
	return colIndex;
}

/*
 * Returns true if pridewheel is done
 */
bool animPrideWheelGetDone()
{
	return prideWheelDone;
}

/*
 * Turns the pride wheel on/off
 */
void animSetPrideWheelState(bool active)
{
	prideWheelActive=active;
}

/*
 * Inits an animation sequence. Will return a number to use to refer to the animation sequence
 * Todo: Support for sync group is not done yet, don't use it
 */
uint8_t animSeqInit(uint8_t seg, bool isSyncGroup, uint32_t cycles, animSeqPoint_t* points, uint8_t nofPoints)
{
	//Check validity
	if(animSeqsNofSeqs>=ANIM_SEQ_MAX_SEQS || !ledSegExists(seg) || nofPoints>ANIM_SEQ_MAX_POINTS)
	{
		return ANIM_SEQ_MAX_SEQS+1;
	}
	//Load all settings into state and reset everything accordingly
	animSequence_t* seq=&animSeqs[animSeqsNofSeqs];
	memset(seq,0,sizeof(animSequence_t));
	seq->cyclesSetting=cycles;
	seq->cyclesLeft=cycles;
	seq->currentPoint=0;
	seq->isFadingToNextPoint=false;
	seq->isSyncGroup=isSyncGroup;
	seq->nofPoints=nofPoints;
	seq->seg=seg;
	seq->waitReleaseTime=0;
	memcpy(seq->points,points,nofPoints*sizeof(animSeqPoint_t));

	//Load first point (if it's a fade, it will be handled by the task later)
	animSeqLoadCurrentPoint(seq,true);
	animSeqsNofSeqs++;
	return animSeqsNofSeqs-1;
}

/*
 * Fills a point with given data
 */
void animSeqFillPoint(animSeqPoint_t* point, ledSegmentFadeSetting_t* fs, ledSegmentPulseSetting_t* ps, uint32_t waitAfter, bool waitForTrigger, bool fadeToNext, bool switchAtMax)
{
	point->fadeUsed=false;
	point->pulseUsed=false;
	if(fs!=NULL)
	{
		memcpy(&(point->fade),fs,sizeof(ledSegmentFadeSetting_t));
		point->fadeUsed=true;
	}
	if(ps!=NULL)
	{
		memcpy(&(point->pulse),ps,sizeof(ledSegmentPulseSetting_t));
		point->pulseUsed=true;
	}
	point->waitAfter=waitAfter;
	point->fadeToNext=fadeToNext;
	point->switchAtMax = switchAtMax;
	point->waitForTrigger=waitForTrigger;
}

/*
 * Append a single point into an animation sequence
 */
bool animSeqAppendPoint(uint8_t seqNum, animSeqPoint_t* point)
{
	if(!animSeqExists(seqNum))
	{
		return false;
	}
	animSequence_t* seq=&animSeqs[seqNum];
	if(seq->nofPoints>ANIM_SEQ_MAX_POINTS)
	{
		return false;
	}
	seq->nofPoints++;
	memcpy(&(seq->points[seq->nofPoints-1]),point,sizeof(animSeqPoint_t));
	return true;
}

/*
 * Removes the n last points in an animation sequence
 * If all points are removed, the segment will not update
 */
bool animSeqRemovePoint(uint8_t seqNum, uint8_t n)
{
	if(!animSeqExists(seqNum))
	{
		return false;
	}
	uint8_t currentPoints=animSeqs[seqNum].nofPoints;
	if(n>currentPoints)
	{
		n=currentPoints;
	}
	animSeqs[seqNum].nofPoints=currentPoints-n;
	return true;
}

/*
 * Returns true if an animation sequence exists
 */
bool animSeqExists(uint8_t seqNum)
{
	if(seqNum<animSeqsNofSeqs)
	{
		return true;
	}
	return false;
}

/*
 * Returns true if the animation sequence is active
 * If LEDSEG_ALL (255) is given, it will return true if any animationSequence is active
 */
bool animSeqIsActive(uint8_t seqNum)
{
	if(seqNum==LEDSEG_ALL)
	{
		for(uint8_t i=0; i<animSeqsNofSeqs;i++)
		{
			if(animSeqIsActive(i))
			{
				return true;
			}
		}
		return false;
	}
	if(!animSeqExists(seqNum))
	{
		return false;
	}
	return animSeqs[seqNum].isActive;
}

/*
 * Enables/disables an animation sequence
 */
void animSeqSetActive(uint8_t seqNum, bool active)
{
	if(!animSeqExists(seqNum))
	{
		return;
	}
	animSeqs[seqNum].isActive=active;
}

/*
 * Restarts an animation sequence from the first point
 */
void animSeqSetRestart(uint8_t seqNum)
{
	if(!animSeqExists(seqNum))
	{
		return;
	}
	animSeqs[seqNum].cyclesLeft=animSeqs[seqNum].cyclesSetting;
	animSeqs[seqNum].currentPoint=0;
	animSeqs[seqNum].isActive=true;
	animSeqLoadCurrentPoint(&animSeqs[seqNum],true);
}

/*
 * Trigger an animation transition manually, if ready.
 * If LEDSEG_ALL is given, it will try to trigger all animSeqs
 * Note: A trigger cannot be ready unless enabled for the point
 */
void animSeqTrigTransition(uint8_t seqNum)
{
	if(seqNum==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<animSeqsNofSeqs;i++)
		{
			animSeqTrigTransition(i);
		}
	}
	if(!animSeqExists(seqNum))
	{
		return;
	}
	if(animSeqs[seqNum].waitReleaseTrigger==ANIM_TRIG_READY)
	{
		animSeqs[seqNum].waitReleaseTrigger=ANIM_TRIG_ACTIVATED;
	}
}

/*
 * Returns true if an animation sequence is ready to be triggered.
 * If LEDSEG_ALL is given, true will only be returned if all seqs are ready
 */
bool animSeqTrigReady(uint8_t seqNum)
{
	if(seqNum==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<animSeqsNofSeqs;i++)
		{
			if(!animSeqTrigReady(i))
			{
				return false;
			}
		}
		return true;
	}
	if(!animSeqExists(seqNum))
	{
		return false;
	}
	if(animSeqs[seqNum].waitReleaseTrigger==ANIM_TRIG_READY)
	{
		return true;
	}
	else
	{
		return false;
	}
}

/*
 * Loads the current point into from an animation sequence
 * Does not change any point state or anything
 */
static void animSeqLoadCurrentPoint(animSequence_t* seq, bool firstPoint)
{
	//We have updated the current point and checked everything. Load the new segment settings
	animSeqPoint_t* point=&(seq->points[seq->currentPoint]);
	const uint8_t seg=seq->seg;
	bool fadeActive=false;
	bool pulseActive=false;
	if(point->fadeUsed)
	{
		fadeActive=true;
	}
	if(point->pulseUsed)
	{
		pulseActive=true;
	}
	//If mode change fade is used, don't update pulse until we're
	if(fadeActive)
	{
		if(point->fadeToNext)
		{
			animSetModeChange(SIMPLE_COL_NO_CHANGE,&point->fade,seg,point->switchAtMax,0,0,false);
			seq->isFadingToNextPoint=true;
		}
		else
		{
			ledSegSetFade(seg,&point->fade);
		}
	}
	else
	{
//		ledSegSetFadeActiveState(seg,false);
		ledSegSetFadeActiveState(seg,true);	//Set fade active, but don't restart or update it
	}
	/*
	 * 4 cases:
	 * !pActive && !fading - disable pulse immediately
	 * !pActive && fading - do nothing. Keep existing pulse state until fade is done. Once fade is done, clear existing pulse
	 * pActive && !fading - update pulse immediately
	 * pActive && fading - do nothing. Keep existing pulse state until fade is done. Once fade is done, load next pulse
	 */
	if(!seq->isFadingToNextPoint || firstPoint)
	{
		if(pulseActive)
		{
			ledSegSetPulse(seg,&point->pulse);
		}
		else
		{
			ledSegSetPulseActiveState(seg,false);
		}
	}
	//Always reset external trigger state when loading a new point
	seq->waitReleaseTrigger=ANIM_TRIG_NOT_READY;
}

/*
 * The main task that handles all time stepping things
 * that I didn't want to put into the regular ledSegment loop
 */
void animTask()
{
	static uint32_t nextCallTime=0;
	if(systemTime < nextCallTime)
	{
		return;
	}
	nextCallTime=systemTime+ANIM_TASK_PERIOD;

	//Rainbow wheel generation
	if(prideWheelActive)
	{
		if(ledSegGetFadeDone(prideWheelSeg))
		{
			prideWheelIndex=animLoadNextRainbowWheel(&prideWheelSetting,prideWheelSeg,prideWheelIndex);
			if(prideCycles)
			{
				if(prideCycles<=2)
				{
					prideCycles=0;
					prideWheelActive=false;
					prideWheelDone=true;
				}
				else
				{
					prideCycles-=2;
				}
			}
		}
	}

	//Go through and update all animations sequences
	//Todo: Consider dividing this into multiple calls to spread out CPU load slightly
	for(uint8_t i=0;i<animSeqsNofSeqs;i++)
	{
		animSequence_t* seq=&animSeqs[i];
		if(seq->isActive && (seq->nofPoints>0))
		{
			const uint8_t seg=seq->seg;
			const bool isSyncGrp=seq->isSyncGroup;

			//Check if fade and pulse and done (and if the entire segment/syncGroup is done). If not, next.
			bool fadeDone=false;
			bool pulseDone=false;
			if(isSyncGrp)
			{
				fadeDone=ledSegGetSyncGroupDone(seg);
				pulseDone=true;	//Todo: Once we have support for pulse done in sync groups, add this here
			}
			else
			{
				if(ledSegGetFadeDone(seg) || !seq->points[seq->currentPoint].fadeUsed)
				{
					fadeDone=true;
				}
				if(ledSegGetPulseDone(seg) || !seq->points[seq->currentPoint].pulseUsed)
				{
					pulseDone=true;
				}
//				fadeDone=ledSegGetFadeDone(seg);
//				pulseDone=ledSegGetPulseDone(seg);
			}
			if(fadeDone && pulseDone)
			{
				//We now know that the point is done. Check if we need to keep waiting.

				//Check if we are using external trigger and if it's ready
				bool trigReady=false;
				//Set trigger to ready, if it's not
				if(seq->points[seq->currentPoint].waitForTrigger)
				{
					if(seq->waitReleaseTrigger == ANIM_TRIG_NOT_READY)
					{
						seq->waitReleaseTrigger = ANIM_TRIG_READY;
					}
					if(seq->waitReleaseTrigger==ANIM_TRIG_ACTIVATED)
					{
						trigReady=true;
					}
				}
				else
				{
					trigReady=true;
				}
				if(trigReady)
				{
					//Check if we have started waiting
					if(!seq->waitReleaseTime)
					{
						seq->waitReleaseTime=seq->points[seq->currentPoint].waitAfter+systemTime;
					}
					if(seq->waitReleaseTime <= systemTime)
					{
						seq->waitReleaseTime=0;	//Ensure this is done only once (as soon as new settings are loaded, fade and pulse will stop being done)

						//We have waited now and the current point is now finished.
						//Update and check point counter
						seq->currentPoint++;
						if(seq->currentPoint>=seq->nofPoints)
						{
							//Check cycle counter if we shall continue looping
							//Cycles==0 means infite loop
							if(seq->cyclesLeft==0)
							{
								seq->currentPoint=0;
							}
							else
							{
								seq->cyclesLeft--;
								if(seq->cyclesLeft)
								{
									//We still have cycles left
									seq->currentPoint=0;
								}
								else
								{
									seq->isActive=false;
								}
							}
						}
						if(seq->isActive)
						{
							animSeqLoadCurrentPoint(seq,false);
						}
					}
				}
			}
			if(seq->isFadingToNextPoint && ledSegGetFadeSwitchDone(seg))
			{
				ledSegmentPulseSetting_t* ps=&(seq->points[seq->currentPoint].pulse);
				if(ps)
				{
					ledSegSetPulse(seg,ps);
				}
				else
				{
					ledSegSetPulseActiveState(seg,false);
				}
				seq->isFadingToNextPoint=false;
			}
		}
	}	//End of go through animation sequences
}

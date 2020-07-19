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
 *
 * (Basically a lot of the stuff that was done within the main loop of the software)
 */


/*
 * Animation sequencing
 * Setup:
 * 1. Store a series of settings (fade and pulse) in a list. Include the length of list and mode (loop list/end list). These settings are stored per segment or sync group
 * 2. Load the first settings for all
 *
 * Loop
 * 1. Check which mode is used
 * 2. Check if all fades in sync group are properly done.
 * 3. Load the next setting in the sequence to all segments using it.
 * 4. Increment setting counter (with wrap around)
 */

#include "advancedAnimations.h"

/*
 * Defines a single point of animation setting
 * the mode and cycles of the fade and pulse controls how long this point runs for
 */
typedef struct
{
	ledSegmentFadeSetting_t fade;	//The fade setting used for this specific point
	ledSegmentPulseSetting_t pulse;	//The fade setting used for this specific point
	uint32_t waitAfter;				//The time the final state (after both fade/pulse is done) shall persist (in ms)
	uint8_t fadeMinScale;
	uint8_t fadeMaxScale;
	uint8_t pulseMaxScale;
	bool switchAtMax;
	bool fadeToNext;				//Indicates if we should fade into the next point or not
}animSeqPoint_t;

/*
 * Defines a full animation program
 */
typedef struct
{
	animSeqPoint_t points[ANIM_SEQ_MAX_POINTS];	//A list of the animation points used for this animation sequence
	uint8_t currentPoint;	//The current point of animation we're one
	uint8_t nofPoints;
	uint32_t cycles;		//The number of cycles the animation shall run for. If cycles = 0 from the start, it will loop indefinitely
	uint8_t seg;			//Segment to run this animation sequence on. If isSyncGroup is true, this is instead the sync group
	bool isSyncGroup;		//Indicates if sync group is used
	bool isActive;			//Indicates if this sequence is currently running
	uint32_t waitReleaseTime;	//The time at which the next point shall be loaded
	bool isFadingToNextPoint;	//Indicates if we're currently fading to the next point
}animSequence_t;

static prideCols_t animLoadNextRainbowWheel(ledSegmentFadeSetting_t* fs, uint8_t seg, prideCols_t colIndex);
static void animSeqLoadCurrentPoint(animSequence_t* seq);


static animSequence_t animSeqs[ANIM_SEQ_MAX_SEQS];
static uint8_t animSeqsNofSeqs=0;


const RGB_t simpleColours[SIMPLE_COL_NOF_COLOURS]=
{
	{255,0,0},			//Red
	{0,255,0},			//Green
	{0,0,255},			//Blue
	{255,0,255},		//Purple
	{0,255,255},		//Cyan
	{255,255,0},		//Yellow
	{255,255,255}		//White
};


const RGB_t prideColours[PRIDE_COL_NOF_COLOURS]=
{
	{0xE7,0,0},			//Red
	{0xFF,0x60,0},		//Orange
	{0xFF,0xEF,0},		//Yellow
	{0,0xFF,0x20},		//Green
	{0,0x20,0xFF},		//Indigo
	{0x76,0,0x79},		//Purple
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
		temp=simpleColours[utilRandRange(SIMPLE_COL_NOF_COLOURS-1)];
	}
	else if(col < SIMPLE_COL_NOF_COLOURS)
	{
		temp=simpleColours[col];
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
	temp=prideColours[col];
	if(normalize>0)
	{
		return animNormalizeColours(&temp,normalize);
	}
	return temp;
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
static prideCols_t animLoadNextRainbowWheel(ledSegmentFadeSetting_t* fs, uint8_t seg, prideCols_t colIndex)
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
	seq->cycles=cycles;
	seq->currentPoint=0;
	seq->isFadingToNextPoint=false;
	seq->isSyncGroup=isSyncGroup;
	seq->nofPoints=nofPoints;
	seq->seg=seg;
	seq->waitReleaseTime=0;
	memcpy(seq->points,points,nofPoints*sizeof(animSeqPoint_t));

	//Load first point (if it's a fade, it will be handled by the task later)
	animSeqLoadCurrentPoint(seq);
	seq->isActive=true;
	animSeqsNofSeqs++;
	return animSeqsNofSeqs-1;
}

/*
 * Append a single point into an animation sequence
 */
void animSeqAppendPoint()
{

}

/*
 * Removes the n last points in an animation sequence
 */
void animSeqRemovePoint()
{

}

/*
 * Loads the current point into from an animation sequence
 * Does not change any point state or anything
 */
static void animSeqLoadCurrentPoint(animSequence_t* seq)
{
	//We have updated the current point and checked everything. Load the new segment settings
	animSeqPoint_t* point=&(seq->points[seq->currentPoint]);
	const uint8_t seg=seq->seg;
	bool fadeActive=false;
	bool pulseActive=false;
	if(&point->fade)
	{
		fadeActive=true;
	}
	if(&point->pulse)
	{
		pulseActive=true;
	}
	//If mode change fade is used, don't update pulse until we're
	if(fadeActive)
	{
		if(point->fadeToNext)
		{
			animSetModeChange(SIMPLE_COL_NO_CHANGE,&point->fade,seg,point->switchAtMax,point->fadeMinScale,point->fadeMaxScale,false);
			seq->isFadingToNextPoint=true;
		}
		else
		{
			ledSegSetFade(seg,&point->fade);
		}
	}
	else
	{
		ledSegSetFadeActiveState(seg,false);
	}
	/*
	 * 4 cases:
	 * !pActive && !fading - disable pulse immediately
	 * !pActive && fading - do nothing. Keep existing pulse state until fade is done. Once fade is done, clear existing pulse
	 * pActive && !fading - update pulse immediately
	 * pActive && fading - do nothing. Keep existing pulse state until fade is done. Once fade is done, load next pulse
	 */
	if(!seq->isFadingToNextPoint)
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
	for(uint8_t i=0;i<animSeqsNofSeqs;i++)
	{
		animSequence_t* seq=&animSeqs[i];
		if(seq->isActive)
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
				fadeDone=ledSegGetFadeDone(seg);
				pulseDone=ledSegGetPulseDone(seg);
			}
			if(fadeDone && pulseDone)
			{
				//We now know that the point is done. Check if we need to keep waiting.
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
						if(seq->cycles==0)
						{
							seq->currentPoint=0;
						}
						else
						{
							seq->cycles--;
							if(seq->cycles)
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
						animSeqLoadCurrentPoint(seq);
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
			}
		}
	}	//End of go through animation sequences

	/*
	 * Setup:
	 * 	Load first colour in sequence as fromTo with fade_end
	 * 	Remember to setup sync groups
	 * 	Set rainbow wheel as true
	 *
	 * Loop:
	 * Check if in rainbow wheel mode
	 * 		Check if fade is done
	 * 			Load next colour in sequence as fromTo with fade_end
	 * 			Set next colour
	 * 			increment segment with wraparound
	 */


	/*
	 *  *	case SMODE_STAD_I_LJUS:
			{
				//Special mode for Stad i ljus performance
				stadILjusState=1;
				if(!modeChangeStage)
				{
					loadModeChange(DISCO_COL_YELLOW,&fade,segmentTail);
					pulseIsActive=false;
					modeChangeStage=1;
				}
				else if(modeChangeStage==2 && ledSegGetFadeDone(segmentTail))
				{
					//Todo: set fade parameters
					modeChangeStage=0;
					fade.cycles=0;
					fade.startDir=-1;
					loadLedSegFadeColour(DISCO_COL_YELLOW,&fade);
					pulseIsActive=false;
				}
				break;
			}
			}
			if(modeChangeStage<2)
			{
				ledSegSetFade(segmentTail,&fade);
				ledSegSetPulse(segmentTail,&pulse);
				ledSegSetPulseActiveState(segmentTail,pulseIsActive);
				ledSegSetFade(segmentArmLeft,&fade);
				ledSegSetPulse(segmentArmLeft,&pulse);
				ledSegSetPulseActiveState(segmentArmLeft,pulseIsActive);
				if(modeChangeStage==1)
				{
					modeChangeStage=2;
				}
			}
	 *
	 */
}

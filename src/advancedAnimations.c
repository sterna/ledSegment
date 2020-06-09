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
 * 1. Store a
 */

#include "advancedAnimations.h"

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

/*
 * Returns a given simple colour
 */
RGB_t animGetColour(simpleCols_t col)
{
	if(col == SIMPLE_COL_RANDOM)
	{
		return simpleColours[utilRandRange(SIMPLE_COL_NOF_COLOURS-1)];
	}
	else if(col >= SIMPLE_COL_NOF_COLOURS)
	{
		return (RGB_t){0,0,0};
	}
	else
	{
		return simpleColours[col];
	}
}

/*
 * Load new colours for a given ledFadeSegment
 * minScale/maxScale are scaling factors (0-255) for the max and min value
 */
void animLoadLedSegFadeColour(simpleCols_t col,ledSegmentFadeSetting_t* st, uint8_t minScale, uint8_t maxScale)
{
	RGB_t tmpCol=animGetColour(col);
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
	RGB_t tmpCol=animGetColour(col);
	st->r_max = (uint8_t)utilScale(tmpCol.r,255,maxScale);
	st->g_max = (uint8_t)utilScale(tmpCol.g,255,maxScale);
	st->b_max = (uint8_t)utilScale(tmpCol.b,255,maxScale);
}

/*
 * Sets up a fade from one colour to another one
 * MaxScale indicates what the max colour of each colour should be scaled by
 */
void animLoadLedSegFadeBetweenColours(simpleCols_t colFrom, simpleCols_t colTo, ledSegmentFadeSetting_t* st, uint8_t fromScale, uint8_t toScale)
{
	RGB_t from=animGetColour(colFrom);
	RGB_t to=animGetColour(colTo);
	st->r_min = (uint8_t)utilScale(from.r,255,fromScale);
	st->r_max = (uint8_t)utilScale(to.r,255,toScale);
	st->g_min = (uint8_t)utilScale(from.g,255,fromScale);
	st->g_max = (uint8_t)utilScale(to.g,255,toScale);
	st->b_min = (uint8_t)utilScale(from.b,255,fromScale);
	st->b_max = (uint8_t)utilScale(to.b,255,toScale);
	/*
	ledSegmentFadeSetting_t settingFrom;
	ledSegmentFadeSetting_t settingTo;
	//Fetch colours into two settings (it's probably easier to do it like this)
	animLoadLedSegFadeColour(colFrom,&settingFrom,0,fromScale);
	animLoadLedSegFadeColour(colTo,&settingTo,0,toScale);
	st->r_min = settingFrom.r_max;
	st->r_max = settingTo.r_max;
	st->g_min = settingFrom.g_max;
	st->g_max = settingTo.g_max;
	st->b_min = settingFrom.b_max;
	st->b_max = settingTo.b_max;*/
}

/*
 * Sets up a mode where you switch from one mode to another (soft fade between the two fade colours)
 * fs is the fade setting to fade TO (this just loads the correct colour)
 * switchAtMax indicates of the modeChange-animation shall end on min or max
 * Note that this will actually perform the fadeSet, so a new fadeset of this fadesetting with this segment should not be performed
 */
void animSetModeChange(simpleCols_t col, ledSegmentFadeSetting_t* fs, uint8_t seg, bool switchAtMax, uint8_t minScale, uint8_t maxScale)
{
	ledSegmentFadeSetting_t fsTmp;
	memcpy(&fsTmp,fs,sizeof(ledSegmentFadeSetting_t));
	//Load the setting as normal (will give us the max setting, as fade by default starts from max)
	if(col!=SIMPLE_COL_NO_CHANGE)
	{
		animLoadLedSegFadeColour(col,&fsTmp,minScale,maxScale);
	}
	//Send the change we want to have to the ledSeg, as no-one else should access and change the state
	ledSegSetModeChange(&fsTmp,seg,switchAtMax);


	//To save for the next setting:
	/*
	 * rgb_min
	 * cycles
	 * mode
	 * note that dir does NOT have to be saved, as switching between modes will always fade TO max, so the start-dir will always change to down.
	 *
	 * Usecases:
	 * ModeLoop
	 * - Efter byte är vi på max. Då vill vi börja om från min (dir = 1)
	 * - Efter byte är vi på max. Då vill vi gå mot min (dir=-1)
	 * - Efter byte är vi på min. Då vill vi gå mot max (dir=1)
	 * - Efter byte är vi på min. Då vill vi börja om från max (dir=-1)
	 * ModeBounce
	 * - Efter byte är vi på max. Då ska vi gå neråt (alltid)
	 * - Efter byte är vi på min. Då ska vi gå uppåt (alltid)
	 *
	 * Dir vid bytet indikerar om vi är på min eller max. Dir=1 indikerar max, dir=-1 indikerar min
	 */
/*
	//Get the colour of the current state to know what to move from
	ledSegment_t currentSeg;
	ledSegGetState(segment,&currentSeg);

	//At this point, we know the entire setting that we're going to go TO.
	//Now we save the settings needed:
	st->savedCycles = st->cycles;
	st->switchMode=true;
	st->savedDir = st->startDir;
	//We will fade from min to max, with dir up. We therefore save the min value and assign that to the current state.
	if(switchAtMax)
	{
		st->savedR =st->r_min;
		st->savedG =st->g_min;
		st->savedB =st->b_min;
		st->r_min = currentSeg.state.r;
		st->g_min = currentSeg.state.g;
		st->b_min = currentSeg.state.b;
		st->startDir=1;
	}
	else	//we will fade from max to min, with dir down.
	{
		st->savedR =st->r_max;
		st->savedG =st->g_max;
		st->savedB =st->b_max;
		st->r_max = currentSeg.state.r;
		st->g_max = currentSeg.state.g;
		st->b_max = currentSeg.state.b;
		st->startDir=-1;
	}
	//Cycles shall always be 1, so we know when we are done
	st->cycles=1;
	*/
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

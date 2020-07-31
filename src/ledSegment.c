/*
 *	ledSegment.c
 *
 *	Created on: Dec 29, 2017
 *		Author: Sterna
 *
 *	This file handles segmentation of LEDs. A segment is a strip with a certain number of LEDs
 *	The segment can be treated like a single strip, just smaller. It can be faded, put on loops, perform a pre-programmed pattern etc
 *	A segment may only span a single real strip. (i.e. a segment cannot begin on strip1 and end on strip2)
 *
 *	A segment is created by initing it. The information needed is basically a range in a strip (say strip1, pixel 30 to 50).
 *	It will return a number, used to reference to this strip. Using this number, various things can be programmed per strip.
 *	Segment numbers are counted from 0.
 *
 *	A segment has two settings, working in unison: fade and pulse. If one is not given (it does not have a segment number), that one is ignored
 *	The pulse (if given) will always supersede the fade.
 *	Pulse:
 *	A pulse is defined as a number of LEDs at max power, surrounded by a number of LEDs before and after with decreasing intensity, from max to min, per colour
 *	The pulse has an LED propagation speed. It consists of the number of cycles between each update, and the number of pixels the pulse shall move
 *	Pulse supports three modes: bounce and loop.
 *		In Loop mode, the pulse starts over from the first (or last) LED in the segment
 *		In Loop_end mode, the pulse behaves the same as in Loop mode, but the whole pulse will disappear before re-appearing
 *		In Bounce mode, the pulse will travel to the end of the segment, and then back to beginning, and then back to the end and loop like that
 *	Cycles defines the number of loop/bounce cycles. A complete bounce (first->last->first) is considered to be two cycles
 *	A pulse can start at a specified LED and have a specified direction. This direction will change in bounce mode, but persist in loop mode.
 *	Fade:
 *	A fade is defined as a max and min value per colour, between which the whole segment will fade in a certain time.
 *	Fade supports two modes: bounce and loop.
 *		In Loop mode, the intensity goes only in one direction, jumping immediately to the other extreme when done
 *		In Bounce mode, the intensity goes back and forth between min and max
 *		Loop_end mode is not really supported for fade, and will do the same thing as Loop mode
 *	Fade can also have the colToFrom-flag. This is used when wanting to fade between two different colours. Min is the from colour, and Max is the To colour
 *	In bounce mode, the fade is back an forth between the two colours
 *
 *	Note on some animation tricks
 *	- Fill a segment with a pulse:
 *		Init a segment with a pulse and no fade. Use LOOP_END mode and 1 cycle
 *	- Use only pulse on a segment:
 *		Set max of all colours of fade=0
 *
 *
 *	New mode: Glitter mode. Runs instead of a pulse. Will be painted on a fade, without any regard to the fade setting.
 *	Instead of a wandering pulse, it will light up a number of LEDs in random places in the whole strip, each specified cycle.
 *
 *	Glitter has a couple of main settings different from the standard settings to use:
 *		- RGBmax (set by pulse RGBMax). This describes the end colour for the glitter points. Glitter points always start from RGB=000;
 *		- Number of persistent glitter points (set by pulse ledsMaxPower). These are the number of saved glitter points.
 *		- Number of glitter points to be updated at the time (set by pulse pixelsPerIteration). These are the number of glitter points being faded from 0 to max. This is called the "glitter subset"
 *		- (This means that the total number of glitter points are ledsMaxPower+ledsFadeBefore)
 *		- Glitter fade time (set by pixelTime in ms). The time it will take for all fade LEDs (that is, all persistant LEDs) to reach max. It is also the cycle time.
 *			This means that each glitter subset will take pixelTime*pixelsPerIteration/ledsMaxpower
 *			pixelTime should be a multiple of LEDSEG_UPDATE_PERIOD_TIME
 *	Apart from these, the following settings are also used:
 *		- Mode. Sets which mode we're using (like before)
 *		- Start dir. (Will be added later. Set by startDir). The direction to start with
 *		- Number of cycles (set by cycles). One cycle is defined as when all ledsMaxPower have been lit.
 *		- GlobalSetting. Same as before
 *	The other settings are not used and can be omitted.
 *
 *
 *	Glitter can use the following modes. All modes light up points according to the settings until it reaches max. The mode then decides what happens:
 *		Loop: At max, it puts all those points out and restarts from 0.
 *		Loop_end: At max, it stops, persisting all lit points.
 *		Loop_persist (new mode): At max, it adds new LEDs every cycle, replacing the oldest ones.
 *		Bounce (implemented because it seems annoying): Like normal bounce, but works with adding/removing LEDs as the direction.
 *
 *
 *	Glitter - each cycle (with info needed)
 *		Count down cyclesToPulseMove. If trigger cycle:
 *		Update fade colour. If fade is done (>=RGBMax), reset colour state, add new
 *		Start in the ringbuffer from the currentLed (which is index0+pixelsPerIteration). Set all LEDs from this index until end of ringbuffer to maxRGB
 *
 *
 */

#include "ledSegment.h"
#include "stdlib.h"
#include "advancedAnimations.h"

//-----------Internal variables--------//
//Contains all information for all virtual LED segments
static ledSegment_t segments[LEDSEG_MAX_SEGMENTS];
//The number of initialized segments
static uint8_t currentNofSegments=0;

//---------------Internal functions------------//
static void fadeCalcColour(uint8_t seg);
static uint8_t pulseCalcColourPerLed(ledSegmentState_t* st,uint16_t led, colour_t col);
static void pulseCalcAndSet(uint8_t seg);
static bool checkCycleCounter(uint32_t* cycle);
static bool checkCycleCounterU16(uint16_t* cycle);
static bool isGlitterMode(ledSegmentMode_t mode);
static bool checkSyncReadyFade(uint8_t syncGrp, uint8_t seg);
static bool checkFadeNotWaitingForSyncAll(uint8_t syncGrp);
static void resetSyncDoneGroup(uint8_t syncGrp);
static bool ledIsWithinSeg(uint8_t seg, uint16_t led);
static bool isExcludedFromAll(uint8_t seg);


/*
 * Inits an LED segment
 * Will return a value larger than LEDSEG_MAX_SEGMENTS if there is no more room for segments or any other error
 * Note that it's possible to register a segment over another segment. There are no checks for this
 */
uint8_t ledSegInitSegment(uint8_t strip, uint16_t start, uint16_t stop, bool invertPulse, bool excludeFromAll, ledSegmentPulseSetting_t* pulse, ledSegmentFadeSetting_t* fade)
{
	if(currentNofSegments>=LEDSEG_MAX_SEGMENTS || start>stop)
	{
		return (LEDSEG_MAX_SEGMENTS+1);
	}
	if(!apa102IsValidPixel(strip,start) || !apa102IsValidPixel(strip,stop))
	{
		return (LEDSEG_MAX_SEGMENTS+1);
	}
	//Make it slighly faster to write code for
	ledSegment_t* sg=&segments[currentNofSegments];
	//Load settings into state
	sg->strip=strip;
	sg->start=start;
	sg->stop=stop;
	sg->invertPulse=invertPulse;
	sg->excludeFromAll=excludeFromAll;

	currentNofSegments++;
	if(!ledSegSetFade(currentNofSegments-1,fade))
	{
		sg->state.fadeActive=false;
	}
	if(!ledSegSetPulse(currentNofSegments-1,pulse))
	{
		sg->state.pulseActive=false;
	}
	return (currentNofSegments-1);
}

/*
 * Get the state and all info for a specific led segment
 * seg is the number of the segment (given from initSegment)
 * state is where the state will be copied (you will not have access to the internal state variable)
 * Returns false if the segment does not exist
 */
bool ledSegGetState(uint8_t seg, ledSegment_t* state)
{
	if(ledSegExists(seg))
	{
		memcpy(state,&segments[seg],sizeof(ledSegment_t));
		return true;
	}
	return false;
}

/*
 * Returns true if a led segment exists. Includes LEDSEG_ALL
 */
bool ledSegExists(uint8_t seg)
{
	if(seg<currentNofSegments || seg==LEDSEG_ALL)
	{
		return true;
	}
	return false;
}

/*
 * Returns true if a led segment exists.
 */
bool ledSegExistsNotAll(uint8_t seg)
{
	if(seg<currentNofSegments)
	{
		return true;
	}
	return false;
}

/*
 * Setup the fade for a segment
 * seg is the segment given by the init function
 * fs is a pointer to the wanted setting
 * Will reset the current fade cycle
 */
bool ledSegSetFade(uint8_t seg, ledSegmentFadeSetting_t* fs)
{
	if(!ledSegExists(seg) || fs==NULL)
	{
		return false;
	}
	if(seg==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<currentNofSegments;i++)
		{
			if(!isExcludedFromAll(i))
			{
				ledSegSetFade(i,fs);
			}
		}
		return true;
	}
	//Create some temp variables that are easier to use
	ledSegment_t* sg;
	sg=&(segments[seg]);
	ledSegmentState_t* st;
	st=&(sg->state);
	ledSegmentFadeSetting_t* fd;
	fd=&(sg->state.confFade);
	//Copy new setting into state
	memcpy(fd,fs,sizeof(ledSegmentFadeSetting_t));
	//Setup fade parameters
	uint16_t periodMultiplier=1;
	bool makeItSlower=false;
	//The total number update periods we have to achieve the fade time Todo: consider adding a limit if a fade is very small (such as less than 10 steps)
	uint32_t master_steps=0;
	const uint8_t largestError=50;
	do
	{
		makeItSlower=false;
		master_steps=fs->fadeTime/(LEDSEG_UPDATE_PERIOD_TIME*periodMultiplier);
		//Calculate number of steps needed to increase the colour per update period. If any value is too small, we need to go to a slower period
		uint8_t r_diff= abs(fs->r_max-fs->r_min);
		uint8_t g_diff= abs(fs->g_max-fs->g_min);
		uint8_t b_diff= abs(fs->b_max-fs->b_min);
		st->r_rate = r_diff/master_steps;
		if(r_diff!=0 && (st->r_rate<1 || ((r_diff%master_steps)>largestError)))
		{
			makeItSlower=true;
		}
		st->g_rate = g_diff/master_steps;
		if(g_diff!=0 && (st->g_rate<1 || ((g_diff%master_steps)>largestError)))
		{
			makeItSlower=true;
		}
		st->b_rate = b_diff/master_steps;
		if(b_diff!=0 && (st->b_rate<1 || ((b_diff%master_steps)>largestError)))
		{
			makeItSlower=true;
		}
		if(makeItSlower)
		{
			periodMultiplier++;
		}
	}
	while(makeItSlower);
	fd->fadePeriodMultiplier = periodMultiplier;
	st->cyclesToFadeChange = periodMultiplier;
	//If the start dir is down, start from max
	if(fs->startDir ==-1)
	{
		st->r=fs->r_max;
		st->g=fs->g_max;
		st->b=fs->b_max;
	}
	else
	{
		st->r=fs->r_min;
		st->g=fs->g_min;
		st->b=fs->b_min;
	}
	st->fadeDir = fs->startDir;
	//Check if user wants a very large number of cycles. If so, mark this as run indefinitely
	if(fs->cycles==0 || (UINT32_MAX/fs->cycles)<master_steps)
	{
		st->confFade.cycles=0;
	}
	else
	{
		st->confFade.cycles=fs->cycles;//*master_steps;	//Each cycle shall be one half cycle (min->max)
	}
	st->fadeCycle=st->confFade.cycles;
	//If the global setting is not used (set to 0) the default global will be loaded dynamically from the current global
	if(fd->globalSetting == 0)
	{
		//fd->globalSetting = APA_MAX_GLOBAL_SETTING+1;
	}
	st->fadeActive = true;
	st->fadeState=LEDSEG_FADE_NOT_DONE;

	return true;
}

/*
 *	Setup a pulse for an LED segment
 *	Will reset the current pulse cycle
 */
bool ledSegSetPulse(uint8_t seg, ledSegmentPulseSetting_t* ps)
{
	if(!ledSegExists(seg) || ps==NULL)
	{
		return false;
	}
	if(seg==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<currentNofSegments;i++)
		{
			if(!isExcludedFromAll(i))
			{
				ledSegSetPulse(i,ps);
			}
		}
		return true;
	}
	//Create some temp variables that are easier to use
	ledSegment_t* sg;
	sg=&(segments[seg]);
	ledSegmentState_t* st;
	st=&(sg->state);
	ledSegmentPulseSetting_t* pu;
	pu=&(sg->state.confPulse);
	//Copy new setting into state
	memcpy(pu,ps,sizeof(ledSegmentPulseSetting_t));

	st->pulseCycle=ps->cycles;
	st->pulseActive = true;
	if(isGlitterMode(pu->mode))
	{
		//Allocate memory for the ring buffer.
		free(st->glitterActiveLeds);	//Remove old buffer
		st->glitterActiveLeds = (uint16_t*)calloc(pu->ledsMaxPower+pu->pixelsPerIteration,sizeof(uint16_t));	//Allocate new buffer
		st->currentLed=0;	//currentLed is used as index in the ringbuffer.

		//For glitter mode, pixelTime setting is the total time for fade of all the glitter pixels together.
		//Therefore, we calculate the number of LEDSEG_UPDATE_PERIOD_TIME-cycles is needed for each glitter subsegment
		//(GCC will probably optimize this)
		uint32_t pixelTimeTemp=0;
		pixelTimeTemp=pu->pixelTime/LEDSEG_UPDATE_PERIOD_TIME;	//Total number of update periods for all glitter points (until the whole cycle is done)
		pixelTimeTemp=pixelTimeTemp*pu->pixelsPerIteration/pu->ledsMaxPower;	//The time it will take for each cycle to fade completely from 0 to max
		if(pixelTimeTemp==0)
		{
			pixelTimeTemp=1;
		}
		pu->pixelTime=pixelTimeTemp;
		st->cyclesToPulseMove=1;	//So that we get LEDs from the beginning
	}
	else
	{
		if(sg->invertPulse)
		{
			pu->startLed = sg->stop-pu->startLed+1;
			pu->startDir *= -1;
		}
		else
		{
			pu->startLed = sg->start + pu->startLed-1;
		}
		if(pu->startLed>sg->stop)
		{
			pu->startLed=sg->stop;
		}
		else if(pu->startLed < sg->start)
		{
			pu->startLed=sg->start;
		}
		st->currentLed = pu->startLed;
		st->cyclesToPulseMove = pu->pixelTime;
	}
	st->pulseDir=pu->startDir;

	//If the global setting is not used (set to 0) the default global will be loaded dynamically from the current global
	if(pu->globalSetting == 0)
	{
		//pu->globalSetting = APA_MAX_GLOBAL_SETTING+1;
	}
	st->pulseDone=false;
	st->pulseActive=true;
	return true;
}

/*
 * Sets the fade colour to 0
 */
bool ledSegClearFade(uint8_t seg)
{
	if(!ledSegExists(seg))
	{
		return false;
	}
	if(seg==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<currentNofSegments;i++)
		{
			if(!isExcludedFromAll(i))
			{
				ledSegClearFade(i);
			}
		}
		return true;
	}
	ledSegment_t st;

	ledSegGetState(seg,&st);
	st.state.confFade.r_min=0;
	st.state.confFade.r_max=0;
	st.state.confFade.g_min=0;
	st.state.confFade.g_max=0;
	st.state.confFade.b_min=0;
	st.state.confFade.b_max=0;
	return ledSegSetFade(seg,&(st.state.confFade));
}

/*
 * Sets the pulse colour to 0
 */
bool ledSegClearPulse(uint8_t seg)
{
	if(!ledSegExists(seg))
	{
		return false;
	}
	if(seg==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<currentNofSegments;i++)
		{
			if(!isExcludedFromAll(i))
			{
				ledSegClearPulse(i);
			}
		}
		return true;
	}
	ledSegment_t st;

	ledSegGetState(seg,&st);
	st.state.confPulse.r_max=0;
	st.state.confPulse.g_max=0;
	st.state.confPulse.b_max=0;
	return ledSegSetPulse(seg,&(st.state.confPulse));
}

/*
 * Sets the mode for a pulse
 * Will take effect immediately
 */
bool ledSegSetFadeMode(uint8_t seg, ledSegmentMode_t mode)
{
	if(!ledSegExists(seg))
	{
		return false;
	}
	if(seg==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<currentNofSegments;i++)
		{
			if(!isExcludedFromAll(i))
			{
				ledSegSetFadeMode(i,mode);
			}
		}
		return true;
	}
	segments[seg].state.confFade.mode=mode;
	return true;
}

/*
 * Sets the mode for a pulse
 * Will take effect immediately
 */
bool ledSegSetPulseMode(uint8_t seg, ledSegmentMode_t mode)
{
	if(!ledSegExists(seg))
	{
		return false;
	}
	if(seg==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<currentNofSegments;i++)
		{
			if(!isExcludedFromAll(i))
			{
				ledSegSetPulseMode(i,mode);
			}
		}
		return true;
	}
	segments[seg].state.confPulse.mode=mode;
	return true;
}

/*
 * Sets a single LED within a segment to a colour
 * The LED is counted from the first LED in the segment (if LED=1, the start will be set)
 * If the LED is out of bounds for the strip, the function will return false
 * Will be overriden by any fade or pulse setting
 */
bool ledSegSetLed(uint8_t seg, uint16_t led, uint8_t r, uint8_t g, uint8_t b)
{
	return ledSegSetLedWithGlobal(seg,led,r,g,b,0);
}

/*
 * Sets a single LED within a segment to a colour, including the global setting
 * The LED is counted from the first LED in the segment (if LED=1, the first LED will be set)
 * If the LED is out of bounds for the strip, the function will return false
 * Will be overriden by any fade or pulse setting
 */
bool ledSegSetLedWithGlobal(uint8_t seg, uint16_t led, uint8_t r, uint8_t g, uint8_t b,uint8_t global)
{
	if(!ledIsWithinSeg(seg,led))
	{
		return false;
	}
	uint16_t tmp=0;
	if(segments[seg].invertPulse)
	{
		tmp=segments[seg].stop-led+1;
	}
	else
	{
		tmp=segments[seg].start+led-1;
	}
	apa102SetPixelWithGlobal(segments[seg].strip,tmp,r,g,b,global,true);
	return true;
}

/*
 * Sets a range of LEDs within a segment to the same colour
 * Start and stop are counted from the first LED in the segment (if LED=1, the first LED will be set)
 * If the LED is out of bounds for the strip, the function will return false
 * Will be overriden by any fade or pulse setting
 */
bool ledSegSetRange(uint8_t seg, uint16_t start, uint16_t stop,uint8_t r,uint8_t g,uint8_t b)
{
	return ledSegSetRangeWithGlobal(seg,start,stop,r,g,b,0);
}

/*
 * Sets a range of LEDs within a segment to the same colour, including the global setting
 * Start and stop are counted from the first LED in the segment (if LED=1, the first LED will be set)
 * If the LED is out of bounds for the strip, the function will return false
 * Will be overriden by any fade or pulse setting
 */
bool ledSegSetRangeWithGlobal(uint8_t seg, uint16_t start, uint16_t stop,uint8_t r,uint8_t g,uint8_t b,uint8_t global)
{
	if(start>stop || !ledIsWithinSeg(seg,start) || !ledIsWithinSeg(seg,stop))
	{
		return false;
	}
	apa102FillRange(segments[seg].strip,segments[seg].start+start-1,segments[seg].start+stop-1,r,g,b,global);
	return true;
}

/*
 * Sets the pulse active state to a new value. Useful for pausing an animation
 */
bool ledSegSetPulseActiveState(uint8_t seg, bool state)
{
	if(!ledSegExists(seg))
	{
		return false;
	}
	if(seg==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<currentNofSegments;i++)
		{
			if(!isExcludedFromAll(i))
			{
				ledSegSetPulseActiveState(i,state);
			}
		}
		return true;
	}
	segments[seg].state.pulseActive=state;
	return true;
}

/*
 * Gets the status of a pulse animation
 */
bool ledSegGetPulseActiveState(uint8_t seg)
{
	if(!ledSegExists(seg))
	{
		return false;
	}
	if(seg==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<currentNofSegments;i++)
		{
			if(!isExcludedFromAll(i) && !ledSegGetPulseActiveState(i))
			{
				return false;
			}
		}
		return true;
	}
	return segments[seg].state.pulseActive;
}

/*
 * Sets the fade active state to a new value. Useful for pausing an animation
 */
bool ledSegSetFadeActiveState(uint8_t seg, bool state)
{
	if(!ledSegExists(seg))
	{
		return false;
	}
	if(seg==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<currentNofSegments;i++)
		{
			if(!isExcludedFromAll(i))
			{
				ledSegSetFadeActiveState(i,state);
			}
		}
		return true;
	}
	segments[seg].state.fadeActive=state;
	return true;
}

/*
 * Gets the status os a fade animation
 */
bool ledSegGetFadeActiveState(uint8_t seg)
{
	if(!ledSegExists(seg))
	{
		return false;
	}
	if(seg==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<currentNofSegments;i++)
		{
			if(!isExcludedFromAll(i) && !ledSegGetFadeActiveState(i))
			{
				return false;
			}
		}
		return true;
	}
	return segments[seg].state.fadeActive;
}

/*
 * Returns true if the set fade animation is done
 */
bool ledSegGetFadeDone(uint8_t seg)
{
	if(!ledSegExists(seg))
	{
		return false;
	}
	if(seg==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<currentNofSegments;i++)
		{
			if(!isExcludedFromAll(i) && !ledSegGetFadeDone(i))
			{
				return false;
			}
		}
		return true;
	}
	return (segments[seg].state.fadeState==LEDSEG_FADE_DONE);
}

/*
 * Returns true if the set fade animation switch is done
 * If LEDSEG_ALL is given, it will only report true if all segments are done
 */
bool ledSegGetFadeSwitchDone(uint8_t seg)
{
	if(!ledSegExists(seg))
	{
		return false;
	}
	if(seg==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<currentNofSegments;i++)
		{
			if(!isExcludedFromAll(i) && !ledSegGetFadeSwitchDone(i))
			{
				return false;
			}
		}
		return true;
	}
	return (!segments[seg].state.switchMode);
}

/*
 * Returns the sync group a segment is part of (will return 0 if not part of any sync group)
 */
uint8_t ledSegGetSyncGroup(uint8_t seg)
{
	if(!ledSegExistsNotAll(seg))
	{
		return 0;
	}
	return segments[seg].state.confFade.syncGroup;
}

/*
 * Checks if all fades within
 * If the segment is not part of any sync group (if it's ==0) it's considered done
 */
bool ledSegGetSyncGroupDone(uint8_t syncGrp)
{
	if(!syncGrp)
	{
		return true;
	}
	ledSegmentState_t* st;
	for(uint8_t i=0;i<currentNofSegments;i++)
	{
		st=&(segments[i].state);
		if(st->confFade.syncGroup == syncGrp && st->fadeState !=LEDSEG_FADE_DONE)
		{
			return false;
		}
	}
	return true;
}

/*
 * Returns true if the set pulse animation is done
 */
bool ledSegGetPulseDone(uint8_t seg)
{
	if(!ledSegExists(seg))
	{
		return false;
	}
	if(seg==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<currentNofSegments;i++)
		{
			if(!isExcludedFromAll(i) && !ledSegGetPulseDone(i))
			{
				return false;
			}
		}
		return true;
	}
	return segments[seg].state.pulseDone;
}

/*
 * The the pulse speed by setting the pixel time and the pixelsPerIteration
 * If value is 0, the existing value is used
 * This will take effect immediately
 */
bool ledSegSetPulseSpeed(uint8_t seg, uint16_t time, uint16_t ppi)
{
	if(!ledSegExists(seg))
	{
		return false;
	}
	if(seg==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<currentNofSegments;i++)
		{
			if(!isExcludedFromAll(i))
			{
				ledSegSetPulseSpeed(i,time,ppi);
			}
		}
		return true;
	}
	if(time)
	{
		segments[seg].state.confPulse.pixelTime=time;
	}
	if(ppi)
	{
		segments[seg].state.confPulse.pixelsPerIteration=ppi;
	}
	return true;
}

/*
 * Restart the fade, pulse or both. All other settings are retained
 */
bool ledSegRestart(uint8_t seg, bool restartFade, bool restartPulse)
{
	if(!ledSegExists(seg))
	{
		return false;
	}
	if(seg==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<currentNofSegments;i++)
		{
			if(!isExcludedFromAll(i))
			{
				ledSegRestart(i,restartFade,restartPulse);
			}
		}
		return true;
	}
	ledSegmentState_t* st=&segments[seg].state;
	if(restartFade)
	{
		if(st->confFade.startDir == 1)
		{
			st->r = st->confFade.r_min;
			st->g = st->confFade.g_min;
			st->b = st->confFade.b_min;
			st->fadeDir = 1;
		}
		else
		{
			st->r = st->confFade.r_max;
			st->g = st->confFade.g_max;
			st->b = st->confFade.b_max;
			st->fadeDir = -1;
		}
		st->fadeState=LEDSEG_FADE_NOT_DONE;
		st->fadeCycle=st->confFade.cycles;
		st->fadeActive=true;
	}
	if(restartPulse)
	{
		st->pulseDir = st->confPulse.startDir;
		st->currentLed = st->confPulse.startLed;
		st->pulseCycle=st->confPulse.cycles;
		if(isGlitterMode(st->confPulse.mode))
		{
			//Todo: Add handling for bounce
			st->currentLed=st->confPulse.startLed;
		}
		st->pulseActive=true;
		st->pulseDone=false;
	}
	return true;
}

/*
 * Sets a new global setting for the current segment
 * Setting global to 0 will use the set default in the library
 */
bool ledSegSetGlobal(uint8_t seg, uint8_t fadeGlobal, uint8_t pulseGlobal)
{
	if(!ledSegExists(seg))
	{
		return false;
	}
	if(seg==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<currentNofSegments;i++)
		{
			if(!isExcludedFromAll(i))
			{
				ledSegSetGlobal(i,fadeGlobal,pulseGlobal);
			}
		}
		return true;
	}
	segments[seg].state.confFade.globalSetting=fadeGlobal;
	segments[seg].state.confPulse.globalSetting=pulseGlobal;
	return true;
}


/*
 * Sets up a mode where you switch from one mode to another (soft fade between the two fade colours)
 * st is the fade setting to fade TO (this just loads the correct colour)
 * switchAtMax indicates of the modeChange-animation shall end on min or max
 */
void ledSegSetModeChange(ledSegmentFadeSetting_t* fs, uint8_t seg, bool switchAtMax)
{
	if(!ledSegExists(seg))
	{
		return;
	}
	if(seg==LEDSEG_ALL)
	{
		for(uint8_t i=0;i<currentNofSegments;i++)
		{
			if(!isExcludedFromAll(i))
			{
				ledSegSetModeChange(fs,i,switchAtMax);
			}
		}
		return;
	}

	//Get the colour of the current state to know what to move from
	ledSegmentState_t* st = &(segments[seg].state);
	ledSegmentFadeSetting_t fsTmp;
	memcpy(&fsTmp,fs,sizeof(ledSegmentFadeSetting_t));
	//At this point, we know the entire setting that we're going to go TO.
	//Now we save the settings needed:
	st->savedCycles = fs->cycles;
	st->switchMode=true;
	st->savedDir = fs->startDir;
	//We will fade from min to max, with dir up. We therefore save the min value and assign that to the current state.
	if(switchAtMax)
	{
		st->savedR =fs->r_min;
		st->savedG =fs->g_min;
		st->savedB =fs->b_min;
		fsTmp.r_min = st->r;
		fsTmp.g_min = st->g;
		fsTmp.b_min = st->b;
		fsTmp.startDir=1;
	}
	else	//we will fade from max to min, with dir down.
	{
		st->savedR =fs->r_max;
		st->savedG =fs->g_max;
		st->savedB =fs->b_max;
		fsTmp.r_max = st->r;
		fsTmp.g_max = st->g;
		fsTmp.b_max = st->b;
		fsTmp.startDir=-1;
	}
	//Cycles shall always be 1, so we know when we are done
	fsTmp.cycles=1;
	ledSegSetFade(seg,&fsTmp);
}

/*
 * The great big update function. This should be run as often as possible (not from interrupts!)
 * It keeps its own time gate, and will from time to time create a heavy load
 *
 * It uses the following scheme:
 * The whole LED-system (all strips) is updated every LEDSEG_UPDATE_PERIOD_TIME.
 * It has several calculations cycles per period (configurable in ledSegment.h). Each of these iterations, a calculation is performed
 * Each calculation cycle will calculate a fraction of the total number of segments and load this into the big pixel array
 * Note: no calculations are done while the strips are updating (the DMA is running).
 *	 Otherwise, we would need to keep two copies of the whole pixel map, costing about 3kB of additional RAM
 *
 *	 Add an option in fadeSetting or a state in fadeState.
 *	 The fade state or conf will need to remember some parts of the fade setting (the min colours, that it's fade, and the number of cycles)
 *	 Set up the fade as normal, but create that special setting that allows fade between two colours and one cycle.
 *	 Once fadeDone is true, switch the fade to the fade that's it supposed to be and switch to that
 *
 */
void ledSegRunIteration()
{
	static uint32_t nextCallTime=0;
	static uint8_t calcCycle=0;
	static uint8_t currentSeg=0;

	//Temporary variables
	uint8_t stopSegment=0;
	uint16_t start=0;
	uint16_t stop=0;
	uint8_t strip=0;
	ledSegmentState_t* st;
	//These two are to measure the time the calculation takes
	volatile uint32_t startVal=0;
	volatile uint32_t timeTaken=0;

	if(systemTime>nextCallTime && !apa102DMABusy(APA_ALL_STRIPS))
	{
		calcCycle++;
		nextCallTime=systemTime+LEDSEG_UPDATE_PERIOD_TIME/LEDSEG_CALCULATION_CYCLES;
		//Calculate the number of segments to calculate this cycle (always try to calculate one segment, even though it doesn't exist)
		stopSegment=currentSeg+currentNofSegments/LEDSEG_CALCULATION_CYCLES+1;
		//Calculate all active segments for this cycle
		while(ledSegExists(currentSeg) && currentSeg<stopSegment)
		{
			startVal=microSeconds();
			//Extract useful variables from the state
			st=&(segments[currentSeg].state);
			start=segments[currentSeg].start;
			stop=segments[currentSeg].stop;
			strip=segments[currentSeg].strip;

			//Calculate and write fill colour to internal buffer
			if(st->fadeActive)
			{
				if(checkCycleCounterU16(&st->cyclesToFadeChange))
				{
					fadeCalcColour(currentSeg);
					st->cyclesToFadeChange = st->confFade.fadePeriodMultiplier;
				}
				//It will most likely take longer time to calculate which LEDs should not be filled,
				//rather than just filling them and overwriting them. Writing a single pixel with force does not take very long time
				apa102FillRange(strip,start,stop,st->r,st->g,st->b,st->confFade.globalSetting);
			}
			//Calculate and write pulse to internal LED buffer. Will overwrite the fade colour
			if(st->pulseActive)
			{
				pulseCalcAndSet(currentSeg);
			}
			timeTaken=microSeconds()-startVal;
			currentSeg++;
		}
		//Update calculation cycle and check if we should update the physical strip
		if(calcCycle>=LEDSEG_CALCULATION_CYCLES)
		{
			//Update LEDs and restart calc cycle
			apa102UpdateStrip(APA_ALL_STRIPS);
			calcCycle=0;
			currentSeg=0;
		}
	}
}


//-------------Internal functions------------------------//

/*
 * Calculate the colour of a led faded in a pulse
 * led is the led within the pulse, counted from currentLed (the first LED with a colour).
 * led is indexed from reality (meaning currentLed has value 1, and that the lowest value is 1)
 */
static uint8_t pulseCalcColourPerLed(ledSegmentState_t* st,uint16_t led, colour_t col)
{
	typedef enum
	{
		PULSE_BEFORE=0,
		PULSE_MAX,
		PULSE_AFTER
	}pulsePart_t;
	ledSegmentPulseSetting_t* ps;
	ps=&(st->confPulse);
	RGB_t RGBMaxTmp;

	if(ps->colourSeqNum)
	{
		uint8_t tmp=1;
		if(ps->colourSeqLoops)
		{
			tmp=ps->colourSeqLoops;
		}
		uint16_t ledsPerColour=(ps->ledsFadeBefore+ps->ledsMaxPower+ps->ledsFadeAfter)/(tmp*ps->colourSeqNum);
		//If the pulse is shorter than PRIDE_COL_NOF_COLOURS, this will not work well
		if(ledsPerColour<1)
		{
			ledsPerColour=1;
		}
		uint16_t colIndex=((led-1)/ledsPerColour)%ps->colourSeqNum;
		RGBMaxTmp=animGetColourFromSequence(ps->colourSeqPtr,colIndex,255);//animGetColourPride((prideCols_t)colIndex,255);
	}
	else
	{
		RGBMaxTmp.r=ps->r_max;
		RGBMaxTmp.g=ps->g_max;
		RGBMaxTmp.b=ps->b_max;
	}
	uint8_t tmpCol=0;
	uint8_t tmpMax=0;
	uint8_t tmpMin=0;
	pulsePart_t part=PULSE_BEFORE;
	if(led<=ps->ledsFadeBefore)
	{
		part=PULSE_BEFORE;
	}
	else if(led<=(ps->ledsFadeBefore+ps->ledsMaxPower))
	{
		part=PULSE_MAX;
	}
	else
	{
		part=PULSE_AFTER;
	}
	switch(col)
	{
		case COL_RED:
//			tmpMax=ps->r_max;
			tmpMax=RGBMaxTmp.r;
			tmpMin=st->r;
		break;
		case COL_GREEN:
//			tmpMax=ps->g_max;
			tmpMax=RGBMaxTmp.g;
			tmpMin=st->g;
			break;
		case COL_BLUE:
//			tmpMax=ps->b_max;
			tmpMax=RGBMaxTmp.b;
			tmpMin=st->b;
			break;
	}
	switch(part)
	{
		case PULSE_MAX:
			tmpCol=tmpMax;
		break;
		case PULSE_BEFORE:
			tmpCol=tmpMin+(led)*(tmpMax-tmpMin)/ps->ledsFadeBefore;
		break;
		case PULSE_AFTER:
			tmpCol=tmpMax-(led-ps->ledsFadeBefore-ps->ledsMaxPower-1)*(tmpMax-tmpMin)/ps->ledsFadeAfter;
		break;
	}
	return tmpCol;
}

/*
 * Calculate and set the LEDs for a pulse
 */
static void pulseCalcAndSet(uint8_t seg)
{
	uint16_t start=0;
	uint16_t stop=0;
	uint8_t strip=0;

	ledSegmentPulseSetting_t* ps;
	ledSegmentState_t* st;
	int8_t tmpDir=1;
	uint16_t pulseLength=0;

	uint8_t tmpR=0;
	uint8_t tmpG=0;
	uint8_t tmpB=0;
	volatile uint8_t tmpSeg=seg;
	if(!ledSegExists(seg))
	{
		return;
	}
	//Extract useful information
	st=&(segments[seg].state);
	ps=&(st->confPulse);
	start=segments[seg].start;
	stop=segments[seg].stop;
	strip=segments[seg].strip;
	pulseLength=ps->ledsFadeAfter+ps->ledsFadeBefore+ps->ledsMaxPower;
	uint16_t glitterTotal=ps->ledsMaxPower+ps->pixelsPerIteration;
	bool glitterJustGenerated=false;
	//Move LED and update direction
	//Check if it's time to move a pixel
	/*
	 * Todo: Make a proper cycle counter for modes that are not glitter modes
	 * Vid ett cykelslut (currentLed overflows i kör-riktningen), räkna då ner cycle counter med ett steg
	 * Om cycle counter blev 0, sätt den i läget outOfCycles (använd st->pulseUpdatedCycle). Det här läget fungerar overrides ps.mode och funkar likadant som LEDSEG_MODE_LOOP_END
	 */
	if(checkCycleCounterU16(&st->cyclesToPulseMove) && !st->pulseDone)
	{
		if(ps->mode == LEDSEG_MODE_LOOP_END || st->pulseUpdatedCycle)
		{
			if(utilValueWillOverflow(st->currentLed,ps->pixelsPerIteration*st->pulseDir,start,stop))
			{
				if(checkCycleCounter(&st->pulseCycle))
				{
					st->pulseUpdatedCycle=true;
				}
			}
			st->currentLed =st->currentLed+ps->pixelsPerIteration*st->pulseDir;

			int32_t tmpLed= st->currentLed;
			if(st->pulseDir==1 && (tmpLed>=(pulseLength+stop)))
			{
				if(st->pulseUpdatedCycle)
				{
					st->pulseDone = true;
					st->pulseActive = false;
					st->pulseUpdatedCycle=false;
				}
				else
				{
					st->currentLed = start;
				}
			}
			else if(st->pulseDir==-1 && (tmpLed<=(int32_t)(start-pulseLength)))
			{
				if(st->pulseUpdatedCycle)
				{
					st->pulseDone = true;
					st->pulseActive = false;
					st->pulseUpdatedCycle=false;
				}
				else
				{
					st->currentLed = stop;
				}
			}
		}
		else if(ps->mode == LEDSEG_MODE_BOUNCE)
		{
			st->currentLed = utilBounceValue(st->currentLed,ps->pixelsPerIteration*st->pulseDir,start,stop,&tmpDir);
			if(st->pulseDir!=tmpDir)
			{
				if(checkCycleCounter(&st->pulseCycle))
				{
					st->pulseUpdatedCycle=true;
				}
				else
				{
					st->pulseDir=tmpDir;
				}
			}
		}
		else if(ps->mode == LEDSEG_MODE_LOOP)
		{
			if(utilValueWillOverflow(st->currentLed,ps->pixelsPerIteration*st->pulseDir,start,stop))
			{
				if(checkCycleCounter(&st->pulseCycle))
				{
					st->pulseUpdatedCycle=true;
				}
			}
			if(!st->pulseUpdatedCycle)
			{
				st->currentLed = utilLoopValue(st->currentLed,ps->pixelsPerIteration*st->pulseDir,start,stop);
			}
		}
		else if(isGlitterMode(ps->mode))
		{
			//If fade is not active, make sure to clear all LEDs (which is what the fade would have done)
			if(!st->fadeActive)
			{
				for(uint16_t i=0;i<glitterTotal;i++)
				{
					ledSegSetLedWithGlobal(seg,st->glitterActiveLeds[i],0,0,0,ps->globalSetting);
				}
			}
			//For glitter mode, we will generate new LEDs now
			//Here, we also check what we need to do based on mode.
			if(st->currentLed>=glitterTotal)
			{
				/*
				 *
				 * When is a cycle finished? We get here when we have already added all glitter LEDs
				 *
				 * Loop - go to glitter total, restart from 0 -> Done when all glitter LEDs are lit, which is currentLed>=glitter total
				 * Loop end - Same as Loop, but doesn't restart. Mark as done at the same time.
				 * Loop persist - First cycle is done when all LEDs are lit (currentLed>=glitterTotal). Each subsequent cycle happens when we have finished lightning up to all LEDs again
				 * Bounce - Whenever we change direction.
				 */

				if(ps->mode==LEDSEG_MODE_GLITTER_LOOP)
				{
					memset(st->glitterActiveLeds,0,glitterTotal*sizeof(uint16_t));
					st->currentLed=0;
				}
			}
			//This will handle LEDSEG_MODE_GLITTER_LOOP_END (and also other unforeseen things)
			if(st->currentLed<glitterTotal)
			{
				for(uint16_t i=0;i<ps->pixelsPerIteration;i++)
				{
					//Todo: This method might generate LEDs that are already lit. To avoid this, we would need to look through the entire buffer for each new LED. Sorting might help here. Don't this unless really needed.
					//Generate a random LED
					if(st->pulseDir==-1)
					{
						st->glitterActiveLeds[st->currentLed]=0;	//Clear the current LED if direction is down
					}
					else
					{
						st->glitterActiveLeds[st->currentLed]=utilRandRange(stop-start)+1;	//We will skip LED 0 in the segment
					}
					if(st->pulseDir==-1 && st->currentLed==0)
					{
						st->currentLed=1;
					}
					st->currentLed+=st->pulseDir;
					if(st->currentLed>=glitterTotal)
					{
						st->pulseUpdatedCycle=true;
						//In loop_persist, we will restart the ring buffer from 0
						if(ps->mode == LEDSEG_MODE_GLITTER_LOOP_PERSIST)
						{
							st->currentLed=0;
						}
						else if(ps->mode==LEDSEG_MODE_GLITTER_BOUNCE)
						{
							st->currentLed=glitterTotal-1;
							st->pulseDir=-1;
							break;
						}
						else	//For LEDSEG_MODE_GLITTER_LOOP and LOOP_END
						{
							st->currentLed=glitterTotal;
							break;
						}
					}
					else if(st->currentLed==0 && ps->mode==LEDSEG_MODE_GLITTER_BOUNCE)
					{
						st->pulseUpdatedCycle=true;
						st->pulseDir=1;
					}
				}
				glitterJustGenerated=true;
			}
		}
		else
		{
			//Invalid mode, fail silently
			return;
		}
		st->cyclesToPulseMove = ps->pixelTime;

	}//End of Cycles to move



	//Glitter mode:
	if(isGlitterMode(ps->mode))
	{
		//Load the current index of the ring buffer
		uint16_t currentIndex=st->currentLed;
		if((ps->mode == LEDSEG_MODE_GLITTER_LOOP || ps->mode == LEDSEG_MODE_GLITTER_LOOP_END) && currentIndex==glitterTotal)
		{
			currentIndex--;
		}
		//Calculate how many leds per colout for rainbowMode
		uint16_t ledsPerCol=0;
		if(ps->colourSeqNum)
		{
			uint8_t tmp=1;
			if(ps->colourSeqLoops)
			{
				tmp=ps->colourSeqLoops;
			}
			ledsPerCol=(stop-start)/(ps->colourSeqNum*tmp);
		}

		//Handles the LED fading (the newest LEDs) for all modes. If no fading is going on (because we're done), currentIndex==glitterTotal.
		if(currentIndex<glitterTotal)
		{
			//Todo: Check general buffer indexing to see if we miss/use the same LED multiple times, etc

			//Go through the ring buffer in reverse, setting all fade LEDs to the proper colour
			for(uint16_t i=0;i<ps->pixelsPerIteration;i++)
			{
				//Get which LED it is (updating the ring buffer)
				if(currentIndex>0)
				{
					currentIndex--;
				}
				else
				{
					currentIndex=glitterTotal-1;
				}
				uint16_t ledIndex=st->glitterActiveLeds[currentIndex];
				RGB_t RGBMaxTmp;
				if(ps->colourSeqNum)
				{
					uint16_t colIndex=((ledIndex-1)/ledsPerCol)%ps->colourSeqNum;
					RGBMaxTmp=animGetColourFromSequence(ps->colourSeqPtr,colIndex,255);
				}
				else
				{
					RGBMaxTmp.r=ps->r_max;
					RGBMaxTmp.g=ps->g_max;
					RGBMaxTmp.b=ps->b_max;
				}
				//Only reset the colour if new LEDs were just generated
				if(glitterJustGenerated)
				{
					if(ps->mode == LEDSEG_MODE_GLITTER_BOUNCE && st->pulseDir==-1)
					{
						st->glitterR=RGBMaxTmp.r;
						st->glitterG=RGBMaxTmp.g;
						st->glitterB=RGBMaxTmp.b;
					}
					else
					{
						//Reset fade colour to 0, to start a new fade cycle
						st->glitterR=0;//st->r;
						st->glitterG=0;//st->g;
						st->glitterB=0;//st->b;
					}
				}
				//Update colour
				//Todo: Make sure we can fade to and from st->rgb (might require quite a bit of code and possibly syncing to get right...)

				st->glitterR=utilIncWithDir(st->glitterR,st->pulseDir,RGBMaxTmp.r/ps->pixelTime,0,RGBMaxTmp.r);
				st->glitterG=utilIncWithDir(st->glitterG,st->pulseDir,RGBMaxTmp.g/ps->pixelTime,0,RGBMaxTmp.g);
				st->glitterB=utilIncWithDir(st->glitterB,st->pulseDir,RGBMaxTmp.b/ps->pixelTime,0,RGBMaxTmp.b);
				ledSegSetLedWithGlobal(seg,ledIndex,st->glitterR,st->glitterG,st->glitterB,ps->globalSetting);

				//Check if colour is reached
				if(	(st->pulseDir==1 && st->glitterR == RGBMaxTmp.r && st->glitterG == RGBMaxTmp.g && st->glitterB == RGBMaxTmp.b) ||
						(st->pulseDir==-1 && st->glitterR == 0 && st->glitterG == 0 && st->glitterB == 0) )
				{
					//If colour is reached, check if we have just generated all LEDs and update cycle if needed.
					if(st->pulseUpdatedCycle)
					{
						st->pulseUpdatedCycle=false;
						if(checkCycleCounter(&st->pulseCycle))
						{
							st->currentLed=glitterTotal;
							st->pulseDone=true;
						}
					}
				}

			}
		}
		//Traverse the buffer in reverse from the point stopped to light up the rest of the LEDs fully (This will always run as long as the glitter pulse is active)
		for(uint16_t i=0;i<ps->ledsMaxPower;i++)
		{
			if(currentIndex>0)
			{
				currentIndex--;
			}
			else
			{
				currentIndex=glitterTotal-1;
			}
			//Get which LED it is
			uint16_t ledIndex=st->glitterActiveLeds[currentIndex];
			//An LED with number 0 indicates that this is something we have not handled yet
			if(ledIndex==0)
			{
				break;
			}
			//Generate maxColour for LED
			RGB_t RGBMaxTmp;
			if(ps->colourSeqNum)
			{
				uint16_t colIndex=((ledIndex-1)/ledsPerCol)%ps->colourSeqNum;
				RGBMaxTmp=animGetColourFromSequence(ps->colourSeqPtr,colIndex,255);
			}
			else
			{
				RGBMaxTmp.r=ps->r_max;
				RGBMaxTmp.g=ps->g_max;
				RGBMaxTmp.b=ps->b_max;
			}
			ledSegSetLedWithGlobal(seg,ledIndex,RGBMaxTmp.r,RGBMaxTmp.g,RGBMaxTmp.b,ps->globalSetting);
		}
	}
	else	//Not glitter mode
	{
		if(st->pulseActive)
		{
			//Set colour for all LEDs in pulse
			for(uint16_t i=0;i<pulseLength;i++)
			{
				//Generate where the LED shall be
				int16_t tmpLedNum=0;

				if(ps->mode == LEDSEG_MODE_LOOP_END || st->pulseUpdatedCycle)
				{
					tmpLedNum = st->currentLed+i*st->pulseDir*-1;
	//				if(i>=(pulseLength-1))
	//				{
	//					if(st->pulseDir==1 && tmpLedNum>stop)//Loop was finished, move currentLed to edge for next loop
	//					{
	//						st->currentLed = start;
	//						if(checkCycleCounter(&st->pulseCycle))
	//						{
	//							st->pulseDone = true;
	//							st->pulseActive = false;
	//						}
	//					}
	//					else if(st->pulseDir == -1 && tmpLedNum<start)
	//					{
	//						st->currentLed = stop;
	//						if(checkCycleCounter(&st->pulseCycle))
	//						{
	//							st->pulseDone = true;
	//							st->pulseActive = false;
	//						}
	//					}
	//				}
				}
				else if(ps->mode == LEDSEG_MODE_BOUNCE)
				{
					tmpLedNum=utilBounceValue(st->currentLed,i*st->pulseDir*-1,start,stop,NULL);
				}
				else if(ps->mode == LEDSEG_MODE_LOOP)
				{
					tmpLedNum=utilLoopValue(st->currentLed,i*st->pulseDir*-1,start,stop);
				}
				else
				{
					//Invalid mode, fail silently
					return;
				}
				//Calculate colours and write LED
				if(tmpLedNum>=start && tmpLedNum<=stop)
				{
					tmpR=pulseCalcColourPerLed(st,i+1,COL_RED);
					tmpG=pulseCalcColourPerLed(st,i+1,COL_GREEN);
					tmpB=pulseCalcColourPerLed(st,i+1,COL_BLUE);
					apa102SetPixelWithGlobal(strip,tmpLedNum,tmpR,tmpG,tmpB,ps->globalSetting,true);
				}
			}
		}
	}
}

/*
 * Takes a cycle counter and checks if it's done. Returns true if the cycles are completed
 */
static bool checkCycleCounter(uint32_t* cycle)
{
	uint32_t tmp=0;
	tmp=*cycle;
	if(!tmp)
	{
		return false;
	}
	tmp--;
	if(!tmp)
	{
		return true;
	}
	*cycle=tmp;
	return false;
}

/*
 * Takes a cycle counter and checks if it's done. Returns true if the cycles are completed
 */
static bool checkCycleCounterU16(uint16_t* cycle)
{
	uint16_t tmp=0;
	tmp=*cycle;
	if(!tmp)
	{
		return false;
	}
	tmp--;
	if(!tmp)
	{
		return true;
	}
	*cycle=tmp;
	return false;
}
static bool segSyncRelease[LEDSEG_MAX_SEGMENTS];
/*
 * Calculates the colour to set for the fade part of this segment
 * This colour is applied to all parts of the LED fade segment
 */
static void fadeCalcColour(uint8_t seg)
{
	volatile uint8_t segTmp=seg;
	ledSegmentState_t* st;
	ledSegmentFadeSetting_t* conf;
	if(!ledSegExists(seg))
	{
		return;
	}
	st=&(segments[seg].state);
	conf=&(st->confFade);
	uint8_t compareColor=COL_BLUE;
	bool redReversed=false;
	bool blueReversed=false;
	bool greenReversed=false;
	if(conf->b_max == conf->b_min)
	{
		compareColor = COL_GREEN;
		if(conf->g_max == conf->g_min)
		{
			compareColor = COL_RED;
		}
	}
	if(st->fadeActive)
	{
		//Check if the direction of any colour is reveresed
		if(conf->r_min>conf->r_max)
		{
			redReversed=true;
		}
		if(conf->g_min>conf->g_max)
		{
			greenReversed=true;
		}
		if(conf->b_min>conf->b_max)
		{
			blueReversed=true;
		}
		if(redReversed)
		{
			st->r=utilIncWithDir(st->r,st->fadeDir*-1,st->r_rate,conf->r_max,conf->r_min);
		}
		else
		{
			st->r=utilIncWithDir(st->r,st->fadeDir,st->r_rate,conf->r_min,conf->r_max);
		}
		if(greenReversed)
		{
			st->g=utilIncWithDir(st->g,st->fadeDir*-1,st->g_rate,conf->g_max,conf->g_min);
		}
		else
		{
			st->g=utilIncWithDir(st->g,st->fadeDir,st->g_rate,conf->g_min,conf->g_max);
		}
		if(blueReversed)
		{
			st->b=utilIncWithDir(st->b,st->fadeDir*-1,st->b_rate,conf->b_max,conf->b_min);
		}
		else
		{
			st->b=utilIncWithDir(st->b,st->fadeDir,st->b_rate,conf->b_min,conf->b_max);
		}
		//Check if we have reached an end (regardless of mode)
		bool bAtMin=false;
		bool bAtMax=false;
		bool gAtMin=false;
		bool gAtMax=false;
		bool rAtMin=false;
		bool rAtMax=false;
		bool allReached=false;
		//Check if each colour has reached its end
		if((blueReversed && st->b<=conf->b_max) || (!blueReversed && st->b>=conf->b_max))
		{
			bAtMax=true;
		}
		else if((blueReversed && st->b>=conf->b_min) || (!blueReversed && st->b<=conf->b_min))
		{
			bAtMin=true;
		}
		if((greenReversed && st->g<=conf->g_max) || (!greenReversed && st->g>=conf->g_max))
		{
			gAtMax=true;
		}
		else if((greenReversed && st->g>=conf->g_min) || (!greenReversed && st->g<=conf->g_min))
		{
			gAtMin=true;
		}
		if((redReversed && st->r<=conf->r_max) || (!redReversed && st->r>=conf->r_max))
		{
			rAtMax=true;
		}
		else if((redReversed && st->r>=conf->r_min) || (!redReversed && st->r<=conf->r_min))
		{
			rAtMin=true;
		}

		if((bAtMin || bAtMax) && (gAtMin || gAtMax) && (rAtMin || rAtMax))
		{
			allReached=true;
		}

		if(allReached)
		{

			/*
			 * Om den är i syncgrp, måste den vänta tills alla i samma syncgrp är klara innan den går vidare
			 * NÄR ett segment hamnar här och alla är klara, kan vi gå vidare.
			 * När vi går vidare måste resten av syncgruppen veta att vi blev klara och ska gå vidare.
			 *
			 * state 1: sync not done (vi kör som vanligt och går till sin extrem
			 * state 2: extreme reached, this seg ready for sync
			 * state 3: all reached and synced.
			 * state 4: sync done for this seg
			 * state 5: sync done for all, move to state 1
			 */

			volatile ledSegmentFadeState_t syncState=LEDSEG_FADE_NOT_DONE;
			if(conf->syncGroup)
			{
				//We enter here and we have not marked this as ready. Mark it as ready and continue to loop the fade to its own max
				if(st->fadeState==LEDSEG_FADE_NOT_DONE)
				{
					st->fadeState=LEDSEG_FADE_WAITING_FOR_SYNC;
				}
				if(checkSyncReadyFade(conf->syncGroup,seg))
				{
					segSyncRelease[conf->syncGroup]=true;
				}
				//Check: Have all reached and are we at the lowest seg in the group?
				//If so, we can release all segs in this group
			}
			/*
			 * Sync system (fade)
			 * 	Register syncs:
			 * 		Add the sync number to the fade setting.
			 * 		Send this setting to a segment
			 *
			 * 	During run
			 * 		When this segment has allReached and is about to clear the last cycle, mark as waitingForSync.
			 * 		Check if all segments with this sync number are waitingForSync.
			 * 			If yes, end cycle and continue
			 * 			If no, run one more cycle and check again.
			 */

			if(!conf->syncGroup || (conf->syncGroup && segSyncRelease[conf->syncGroup]))
			{
				//Check if the fade is done. if so, mark this fade as done. Otherwise, update what is do be done at an extreme
				if(st->fadeCycle && checkCycleCounter(&st->fadeCycle))
				{
					//Perform switch, if needed to. Otherwise, we're done
					if(st->switchMode)
					{
						st->switchMode=false;
						if(conf->startDir==1)	//We are at max
						{
							//Restore min
							conf->r_min = st->savedR;
							conf->g_min = st->savedG;
							conf->b_min = st->savedB;
						}
						else	//We are at min
						{
							//Restore max
							conf->r_max = st->savedR;
							conf->g_max = st->savedG;
							conf->b_max = st->savedB;
						}
						if(conf->mode == LEDSEG_MODE_LOOP || conf->mode == LEDSEG_MODE_LOOP_END)
						{
							conf->startDir = st->savedDir;
						}
						else if(conf->mode == LEDSEG_MODE_BOUNCE)
						{
							conf->startDir = st->fadeDir*-1;
						}
						conf->cycles = st->savedCycles;
						ledSegSetFade(seg,conf);
					}
					else
					{
						st->fadeState=LEDSEG_FADE_DONE;
					}
				}
				else
				{
					switch (conf->mode)
					{
						case LEDSEG_MODE_BOUNCE:
						{
							st->fadeDir*=-1;
							break;
						}
						//Loop and Loop_end works the same
						case LEDSEG_MODE_LOOP:
						case LEDSEG_MODE_LOOP_END:
						{
							if(st->fadeDir == -1)
							{
								st->r=conf->r_max;
								st->g=conf->g_max;
								st->b=conf->b_max;
							}
							else if(st->fadeDir == 1)
							{
								st->r=conf->r_min;
								st->g=conf->g_min;
								st->b=conf->b_min;
							}
							break;
						}
						default:
						{
							//For any other mode, do nothing
							break;
						}
					}
					st->fadeState=LEDSEG_FADE_NOT_DONE;
				}
				//Check if ALL segments in the group have finished syncing. If so, reset all of them to NOT_FINISHED (except if they're done)
				if(segSyncRelease[conf->syncGroup] && checkFadeNotWaitingForSyncAll(conf->syncGroup))
				{
					segSyncRelease[conf->syncGroup]=false;
				}
			}
		}	//End of allReached
	}
}

/*
 * Checks if all the fade animations in the same sync group are ready for sync
 * If syncgroup=0 (not part of any sync group), it is by definition synced with itself
 * Note: FADE_DONE is treated as FADE_SYNC_DONE
 */
static bool checkSyncReadyFade(uint8_t syncGrp, uint8_t seg)
{
	if(!syncGrp)
	{
		return true;
	}
	bool allState=true;
	uint8_t firstFound=255;
	ledSegmentState_t* st;
	for(uint8_t i=0;i<currentNofSegments;i++)
	{
		st=&(segments[i].state);
		if(st->confFade.syncGroup == syncGrp)
		{
			if(firstFound==255)
			{
				firstFound=i;
			}
			if(st->fadeState != LEDSEG_FADE_WAITING_FOR_SYNC)
			{
				allState=false;
			}
		}
	}
	return (allState && (firstFound==seg));
}

/*
 * Returns true if all none of the fades in a sync group are not in Waiting_for_sync
 * This is used to detect if they have already synced
 */
static bool checkFadeNotWaitingForSyncAll(uint8_t syncGrp)
{
	if(!syncGrp)
	{
		return true;
	}
	bool allState=true;
	ledSegmentState_t* st;
	for(uint8_t i=0;i<currentNofSegments;i++)
	{
		st=&(segments[i].state);
		if(st->confFade.syncGroup == syncGrp && st->fadeState==LEDSEG_FADE_WAITING_FOR_SYNC)
		{
			allState=false;
		}
	}
	return allState;
}

/*
 * This will reset all segments to LEDSEG_FADE_NOT_DONE
 */
static void resetSyncDoneGroup(uint8_t syncGrp)
{
	ledSegmentState_t* st;
	for(uint8_t seg=0;seg<currentNofSegments;seg++)
	{
		st=&(segments[seg].state);
		if(st->confFade.syncGroup == syncGrp && st->fadeState!=LEDSEG_FADE_DONE)
		{
			st->fadeState=LEDSEG_FADE_NOT_DONE;
		}
	}
}

/*
 * Tells if a mode is a glitter mode
 */
static bool isGlitterMode(ledSegmentMode_t mode)
{
	if(mode>=LEDSEG_MODE_GLITTER_LOOP && mode<=LEDSEG_MODE_GLITTER_BOUNCE)
	{
		return true;
	}
	else
	{
		return false;
	}
}

/*
 * Returns true if the LED exists within the segment and if the segment exists (Not valid for LEDSEG_ALL)
 */
static bool ledIsWithinSeg(uint8_t seg, uint16_t led)
{
	if(!ledSegExistsNotAll(seg))
	{
		return false;
	}
	if(led==0 || led>(segments[seg].stop-segments[seg].start+1))
	{
		return false;
	}
	return true;
}

/*
 * Returns true if a segment is configured to be excluded from a call with LEDSEG_ALL
 * LEDSEG_ALL and a non-existing segment are considered to be excluded
 */
static bool isExcludedFromAll(uint8_t seg)
{
	if(!ledSegExistsNotAll(seg))
	{
		return true;
	}
	return segments[seg].excludeFromAll;
}

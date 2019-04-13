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
 *
 *	Note on some animation tricks
 *	- Fill a segment with a pulse:
 *		Init a segment with a pulse and no fade. Use LOOP_END mode and 1 cycle
 *	- Use only pulse on a segment:
 *		Set max of all colours of fade=0
 *
 *
 *
 */

#include "ledSegment.h"

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

/*
 * Inits an LED segment
 * Will return a value larger than LEDSEG_MAX_SEGMENTS if there is no more room for segments or any other error
 * Note that it's possible to register a segment over another segment. There are no checks for this
 */
uint8_t ledSegInitSegment(uint8_t strip, uint16_t start, uint16_t stop,ledSegmentPulseSetting_t* pulse, ledSegmentFadeSetting_t* fade)
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
 * Returns true if a led segment exists
 */
bool ledSegExists(uint8_t seg)
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
	uint16_t periodMultiplier=3;
	bool makeItSlower=false;
	uint32_t master_steps=0;
	do
	{
		makeItSlower=false;
		master_steps=fs->fadeTime/(LEDSEG_UPDATE_PERIOD_TIME*periodMultiplier);
		st->r_rate = (fs->r_max-fs->r_min)/master_steps;
		if(st->r_rate<1 && fs->r_max)
		{
			makeItSlower=true;
		}
		st->g_rate = (fs->g_max-fs->g_min)/master_steps;
		if(st->g_rate<1 && fs->g_max)
		{
			makeItSlower=true;
		}
		st->b_rate = (fs->b_max-fs->b_min)/master_steps;
		if(st->b_rate<1 && fs->b_max)
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
	if((UINT32_MAX/st->fadeCycle)<master_steps)
	{
		st->fadeCycle = 0;
	}
	else
	{
		st->fadeCycle=fs->cycles*master_steps;	//Each cycle shall be one half cycle (min->max)
	}
	//If the global setting is not used, use whatever is set as default
	if(fd->globalSetting == 0)
	{
		fd->globalSetting = APA_MAX_GLOBAL_SETTING+1;
	}
	st->fadeActive = true;

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
	//Create some temp variables that are easier to use
	ledSegment_t* sg;
	sg=&(segments[seg]);
	ledSegmentState_t* st;
	st=&(sg->state);
	ledSegmentPulseSetting_t* pu;
	pu=&(sg->state.confPulse);
	//Copy new setting into state
	memcpy(pu,ps,sizeof(ledSegmentPulseSetting_t));
	st->pulseDir=ps->startDir;
	st->currentLed = ps->startLed;
	if(st->currentLed > sg->stop)
	{
		st->currentLed = sg->stop;
	}
	else if(st->currentLed < sg->start)
	{
		st->currentLed = sg->start;
	}
	st->pulseActive = true;
	st->cyclesToPulseMove = ps->pixelTime;
	//If the global setting is not used, use whatever is set as default
	if(pu->globalSetting == 0)
	{
		pu->globalSetting = APA_MAX_GLOBAL_SETTING+1;
	}

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
	if(!ledSegExists(seg))
	{
		return false;
	}
	if(led==0 || led>(segments[seg].stop-segments[seg].start+1))
	{
		return false;
	}
	apa102SetPixel(segments[seg].strip,segments[seg].start+led-1,r,g,b,false);
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
	return segments[seg].state.fadeActive;
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
	if(restartFade)
	{
		if(segments[seg].state.confFade.startDir == 1)
		{
			segments[seg].state.r = segments[seg].state.confFade.r_min;
			segments[seg].state.g = segments[seg].state.confFade.g_min;
			segments[seg].state.b = segments[seg].state.confFade.b_min;
			segments[seg].state.fadeDir = 1;
		}
		else
		{
			segments[seg].state.r = segments[seg].state.confFade.r_max;
			segments[seg].state.g = segments[seg].state.confFade.g_max;
			segments[seg].state.b = segments[seg].state.confFade.b_max;
			segments[seg].state.fadeDir = -1;
		}
	}
	if(restartPulse)
	{
		if(segments[seg].state.confPulse.startDir == 1)
		{
			segments[seg].state.currentLed = segments[seg].start;
			segments[seg].state.pulseDir = 1;
		}
		else
		{
			segments[seg].state.currentLed = segments[seg].stop;
			segments[seg].state.pulseDir = -1;
		}
	}
	return true;
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
 *	 Otherwise, we would need to keep two copies of the whole pixel system, costing about 3kB of additional RAM
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
		calcCycle++;
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
 * led is indexed from meaning (meaning currentLed has value 1)
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
			tmpMax=ps->r_max;
			tmpMin=st->r;
		break;
		case COL_GREEN:
			tmpMax=ps->g_max;
			tmpMin=st->g;
			break;
		case COL_BLUE:
			tmpMax=ps->b_max;
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

	//Move LED and update direction
	//Check if it's time to move a pixel
	if(checkCycleCounterU16(&st->cyclesToPulseMove))
	{
		if(ps->mode == LEDSEG_MODE_BOUNCE)
		{
			st->currentLed = utilBounceValue(st->currentLed,ps->pixelsPerIteration*st->pulseDir,start,stop,&tmpDir);
			st->pulseDir=tmpDir;
		}
		else if(ps->mode == LEDSEG_MODE_LOOP)
		{
			st->currentLed = utilLoopValue(st->currentLed,ps->pixelsPerIteration*st->pulseDir,start,stop);
		}
		else if(ps->mode == LEDSEG_MODE_LOOP_END)
		{
			st->currentLed =st->currentLed+ps->pixelsPerIteration*st->pulseDir;
		}
		else
		{
			//Invalid mode, fail silently
			return;
		}
		st->cyclesToPulseMove = ps->pixelTime;
	}
	//Set colour for all LEDs in pulse
	for(uint16_t i=0;i<pulseLength;i++)
	{
		//Generate where the LED shall be
		int16_t tmpLedNum=0;
		if(ps->mode == LEDSEG_MODE_BOUNCE)
		{
			tmpLedNum=utilBounceValue(st->currentLed,i*st->pulseDir*-1,start,stop,NULL);
		}
		else if(ps->mode == LEDSEG_MODE_LOOP)
		{
			tmpLedNum=utilLoopValue(st->currentLed,i*st->pulseDir*-1,start,stop);
		}
		else if(ps->mode == LEDSEG_MODE_LOOP_END)
		{
			tmpLedNum = st->currentLed+i*st->pulseDir*-1;
			if(i>=(pulseLength-1))
			{
				if(st->pulseDir==1 && tmpLedNum>stop)//Loop was finished, move currentLed to edge for next loop
				{
					st->currentLed = start;
					if(checkCycleCounter(&st->pulseCycle))
					{
						st->pulseActive = false;
					}
				}
				else if(st->pulseDir == -1 && tmpLedNum<start)
				{
					st->currentLed = stop;
					if(checkCycleCounter(&st->pulseCycle))
					{
						st->pulseActive = false;
					}
				}
			}
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

/*
 * Calculates the colour to set for the fade part of this segment
 * This colour is applied to all parts of the LED fade segment
 */
static void fadeCalcColour(uint8_t seg)
{
	ledSegmentState_t* st;
	ledSegmentFadeSetting_t* conf;
	if(!ledSegExists(seg))
	{
		return;
	}
	st=&(segments[seg].state);
	conf=&(st->confFade);
	uint8_t compareColor=COL_BLUE;
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
		st->r=utilIncWithDir(st->r,st->fadeDir,st->r_rate,conf->r_min,conf->r_max);
		st->g=utilIncWithDir(st->g,st->fadeDir,st->g_rate,conf->g_min,conf->g_max);
		st->b=utilIncWithDir(st->b,st->fadeDir,st->b_rate,conf->b_min,conf->b_max);

		//Update dir when one existing color is complete (only in bounce mode)
		if(conf->mode == LEDSEG_MODE_BOUNCE)
		{
			if(compareColor==COL_BLUE && conf->b_max)
			{
				if(st->b>=conf->b_max)
				{
					st->fadeDir=-1;
				}
				else if(st->b<=conf->b_min)
				{
					st->fadeDir=1;
				}
			}
			else if (compareColor==COL_GREEN  && conf->g_max)
			{
				if(st->g>=conf->g_max)
				{
					st->fadeDir=-1;
				}
				else if(st->g<=conf->g_min)
				{
					st->fadeDir=1;
				}
			}
			else if(conf->r_max)	//red
			{
				if(st->r>=conf->r_max)
				{
					st->fadeDir=-1;
				}
				else if(st->r<=conf->r_min)
				{
					st->fadeDir=1;
				}
			}
		}
		//if channel is active and cycles == 0, the channel will never be inactive
		if(checkCycleCounter(&st->fadeCycle))
		{
			st->fadeCycle=false;
		}
	}
}

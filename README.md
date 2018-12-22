# ledSegment
Library for controlling addressable LEDs

This library is used to control addressable LEDs. It can be used together with any low-level library for driving LEDs, provided there's a setPixel-function and a setPixelRange function, as well as an updateStrip function.
The fatures of the LED segment library includes:
* Define a part of the complete LED strip as a substrip, controlling it as though it was a physical strip (i.e. making virtual strips)
* All colours are in RGB, using 8,8,8 bits for the colours. Also supports the "Global" setting used by i.e. APA102.
* Performing 2 kinds of animations on each segment: Fade and pulse
  * Fade can fade from any colour to any other colour, using a set time for the fade.
  * The starting direction of the fade can be set.
  * Fade also supports the modes Fade up and down (bounce), fade stops when reaching max, fade restarts from bottom when done
  * Pulse is a pulse of wandering lights on a strip. It supports a colour and various settings regarding the length of the pulse. It's painted on top of the whatever fade setting is used. The tail of the pulse will fade into the background.
  * Pulse has various settings for speed, both by how often the pulse moves, and how many LEDs to move each iteration
  * Pulse has various settings for moving: It can move to the end of a segment and stop (one-shot), it can restart from the beginning, and it can bounce back. You can also set which direction the pulse shall go in.

The following additional support libraries are provided:
* A library for controlling APA102 using STM32F103 (with DMA and SPI/USART) on a low level
* Various useful utility functions
* A simple library for handling inputswitches with debounce and edge-detection

Everything is written in pure C.

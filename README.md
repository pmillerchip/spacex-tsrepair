# spacex-tsrepair
## MPEG-TS repair software for the SpaceX CRS-3 video repair task

This software was written in May 2014 to process the corrupted CRS-3
landing video posted by SpaceX: [SpaceX's page](http://www.spacex.com/news/2014/04/29/first-stage-landing-video)

I originally posted it on the nasaspaceflight.com forum [here](http://forum.nasaspaceflight.com/index.php?topic=34597.msg1205369#msg1205369)
and that message explains in detail how to use it.

The software was written quickly in order to get results as fast as we
could, so the quality isn't great and it is tailored specifically to
the SpaceX video file. Don't use this as an example of good code!

The software was tested on Ubuntu Linux but it should compile on any modern
Linux distribution. It is written in C++ and uses the g++ compiler.
It has no dependencies so it should build easily.

To build it, just check out the repository and type "make". It will build,
download the raw.ts file from SpaceX and produce the following files:

* tsrepair - The program itself
* raw_aligned.ts - An aligned version of SpaceX's file, so that all the packets are on the correct 188-byte borders
* aligned.txt - A report of the final state of the raw_aligned.ts file


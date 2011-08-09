rtspeccy
========

Real time spectrum analyzer (audio).

Main features:

 * Keep it simple. No complex configuration file, simply a `config.h`.
 * Uses ALSA to record sound.
 * Uses FFTW3 to calculate the spectrum.
 * Displays the spectrogram using OpenGL (GLUT).
 * Split screen: Show history as well as current spectrum.
 * Freeze, zooming and panning.
 * Overtone guidelines: Click and hold the left mouse button to show
   guidelines which indicate where over- and undertones of the current
   frequency are located.
 * History is colored via a color ramp. This can be set in your
   `config.h`.
 * Free software (GPL3).

Dependencies
------------

 * [glut](http://freeglut.sourceforge.net/) and OpenGL.
 * [Userland ALSA lib (alsa-lib)](http://www.alsa-project.org/).
 * [FFTW3](http://www.fftw.org/).

rtspeccy has only been tested on GNU/Linux (Arch Linux) with
[gcc](http://gcc.gnu.org/) 4.6.

Configuration
-------------

rtspeccy is configured via its `config.h`. An example is shipped with
the source code. It's pretty self explanatory.

However, you can change the capture device at startup time. Set the
environment variable `RTSPECCY_CAPTURE_DEVICE` in order to do that. See
`arecord -L` for a list of available devices. For example, to switch to
my Samson C03U, I do:

	$ arecord -L
	...
	front:CARD=C03U,DEV=0
	    Samson C03U, USB Audio
	...
	$ RTSPECCY_CAPTURE_DEVICE='front:CARD=C03U,DEV=0' rtspeccy

These identifiers usually don't change, so you can set up shell aliases
for them.

Controls
--------

 * Space bar: Freeze.
 * `r` or `u`: Reset view (zooming, panning).
 * `o`: Force overview, i.e. temporarily disable zooming and panning.
 * `g`: Toggle main grid.
 * `h`: Pan screen a quarter to the left.
 * `l`: Pan screen a quarter to the right.
 * `j`: Multiply zoom factor by 2.
 * `k`: Divide zoom factor by 2.
 * Most of the interesting stuff happens in the first quarter of the
   spectrogram, `H` focuses that region (short for `j`, `h`, `h`, `j`,
   `h`, `h`).
 * Mouse wheel: Zoom along the X axis.
 * Right mouse drag: Pan along the X axis.
 * Left mouse: Show overtone guidelines.
 * `w`: Toggle between current line spectrum and waveform.
 * Escape or `q`: Quit.

Contact
-------

[uninformativ.de](http://www.uninformativ.de/)

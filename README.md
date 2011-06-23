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
 * Escape or `q`: Quit.

Contact
-------

[uninformativ.de](http://www.uninformativ.de/)

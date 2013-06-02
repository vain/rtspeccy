/*
	Copyright 2011  P. Hofmann

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
	References:
	http://www.linuxjournal.com/article/6735  (basic recording code)
*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <GL/glut.h>
#include <alsa/asoundlib.h>
#include <fftw3.h>
#include <math.h>
#include <string.h>

/* Informations about the window, display options. */
struct interactionInfo
{
	int width;
	int height;

	int update;

	int showOvertones;
	int doPanning;
	int forceOverview;
	int showMainGrid;
	int showWaveform;
	int showFrequency;
	int frequencyLabelLeft;

	int lastMouseDownBS[2];
	int lastMouseDownES[2];
	double lastMouseDownBW[2];
	double lastMouseDownEW[2];

	double offsetX, lastOffsetX;
	double scaleX;
} interaction;

/* Global sound info. */
struct soundInfo
{
	snd_pcm_t *handle;

	char *buffer, *bufferLast;
	snd_pcm_uframes_t bufferSizeFrames;
	snd_pcm_uframes_t bufferFill;
	int bufferReady;

	int reprepare;
} sound;

/* Global fftw info. */
struct fftwInfo
{
	double *in;
	fftw_complex *out;
	fftw_plan plan;
	int outlen;
	double binWidth;

	double *currentLine;
	unsigned char *textureData;
	GLuint textureHandle;
	int textureWidth, textureHeight;
} fftw;

/* Check for OpenGL errors. */
void checkError(int line)
{
	GLenum err = glGetError();
	switch (err)
	{
		case GL_NO_ERROR:
			break;

		case GL_INVALID_ENUM:
			fprintf(stderr, "GL_INVALID_ENUM: %d\n", line);
			break;
		case GL_INVALID_VALUE:
			fprintf(stderr, "GL_INVALID_VALUE: %d\n", line);
			break;
		case GL_INVALID_OPERATION:
			fprintf(stderr, "GL_INVALID_OPERATION: %d\n", line);
			break;
		case GL_STACK_OVERFLOW:
			fprintf(stderr, "GL_STACK_OVERFLOW: %d\n", line);
			break;
		case GL_STACK_UNDERFLOW:
			fprintf(stderr, "GL_STACK_UNDERFLOW: %d\n", line);
			break;
		case GL_OUT_OF_MEMORY:
			fprintf(stderr, "GL_OUT_OF_MEMORY: %d\n", line);
			break;
		case GL_TABLE_TOO_LARGE:
			fprintf(stderr, "GL_TABLE_TOO_LARGE: %d\n", line);
			break;
	}
}

/* Get i'th sample from buffer and convert to short int. */
short int getFrame(char *buffer, int i)
{
	return (buffer[2 * i] & 0xFF) + ((buffer[2 * i + 1] & 0xFF) << 8);
}

/* Return the environment variable "name" or "def" if it's unset. */
char *getenvDefault(char *name, char *def)
{
	char *val = getenv(name);
	if (val == NULL)
		return def;
	else
		return val;
}

/* Open and init the default recording device. */
void audioInit(void)
{
	int rc;
	int size;
	snd_pcm_hw_params_t *params;
	unsigned int val;
	int dir = 0;

	/* Open PCM device for recording (capture). */
	rc = snd_pcm_open(&sound.handle, getenvDefault(SOUND_DEVICE_ENV,
	                                               SOUND_DEVICE),
	                  SND_PCM_STREAM_CAPTURE, 0);
	if (rc < 0)
	{
		fprintf(stderr, "unable to open pcm device: %s\n", snd_strerror(rc));
		exit(EXIT_FAILURE);
	}

	/* Allocate a hardware parameters object. */
	snd_pcm_hw_params_alloca(&params);

	/* Fill it in with default values. */
	snd_pcm_hw_params_any(sound.handle, params);

	/* Set the desired hardware parameters. */

	/* Interleaved mode. */
	snd_pcm_hw_params_set_access(sound.handle, params,
	                             SND_PCM_ACCESS_RW_INTERLEAVED);

	/* Signed 16-bit little-endian format. */
	snd_pcm_hw_params_set_format(sound.handle, params,
	                             SND_PCM_FORMAT_S16_LE);

	/* One channel (mono). */
	snd_pcm_hw_params_set_channels(sound.handle, params, 1);

	/* 44100 bits/second sampling rate (CD quality). */
	val = SOUND_RATE;
	snd_pcm_hw_params_set_rate_near(sound.handle, params, &val, &dir);

	/* Set period size. It's best to set this to the same value as
	 * SOUND_SAMPLES_PER_TURN. A lower value would generate more
	 * hardware interrupts and thus a lower latency but that's of no use
	 * since we have to wait for an amount of SOUND_SAMPLES_PER_TURN
	 * samples anyway. */
	snd_pcm_uframes_t frames = SOUND_SAMPLES_PER_TURN;
	snd_pcm_hw_params_set_period_size_near(sound.handle, params,
	                                       &frames, &dir);

	/* Write the parameters to the driver. */
	rc = snd_pcm_hw_params(sound.handle, params);
	if (rc < 0)
	{
		fprintf(stderr, "unable to set hw parameters: %s\n",
		        snd_strerror(rc));
		exit(EXIT_FAILURE);
	}

	/* Acquire n frames per turn. */
	sound.bufferSizeFrames = SOUND_SAMPLES_PER_TURN;
	size = sound.bufferSizeFrames * 2;  /* 2 bytes/sample, 1 channel */

	/* Initialize the buffer. */
	sound.buffer = (char *)malloc(size);
	sound.bufferLast = (char *)malloc(size);
	sound.bufferFill = 0;
	sound.bufferReady = 0;

	/* Try to switch to non-blocking mode for reading. If that fails,
	 * print a warning and continue in blocking mode. */
	rc = snd_pcm_nonblock(sound.handle, 1);
	if (rc < 0)
	{
		fprintf(stderr, "Could not switch to non-blocking mode: %s\n",
		        snd_strerror(rc));
	}

	/* Prepare in audioRead() for the first time. */
	sound.reprepare = 1;
}

/* Read as far as you can (when in non-blocking mode) or until our
 * buffer is filled (when in blocking mode). */
int audioRead(void)
{
	if (sound.reprepare)
	{
		int ret;
		ret = snd_pcm_drop(sound.handle);
		if (ret < 0)
		{
			fprintf(stderr, "Error while dropping samples: %s\n",
			        snd_strerror(ret));
		}

		ret = snd_pcm_prepare(sound.handle);
		if (ret < 0)
		{
			fprintf(stderr, "Error while preparing to record: %s\n",
			        snd_strerror(ret));
		}

		sound.reprepare = 0;
	}

	/* Request
	 *   "size - fill" frames
	 * starting at
	 *   "base + numFramesFilled * 2" bytes.
	 * Do "* 2" because we get two bytes per sample.
	 *
	 * When in blocking mode, this always fills the buffer to its
	 * maximum capacity.
	 */
	snd_pcm_sframes_t rc;
	rc = snd_pcm_readi(sound.handle, sound.buffer + (sound.bufferFill * 2),
	                   sound.bufferSizeFrames - sound.bufferFill);
	if (rc == -EPIPE)
	{
		/* EPIPE means overrun */
		snd_pcm_recover(sound.handle, rc, 0);
	}
	else if (rc == -EAGAIN)
	{
		/* Not ready yet. Come back again later. */
	}
	else if (rc < 0)
	{
		fprintf(stderr, "error from read: %s\n", snd_strerror(rc));
	}
	else
	{
		sound.bufferFill += rc;
		if (sound.bufferFill == sound.bufferSizeFrames)
		{
			/* Buffer full. display() can add this to the history. */
			sound.bufferFill = 0;
			sound.bufferReady = 1;
		}
	}

	return rc;
}

/* Shutdown audio device. */
void audioDeinit(void)
{
	snd_pcm_drop(sound.handle);
	snd_pcm_close(sound.handle);
	free(sound.buffer);
	free(sound.bufferLast);
}

/* Create FFTW-plan, allocate buffers. */
void fftwInit(void)
{
	fftw.outlen = sound.bufferSizeFrames / 2;
	fftw.in = (double *)fftw_malloc(sizeof(double) * sound.bufferSizeFrames);
	fftw.out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex)
	                                       * (fftw.outlen + 1));
	fftw.plan = fftw_plan_dft_r2c_1d(sound.bufferSizeFrames, fftw.in, fftw.out,
	                                 FFTW_ESTIMATE);

	fftw.currentLine = (double *)malloc(sizeof(double) * fftw.outlen);
	memset(fftw.currentLine, 0, sizeof(double) * fftw.outlen);

	fftw.textureWidth = fftw.outlen;
	fftw.textureHeight = FFTW_HISTORY_SIZE;
	fftw.textureData = (unsigned char *)malloc(sizeof(unsigned char)
	                                           * fftw.textureWidth
	                                           * fftw.textureHeight * 3);
	memset(fftw.textureData, 0, sizeof(unsigned char)
	       * fftw.textureWidth * fftw.textureHeight * 3);

	/* How many hertz does one "bin" comprise? */
	fftw.binWidth = (double)SOUND_RATE / (double)SOUND_SAMPLES_PER_TURN;
}

/* Free any FFTW resources. */
void fftwDeinit(void)
{
	fftw_destroy_plan(fftw.plan);
	fftw_free(fftw.in);
	fftw_free(fftw.out);
	free(fftw.currentLine);
	free(fftw.textureData);
	fftw_cleanup();
}

/* Read from audio device and display current buffer. */
void updateDisplay(void)
{
	int i;

	float bgcolor[3] = DISPLAY_BACKGROUND_COLOR;
	glClearColor(bgcolor[0], bgcolor[1], bgcolor[2], 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	if (interaction.update)
	{
		/* Try again *now* if it failed. */
		while (audioRead() < 0);
	}

	if (sound.bufferReady)
	{
		/* The buffer is marked as "full". We can now read it. After the
		 * texture has been updated, the buffer gets marked as "not
		 * ready". */

		/* First, copy the current buffer to the secondary buffer. We
		 * will show that second buffer if the first buffer is not yet
		 * ready. */
		memmove(sound.bufferLast, sound.buffer, sound.bufferSizeFrames * 2);

		/* Calculate spectrum. Casting "sound.bufferSizeFrames" works as
		 * long as it's less than 2GB. I assume this to be true because
		 * nobody will read 2GB at once from his sound card (at least
		 * not today :-). */
		for (i = 0; i < (int)sound.bufferSizeFrames; i++)
		{
			short int val = getFrame(sound.buffer, i);
			fftw.in[i] = 2 * (double)val / (256 * 256);
		}
		fftw_execute(fftw.plan);

		/* Draw history into a texture. First, move old texture one line up. */
		memmove(fftw.textureData + (3 * fftw.textureWidth), fftw.textureData,
		        (fftw.textureHeight - 1) * fftw.textureWidth * 3);

		int ha = 0, ta = 0;
		double histramp[][4] = DISPLAY_SPEC_HISTORY_RAMP;
		for (i = 0; i < fftw.outlen; i++)
		{
			double val = sqrt(fftw.out[i][0] * fftw.out[i][0]
			                  + fftw.out[i][1] * fftw.out[i][1]) / FFTW_SCALE;
			val = val > 1.0 ? 1.0 : val;

			/* Save current line for current spectrum. */
			fftw.currentLine[ha++] = val;

			/* Find first index where "val" is outside that color
			 * interval. */
			int colat = 1;
			while (colat < DISPLAY_SPEC_HISTORY_RAMP_NUM
			       && val > histramp[colat][0])
				colat++;

			colat--;

			/* Scale "val" into this interval. */
			double span = histramp[colat + 1][0] - histramp[colat][0];
			val -= histramp[colat][0];
			val /= span;

			/* Interpolate those two colors linearly. */
			double colnow[3];
			colnow[0] = histramp[colat][1] * (1 - val)
			            + val * histramp[colat + 1][1];
			colnow[1] = histramp[colat][2] * (1 - val)
			            + val * histramp[colat + 1][2];
			colnow[2] = histramp[colat][3] * (1 - val)
			            + val * histramp[colat + 1][3];

			/* Write this line into new first line of the texture. */
			fftw.textureData[ta++] = (unsigned char)(colnow[0] * 255);
			fftw.textureData[ta++] = (unsigned char)(colnow[1] * 255);
			fftw.textureData[ta++] = (unsigned char)(colnow[2] * 255);
		}
	}

	/* Enable texturing for the quad/history. */
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, fftw.textureHandle);
	if (sound.bufferReady)
	{
		/* Update texture. */
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
		                fftw.textureWidth, fftw.textureHeight,
		                GL_RGB, GL_UNSIGNED_BYTE, fftw.textureData);
		checkError(__LINE__);

		/* Reset buffer state. The buffer is no longer ready and we
		 * can't update the texture from it until audioRead() re-marked
		 * it as ready. */
		sound.bufferReady = 0;
	}

	/* Apply zoom and panning. */
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	if (!interaction.forceOverview)
	{
		glScaled(interaction.scaleX, 1, 1);
		glTranslated(interaction.offsetX, 0, 0);
	}

	/* Draw a textured quad. */
	glColor3f(1, 1, 1);
	glBegin(GL_QUADS);
	/* The texture must be moved half the width of a bin to the left to
	 * match the line spectrogram. (Yes, these "0.5"s cancel out. Let
	 * the compiler do this. It's easier to understand this way.) */
	double halfBin = (0.5 * fftw.binWidth) / (0.5 * SOUND_RATE);
	glTexCoord2d(0 + halfBin, 0);  glVertex2f(-1, -0.5);
	glTexCoord2d(1 + halfBin, 0);  glVertex2f( 1, -0.5);
	glTexCoord2d(1 + halfBin, 1);  glVertex2f( 1,  1);
	glTexCoord2d(0 + halfBin, 1);  glVertex2f(-1,  1);
	glEnd();
	glDisable(GL_TEXTURE_2D);

	/* Show current spectrum. */
	if (!interaction.showWaveform)
	{
		float curcol[3] = DISPLAY_SPEC_CURRENT_COLOR;
		glColor3fv(curcol);
		glBegin(GL_LINE_STRIP);
		for (i = 0; i < fftw.outlen; i++)
		{
			/* relX will be in [-1, 1], relY will be in [0, 1]. */
			double relX = 2 * ((double)i / fftw.outlen) - 1;
			double relY = fftw.currentLine[i];

			/* Move relY so it'll be shown at the bottom of the screen. */
			relY *= 0.5;
			relY -= 1;
			glVertex2f(relX, relY);
		}
		glEnd();
	}
	else
	{
		glPushMatrix();
		glLoadIdentity();
		float curcol[3] = DISPLAY_WAVEFORM_COLOR;
		glColor3fv(curcol);
		glBegin(GL_LINE_STRIP);
		for (i = 0; i < (int)sound.bufferSizeFrames; i++)
		{
			/* relX will be in [-1, 1], relY will be in [-s, s] where s
			 * is WAVEFORM_SCALE. */
			short int val = getFrame(sound.bufferLast, i);
			double relX = 2 * ((double)i / sound.bufferSizeFrames) - 1;
			double relY = 2 * WAVEFORM_SCALE * (double)val / (256 * 256);

			/* Clamp relY ... WAVEFORM_SCALE may be too high. */
			relY = relY > 1 ? 1 : relY;
			relY = relY < -1 ? -1 : relY;

			/* Move relY so it'll be shown at the bottom of the screen. */
			relY *= 0.25;
			relY -= 0.75;
			glVertex2f(relX, relY);
		}
		glEnd();
		glPopMatrix();
	}

	float lineYStart = -1;
	if (interaction.showWaveform)
		lineYStart = -0.5;

	/* Current line and overtones? */
	if (interaction.showOvertones)
	{
		glBegin(GL_LINES);

		/* Crosshair. */
		float colcross[3] = DISPLAY_LINECOLOR_CROSS;
		glColor3fv(colcross);
		glVertex2f(interaction.lastMouseDownEW[0], lineYStart);
		glVertex2f(interaction.lastMouseDownEW[0], 1);

		glColor3fv(colcross);
		glVertex2f(-1, interaction.lastMouseDownEW[1]);
		glVertex2f( 1, interaction.lastMouseDownEW[1]);

		/* Indicate overtones at all multiples of the current frequency
		 * (... this draws unneccssary lines when zoomed in). Don't draw
		 * these lines if they're less than 5 pixels apart. */
		float colover[3] = DISPLAY_LINECOLOR_OVERTONES;
		glColor3fv(colover);
		double nowscale = interaction.forceOverview ? 1 : interaction.scaleX;
		double xInitial = interaction.lastMouseDownEW[0] + 1;
		if (xInitial * interaction.width * nowscale > 5)
		{
			double x = xInitial * 2;
			while (x - 1 < 1)
			{
				glVertex2f(x - 1, lineYStart);
				glVertex2f(x - 1, 1);
				x += xInitial;
			}
		}

		/* Undertones until two lines are less than 2 pixels apart. */
		double x = xInitial;
		while ((0.5 * x * interaction.width * nowscale)
		       - (0.25 * x * interaction.width * nowscale) > 2)
		{
			x /= 2;
			glVertex2f(x - 1, lineYStart);
			glVertex2f(x - 1, 1);
		}

		glEnd();
	}
	else if (interaction.showMainGrid)
	{
		glBegin(GL_LINES);

		/* Show "main grid" otherwise. */
		float colgrid1[3] = DISPLAY_LINECOLOR_GRID_1;
		glColor3fv(colgrid1);
		glVertex2f(0, lineYStart);
		glVertex2f(0, 1);

		float colgrid2[3] = DISPLAY_LINECOLOR_GRID_2;
		glColor3fv(colgrid2);
		glVertex2f(0.5, lineYStart);
		glVertex2f(0.5, 1);

		glVertex2f(-0.5, lineYStart);
		glVertex2f(-0.5, 1);

		glEnd();
	}

	if (interaction.showFrequency)
	{
		/* Scale from [-1, 1] to [0, fftw.outlen). */
		double t = (interaction.lastMouseDownEW[0] + 1) / 2.0;
		int bin = (int)round(t * fftw.outlen);
		bin = (bin < 0 ? 0 : bin);
		bin = (bin >= fftw.outlen ? fftw.outlen - 1 : bin);

		/* Where exactly is this bin displayed? We want to snap our
		 * guide line to that position. */
		double snapX = ((double)bin / fftw.outlen) * 2 - 1;

		/* SOUND_RATE and SOUND_SAMPLES_PER_TURN determine the "size" of
		 * each "bin" (see calculation of binWidth). Each bin has a size
		 * of some hertz. The i'th bin corresponds to a frequency of i *
		 * <that size> Hz. Note that the resolution is pretty low on
		 * most setups, so it doesn't make any sense to display decimal
		 * places. */
		int freq = (int)(fftw.binWidth * bin);

		/* Draw frequency -- left or right of the guide line. */
		float coltext[3] = DISPLAY_TEXTCOLOR;
		glColor3fv(coltext);

		double nowscale = interaction.forceOverview ? 1 : interaction.scaleX;
		double nowoffX = interaction.forceOverview ? 0 : interaction.offsetX;
		double screenX = (interaction.lastMouseDownEW[0] + nowoffX) * nowscale;

		/* Flipping the label could be done at exactly 50% of the
		 * screen. But we only flip it if it's some pixels away from the
		 * center. */
		if (screenX < -0.25)
		{
			interaction.frequencyLabelLeft = 1;
		}
		else if (screenX > 0.25)
		{
			interaction.frequencyLabelLeft = 0;
		}

		char freqstr[256] = "";
		if (interaction.frequencyLabelLeft)
		{
			glRasterPos2d(snapX, interaction.lastMouseDownEW[1]);
			snprintf(freqstr, 256, " <- approx. %d Hz", freq);
		}
		else
		{
			snprintf(freqstr, 256, "approx. %d Hz -> ", freq);
			glRasterPos2d(snapX - 10 * (double)strlen(freqstr)
			              / interaction.width / nowscale,
			              interaction.lastMouseDownEW[1]);
		}

		size_t i;
		for (i = 0; i < strlen(freqstr); i++)
			glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, freqstr[i]);

		/* Show guideline for this frequency. */
		float colcross[3] = DISPLAY_LINECOLOR_CROSS;
		glColor3fv(colcross);
		glBegin(GL_LINES);
		glVertex2f(snapX, lineYStart);
		glVertex2f(snapX, 1);
		glEnd();
	}

	/* Separator between current spectrum and history; border. */
	glBegin(GL_LINES);

	float colborder[3] = DISPLAY_LINECOLOR_BORDER;
	glColor3fv(colborder);
	glVertex2f(-1, -0.5);
	glVertex2f( 1, -0.5);

	glVertex2f(-1, lineYStart);
	glVertex2f(-1, 1);

	glVertex2f( 1, lineYStart);
	glVertex2f( 1, 1);

	glEnd();

	glutSwapBuffers();
}

/* Simple orthographic projection. */
void reshape(int w, int h)
{
	interaction.width = w;
	interaction.height = h;

	glViewport(0, 0, w, h);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(-1, 1, -1, 1, -4, 4);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glutPostRedisplay();
}

/* Keyboard interaction. */
void keyboard(unsigned char key,
              int x __attribute__((unused)),
              int y __attribute__((unused)))
{
	switch (key)
	{
		case 27:
		case 'q':
			exit(EXIT_SUCCESS);

		case ' ':
			interaction.update = !interaction.update;
			sound.reprepare = 1;
			break;

		case 'u':
		case 'r':
			interaction.offsetX = 0;
			interaction.lastOffsetX = 0;
			interaction.scaleX = 1;
			break;

		case 'o':
			interaction.forceOverview = !interaction.forceOverview;
			break;

		case 'j':
			interaction.scaleX *= 2;
			break;

		case 'k':
			interaction.scaleX /= 2;
			break;

		case 'h':
			interaction.offsetX += 0.5 / interaction.scaleX;
			interaction.lastOffsetX = interaction.offsetX;
			break;

		case 'l':
			interaction.offsetX -= 0.5 / interaction.scaleX;
			interaction.lastOffsetX = interaction.offsetX;
			break;

		case 'H':
			interaction.scaleX = 4;
			interaction.offsetX = 0.75;
			interaction.lastOffsetX = interaction.offsetX;
			break;

		case 'g':
			interaction.showMainGrid = !interaction.showMainGrid;
			break;

		case 'w':
			interaction.showWaveform = !interaction.showWaveform;
			break;
	}
}

/* Convert 2D screen coordinates into world coordinates. */
void worldCoord(int *screen, double *world)
{
	world[0] = (double)screen[0] / interaction.width;
	world[1] = (double)screen[1] / interaction.height;

	world[0] *= 2;
	world[1] *= 2;

	world[0] -= 1;
	world[1] -= 1;

	world[1] *= -1;

	/* Panning and scaling only on X axis. */
	if (!interaction.forceOverview)
	{
		world[0] /= interaction.scaleX;
		world[0] -= interaction.lastOffsetX;
	}
}

/* Mouse clicks. */
void mouse(int button, int state, int x, int y)
{
	if (state == GLUT_DOWN)
	{
		/* Save mouse positions for everything but zooming. */
		if (button == GLUT_LEFT_BUTTON || button == GLUT_RIGHT_BUTTON
		    || button == GLUT_MIDDLE_BUTTON)
		{
			interaction.lastMouseDownBS[0] = x;
			interaction.lastMouseDownBS[1] = y;
			worldCoord(interaction.lastMouseDownBS,
			           interaction.lastMouseDownBW);
			interaction.lastMouseDownEW[0] = interaction.lastMouseDownBW[0];
			interaction.lastMouseDownEW[1] = interaction.lastMouseDownBW[1];
		}

		if (button == GLUT_LEFT_BUTTON)
		{
			interaction.showOvertones = 1;
		}
		else if (button == GLUT_RIGHT_BUTTON && !interaction.forceOverview)
		{
			interaction.doPanning = 1;
			interaction.lastOffsetX = interaction.offsetX;
		}
		else if (button == INTERACTION_ZOOM_IN)
		{
			interaction.scaleX *= INTERACTION_ZOOM_SPEED;
		}
		else if (button == INTERACTION_ZOOM_OUT)
		{
			interaction.scaleX /= INTERACTION_ZOOM_SPEED;
			if (interaction.scaleX < 1)
				interaction.scaleX = 1;
		}
		else if (button == GLUT_MIDDLE_BUTTON)
		{
			interaction.showFrequency = 1;
		}
	}
	else
	{
		/* Copy new offset if we were panning. */
		if (interaction.doPanning)
		{
			double dx = interaction.lastMouseDownEW[0]
			            - interaction.lastMouseDownBW[0];
			interaction.offsetX = interaction.lastOffsetX + dx;
			interaction.lastOffsetX = interaction.offsetX;
		}

		interaction.showOvertones = 0;
		interaction.doPanning = 0;
		interaction.showFrequency = 0;
	}
}

/* Mouse movements/drags. */
void motion(int x, int y)
{
	if (!interaction.showOvertones && !interaction.doPanning
	    && !interaction.showFrequency)
		return;

	interaction.lastMouseDownES[0] = x;
	interaction.lastMouseDownES[1] = y;
	worldCoord(interaction.lastMouseDownES, interaction.lastMouseDownEW);

	if (interaction.doPanning)
	{
		double dx = interaction.lastMouseDownEW[0]
		            - interaction.lastMouseDownBW[0];
		interaction.offsetX = interaction.lastOffsetX + dx;
	}
}

/* Create the window, set up callbacks and interaction parameters. */
void displayInit(int argc, char *argv[])
{
	interaction.width = DISPLAY_INITIAL_WIDTH;
	interaction.height = DISPLAY_INITIAL_HEIGHT;
	interaction.update = 1;
	interaction.showOvertones = 0;
	interaction.doPanning = 0;
	interaction.forceOverview = 0;
	interaction.showMainGrid = 1;
	interaction.showWaveform = 0;
	interaction.scaleX = 1;
	interaction.offsetX = 0;
	interaction.lastOffsetX = 0;
	interaction.showFrequency = 0;
	interaction.frequencyLabelLeft = 1;

#ifdef INTERACTION_ZOOM_STARTUP_FIRST_QUARTER
	interaction.scaleX = 4;
	interaction.offsetX = 0.75;
	interaction.lastOffsetX = interaction.offsetX;
#endif

	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
	glutInitWindowSize(interaction.width, interaction.height);
	glutCreateWindow("rtspeccy");

	glutDisplayFunc(updateDisplay);
	glutReshapeFunc(reshape);
	glutKeyboardFunc(keyboard);
	glutMouseFunc(mouse);
	glutMotionFunc(motion);
	glutIdleFunc(updateDisplay);
}

/* Create an initial texture (name + data). */
void textureInit(void)
{
	glEnable(GL_TEXTURE_2D);
	glGenTextures(1, &fftw.textureHandle);
	glBindTexture(GL_TEXTURE_2D, fftw.textureHandle);
	glTexImage2D(GL_TEXTURE_2D, 0, 3,
	             fftw.textureWidth, fftw.textureHeight, 0,
	             GL_RGB, GL_UNSIGNED_BYTE, fftw.textureData);
	checkError(__LINE__);

	/* "Smooth" texture filtering. */
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	/* No texture wrapping! See display(), we have to move the texture a
	 * little to the left. Texture wrapping would result in a wrong
	 * spectrogram. */
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

	glDisable(GL_TEXTURE_2D);
}

/* Delete the texture. */
void textureDeinit(void)
{
	glEnable(GL_TEXTURE_2D);
	glDeleteTextures(1, &fftw.textureHandle);
	glDisable(GL_TEXTURE_2D);
	checkError(__LINE__);
}

int main(int argc, char *argv[])
{
	displayInit(argc, argv);
	audioInit();
	fftwInit();
	textureInit();

	atexit(audioDeinit);
	atexit(fftwDeinit);
	atexit(textureDeinit);

	glutMainLoop();
	return 0;  /* Not reached. */
}

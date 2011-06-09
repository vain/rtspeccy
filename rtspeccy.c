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
#include <endian.h>
#include <fftw3.h>
#include <math.h>
#include <string.h>

/* Informations about the window, display options. */
struct interactionInfo
{
	int width;
	int height;

	int update;
} interaction;

/* Global sound info. */
struct soundInfo
{
	snd_pcm_t *handle;
	char *buffer;
	int bufferCountFrames;
	snd_pcm_uframes_t frames;

	int reprepare;
} sound;

/* Global fftw info. */
struct fftwInfo
{
	double *in;
	fftw_complex *out;
	fftw_plan plan;
	int outlen;

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
	return (buffer[2 * i] & 0xFF)
		+ ((buffer[2 * i + 1] & 0xFF) << 8);
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
	rc = snd_pcm_open(&sound.handle, SOUND_DEVICE, SND_PCM_STREAM_CAPTURE, 0);
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

	/* Set period size to ("nearly") 1024 frames. */
	sound.frames = 1024;
	snd_pcm_hw_params_set_period_size_near(sound.handle, params,
			&sound.frames, &dir);

	/* Write the parameters to the driver. */
	rc = snd_pcm_hw_params(sound.handle, params);
	if (rc < 0)
	{
		fprintf(stderr, "unable to set hw parameters: %s\n",
				snd_strerror(rc));
		exit(EXIT_FAILURE);
	}

	/* Acquire n frames per turn. */
	sound.frames = SOUND_SAMPLES_PER_TURN;
	size = sound.frames * 2;  /* 2 bytes/sample, 1 channel */
	sound.buffer = (char *)malloc(size);

	sound.reprepare = 0;
}

/* Read one period. */
void audioRead(void)
{
	if (sound.reprepare)
	{
		snd_pcm_prepare(sound.handle);
		sound.reprepare = 0;
	}

	int rc;
	rc = snd_pcm_readi(sound.handle, sound.buffer, sound.frames);
	if (rc == -EPIPE)
	{
		/* EPIPE means overrun */
		fprintf(stderr, "overrun occurred\n");
		snd_pcm_prepare(sound.handle);
	}
	else if (rc < 0)
	{
		fprintf(stderr, "error from read: %s\n", snd_strerror(rc));
	}
	else if (rc != (int)sound.frames)
	{
		fprintf(stderr, "short read, read %d frames\n", rc);
	}
	sound.bufferCountFrames = rc;
}

/* Shutdown audio device. */
void audioDeinit(void)
{
	snd_pcm_drain(sound.handle);
	snd_pcm_close(sound.handle);
	free(sound.buffer);
}

/* Create FFTW-plan, allocate buffers. */
void fftwInit(void)
{
	fftw.outlen = sound.frames / 2;
	fftw.in = (double *)fftw_malloc(sizeof(double) * sound.frames);
	fftw.out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * fftw.outlen);
	fftw.plan = fftw_plan_dft_r2c_1d(sound.frames, fftw.in, fftw.out,
			FFTW_ESTIMATE);

	fftw.currentLine = (double *)malloc(sizeof(double) * fftw.outlen);

	fftw.textureWidth = fftw.outlen;
	fftw.textureHeight = FFTW_HISTORY_SIZE;
	fftw.textureData = (unsigned char *)malloc(sizeof(unsigned char)
			* fftw.textureWidth * fftw.textureHeight * 3);
	memset(fftw.textureData, 0, sizeof(unsigned char)
			* fftw.textureWidth * fftw.textureHeight * 3);
}

/* Free any FFTW resources. */
void fftwDeinit(void)
{
	fftw_destroy_plan(fftw.plan);
	fftw_free(fftw.in);
	fftw_free(fftw.out);
	free(fftw.currentLine);
	free(fftw.textureData);
}

/* Read from audio device and display current buffer. */
void updateDisplay(void)
{
	if (!interaction.update)
		return;

	int i;

	audioRead();

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* Calculate spectrum. */
	for (i = 0; i < sound.bufferCountFrames; i++)
	{
		short int val = getFrame(sound.buffer, i);
		fftw.in[i] = 2 * (double)val / (256 * 256);
	}
	for (i = sound.bufferCountFrames; i < (int)sound.frames; i++)
	{
		fftw.in[i] = 0;
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

	/* Update texture. */
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, fftw.textureHandle);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
			fftw.textureWidth, fftw.textureHeight,
			GL_RGB, GL_UNSIGNED_BYTE, fftw.textureData);
	checkError(__LINE__);

	/* Draw a textured quad. */
	glColor3f(1, 1, 1);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0);  glVertex2f(-1, -0.5);
	glTexCoord2f(1, 0);  glVertex2f( 1, -0.5);
	glTexCoord2f(1, 1);  glVertex2f( 1,  1);
	glTexCoord2f(0, 1);  glVertex2f(-1,  1);
	glEnd();
	glDisable(GL_TEXTURE_2D);

	/* Show current spectrum. */
	float curcol[3] = DISPLAY_SPEC_CURRENT_COLOR;
	glColor3fv(curcol);
	glBegin(GL_LINE_STRIP);
	for (i = 0; i < fftw.outlen; i++)
	{
		double relX = 2 * ((double)i / fftw.outlen) - 1;
		double relY = fftw.currentLine[i];
		relY /= 2;
		relY -= 1;
		glVertex2f(relX, relY);
	}
	glEnd();

	/* Dividing line. */
	glColor3f(1, 1, 1);
	glBegin(GL_LINE);
	glVertex2f(-1, -0.5);
	glVertex2f( 1, -0.5);
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
			exit(EXIT_SUCCESS);

		case ' ':
			interaction.update = !interaction.update;
			sound.reprepare = 1;
			glutPostRedisplay();
			break;
	}
}

/* Create the window, set up callbacks and interaction parameters. */
void displayInit(int argc, char *argv[])
{
	interaction.width = DISPLAY_INITIAL_WIDTH;
	interaction.height = DISPLAY_INITIAL_HEIGHT;
	interaction.update = 1;

	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
	glutInitWindowSize(interaction.width, interaction.height);
	glutCreateWindow("rtspeccy");

	glutDisplayFunc(updateDisplay);
	glutReshapeFunc(reshape);
	glutKeyboardFunc(keyboard);
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
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glDisable(GL_TEXTURE_2D);
}

/* Delete the texture. */
void textureDeinit(void)
{
	glEnable(GL_TEXTURE_2D);
	glDeleteTextures(1, &fftw.textureHandle);
	glDisable(GL_TEXTURE_2D);
}

int main(int argc, char *argv[])
{
	displayInit(argc, argv);
	audioInit();
	fftwInit();
	textureInit();
	glutMainLoop();
	textureDeinit();
	audioDeinit();
	fftwDeinit();

	exit(EXIT_SUCCESS);
}

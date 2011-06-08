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

#include <stdio.h>
#include <stdlib.h>
#include <GL/glut.h>
#include <alsa/asoundlib.h>
#include <endian.h>
#include <fftw3.h>
#include <math.h>

/* Informations about the window, display options. */
struct interactionInfo
{
	int width;
	int height;
} interaction;

/* Global sound info. */
struct soundInfo
{
	snd_pcm_t *handle;
	char *buffer;
	int bufferCountFrames;
	snd_pcm_uframes_t frames;
} sound;

/* Global fftw info. */
struct fftwInfo
{
	double *in;
	fftw_complex *out;
	fftw_plan plan;
	int outlen;

	double *history;
	int historySize;
	int historyCurrent;

	unsigned char *textureData;
	GLuint textureHandle;
	int textureWidth, textureHeight;
} fftw;

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
	rc = snd_pcm_open(&sound.handle, "default", SND_PCM_STREAM_CAPTURE, 0);
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
	val = 44100;
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
	sound.frames = 2048;
	size = sound.frames * 2;  /* 2 bytes/sample, 1 channel */
	sound.buffer = (char *)malloc(size);
}

/* Read one period. */
void audioRead(void)
{
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

	fftw.historySize = 256;
	fftw.historyCurrent = 0;
	fftw.history = (double *)malloc(sizeof(double) * fftw.historySize
			* fftw.outlen);
	memset(fftw.history, 0, sizeof(double) * fftw.historySize
			* fftw.outlen);

	fftw.textureWidth = fftw.outlen;
	fftw.textureHeight = fftw.historySize;
	fftw.textureData = (unsigned char *)malloc(sizeof(unsigned char)
			* fftw.textureWidth * fftw.textureHeight * 3);
}

/* Free any FFTW resources. */
void fftwDeinit(void)
{
	fftw_destroy_plan(fftw.plan);
	fftw_free(fftw.in);
	fftw_free(fftw.out);
	free(fftw.history);
	free(fftw.textureData);
}

/* Read from audio device and display current buffer. */
void updateDisplay(void)
{
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

	/* Add line to history. */
	for (i = 0; i < fftw.outlen; i++)
	{
		double relY = sqrt(fftw.out[i][0] * fftw.out[i][0]
				+ fftw.out[i][1] * fftw.out[i][1]) / (0.0125 * fftw.outlen);
		relY = relY > 1.0 ? 1.0 : relY;
		fftw.history[fftw.historyCurrent * fftw.outlen + i] = relY;
	}

	/* Draw history into a texture. */
	int h, hReal, ta = 0;
	for (h = 0; h < fftw.historySize; h++)
	{
		hReal = fftw.historyCurrent - h;
		hReal = hReal < 0 ? hReal + fftw.historySize : hReal;
		for (i = 0; i < fftw.outlen; i++)
		{
			double val = fftw.history[hReal * fftw.outlen + i];
			fftw.textureData[ta++] = 0;
			fftw.textureData[ta++] = (unsigned char)(val * 255);
			fftw.textureData[ta++] = (unsigned char)(val * 255);
		}
	}

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, fftw.textureHandle);
	glTexImage2D(GL_TEXTURE_2D, 0, 3,
			fftw.textureWidth, fftw.textureHeight, 0,
			GL_RGB, GL_UNSIGNED_BYTE, fftw.textureData);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glColor3f(1, 1, 1);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0);  glVertex2f(-1, -0.5);
	glTexCoord2f(1, 0);  glVertex2f( 1, -0.5);
	glTexCoord2f(1, 1);  glVertex2f( 1,  1);
	glTexCoord2f(0, 1);  glVertex2f(-1,  1);
	glEnd();
	glDisable(GL_TEXTURE_2D);

	/* Show current spectrum. */
	glColor3f(0, 1, 0);
	glBegin(GL_LINE_STRIP);
	for (i = 0; i < fftw.outlen; i++)
	{
		double relX = 2 * ((double)i / fftw.outlen) - 1;
		double relY = fftw.history[fftw.historyCurrent * fftw.outlen + i];
		relY /= 2;
		relY -= 1;
		glVertex2f(relX, relY);
	}
	glEnd();

	/* Go to next line in (circular) history. */
	fftw.historyCurrent++;
	fftw.historyCurrent %= fftw.historySize;

	glutSwapBuffers();
}

/* Simple orthographic projection. */
void reshape(int w, int h)
{
	double ratio = (double)w / h;

	interaction.width = w;
	interaction.height = h;

	glClearColor(0, 0, 0, 1);
	glViewport(0, 0, w, h);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glViewport(0, 0, w, h);
	glOrtho(-ratio, ratio, -1, 1, -4, 4);

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
	}

	glutPostRedisplay();
}

void displayInit(int argc, char *argv[])
{
	interaction.width = 512;
	interaction.height = 512;

	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
	glutInitWindowSize(interaction.width, interaction.height);
	glutCreateWindow("vis");

	glutDisplayFunc(updateDisplay);
	glutReshapeFunc(reshape);
	glutKeyboardFunc(keyboard);
	glutIdleFunc(updateDisplay);

	glEnable(GL_TEXTURE_2D);
	glGenTextures(1, &fftw.textureHandle);
}

int main(int argc, char *argv[])
{
	displayInit(argc, argv);
	audioInit();
	fftwInit();
	glutMainLoop();
	audioDeinit();
	fftwDeinit();

	exit(EXIT_SUCCESS);
}

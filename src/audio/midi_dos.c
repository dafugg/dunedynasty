/** @file src/audio/midi_dos.c MS-DOS MIDI output to an MPU-401 in UART
 *  mode (Sound Blaster 16, Roland MPU-401, SC-55 / MT-32 interfaces).
 *
 *  The port comes from the P field of the BLASTER variable, or 330h.
 *  midi_send is called from the 120 Hz music timer interrupt, so it
 *  only polls the MPU status port, with a timeout. */

#include <ctype.h>
#include <pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "midi.h"

enum {
	MPU_STATUS_DRR = 0x40,      /* Clear when the MPU can take a byte. */
	MPU_STATUS_DSR = 0x80,      /* Clear when the MPU has a byte for us. */
	MPU_CMD_RESET  = 0xFF,
	MPU_CMD_UART   = 0x3F,
	MPU_ACK        = 0xFE,
	MPU_TIMEOUT    = 65536      /* Status polls, about 60 ms on ISA. */
};

static int s_port = 0x330;
static bool s_initialised;

/* DD_MIDILOG: count messages and keep the first few, reported at exit. */
static volatile unsigned long s_log_count;
static volatile uint32 s_log_first[16];
static volatile unsigned long s_log_by_type[8];   /* 0x80 .. 0xF0 */

static void
MPU_ReportLog(void)
{
	printf("DD_MIDILOG: %lu messages sent to MPU-401 at %Xh; first:", s_log_count, s_port);
	for (unsigned long i = 0; i < s_log_count && i < 16; i++)
		printf(" %06lX", (unsigned long)s_log_first[i]);
	printf("\nDD_MIDILOG: by type: note-off %lu, note-on %lu, aftertouch %lu, control %lu, program %lu, pressure %lu, pitch %lu\n",
		s_log_by_type[0], s_log_by_type[1], s_log_by_type[2], s_log_by_type[3],
		s_log_by_type[4], s_log_by_type[5], s_log_by_type[6]);
}

static int
MPU_ParseBlasterPort(void)
{
	const char *blaster = getenv("BLASTER");

	for (const char *p = blaster; p != NULL && *p != '\0'; p++) {
		if (toupper((unsigned char)*p) == 'P' && (p == blaster || p[-1] == ' '))
			return (int)strtol(p + 1, NULL, 16);
	}

	return 0x330;
}

static bool
MPU_WaitWrite(void)
{
	for (int i = 0; i < MPU_TIMEOUT; i++) {
		if ((inportb(s_port + 1) & MPU_STATUS_DRR) == 0)
			return true;
	}

	return false;
}

static bool
MPU_WaitAck(void)
{
	for (int i = 0; i < MPU_TIMEOUT; i++) {
		if ((inportb(s_port + 1) & MPU_STATUS_DSR) == 0) {
			if (inportb(s_port) == MPU_ACK)
				return true;
		}
	}

	return false;
}

static bool
MPU_Command(uint8 cmd)
{
	if (!MPU_WaitWrite())
		return false;

	outportb(s_port + 1, cmd);
	return MPU_WaitAck();
}

bool midi_init(void)
{
	if (s_initialised)
		return true;

	s_port = MPU_ParseBlasterPort();

	/* A reset may not be acknowledged if the MPU is already in UART mode,
	 * so try twice. */
	if (!MPU_Command(MPU_CMD_RESET) && !MPU_Command(MPU_CMD_RESET))
		return false;

	if (!MPU_Command(MPU_CMD_UART))
		return false;

	if (getenv("DD_MIDILOG") != NULL)
		atexit(MPU_ReportLog);

	s_initialised = true;
	return true;
}

void midi_uninit(void)
{
	if (!s_initialised)
		return;

	midi_reset();
	MPU_Command(MPU_CMD_RESET);
	s_initialised = false;
}

static void
MPU_WriteByte(uint8 b)
{
	if (MPU_WaitWrite())
		outportb(s_port, b);
}

void midi_send(uint32 data)
{
	if (!s_initialised)
		return;

	const uint8 status = data & 0xFF;

	if (s_log_count < 16)
		s_log_first[s_log_count] = data;
	s_log_count++;
	if (status >= 0x80)
		s_log_by_type[(status >> 4) - 8]++;

	MPU_WriteByte(status);
	MPU_WriteByte((data >> 8) & 0x7F);

	/* Program change and channel pressure have one data byte. */
	if ((status & 0xE0) != 0xC0)
		MPU_WriteByte((data >> 16) & 0x7F);
}

void midi_reset(void)
{
	if (!s_initialised)
		return;

	for (uint8 chan = 0; chan < 16; chan++) {
		midi_send((0xB0 | chan) | (123 << 8));  /* All notes off. */
		midi_send((0xB0 | chan) | (121 << 8));  /* Reset all controllers. */
	}
}

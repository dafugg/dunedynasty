/* opl_hw.cpp
 *
 * MS-DOS: drive a real OPL2/OPL3 (AdLib, Sound Blaster) instead of an
 * emulator.  Implements the fmopl.h API, so the ADL player is shared
 * with the emulated backends.  YM3812UpdateOne renders nothing: the
 * chip makes the sound.
 */

#include <dos.h>
#include <pc.h>
#include <stdlib.h>

#include "fmopl.h"

namespace OPL {

bool OPL::_hasInstance = false;

class Hardware : public OPL {
public:
	explicit Hardware(int port) : _port(port) { }

	bool init(int rate) { (void)rate; reset(); return true; }

	void reset()
	{
		for (int reg = 0x01; reg <= 0xF5; reg++)
			writeReg(reg, 0);
		writeReg(0x01, 0x20);   /* Enable waveform select. */
	}

	void write(int a, int v)
	{
		outportb(_port + (a & 1), v);
		delayAfter(a & 1);
	}

	byte read(int a)
	{
		return inportb(_port + (a & 1));
	}

	void writeReg(int r, int v)
	{
		outportb(_port, r);
		delayAfter(0);
		outportb(_port + 1, v);
		delayAfter(1);
	}

	void readBuffer(int16 *buffer, int length)
	{
		(void)buffer;
		(void)length;
	}

	bool isStereo() const { return false; }

private:
	/* An OPL2 needs 3.3us after an index write and 23us after a data
	 * write; reading the status port takes about 1us on an ISA bus. */
	void delayAfter(int data)
	{
		const int reads = data ? 35 : 6;
		for (int i = 0; i < reads; i++)
			inportb(_port);
	}

	int _port;
};

} /* namespace OPL */

static int s_opl_port = 0x388;

/**
 * Standard AdLib detection: start timer 1 and check that the status
 * register reports it expiring.
 */
bool
OPLHW_Detect(void)
{
	OPL::Hardware opl(s_opl_port);

	opl.writeReg(0x04, 0x60);   /* Reset both timers. */
	opl.writeReg(0x04, 0x80);   /* Reset the IRQ. */
	const int status1 = opl.read(0) & 0xE0;

	opl.writeReg(0x02, 0xFF);   /* Timer 1 count. */
	opl.writeReg(0x04, 0x21);   /* Start timer 1. */
	delay(1);                   /* At least 80us. */
	const int status2 = opl.read(0) & 0xE0;

	opl.writeReg(0x04, 0x60);
	opl.writeReg(0x04, 0x80);

	return (status1 == 0x00) && (status2 == 0xC0);
}

void OPLDestroy(FM_OPL *OPL) {
	delete OPL;
}

void OPLResetChip(FM_OPL *OPL) {
	OPL->reset();
}

void OPLWrite(FM_OPL *OPL, int a, int v) {
	OPL->write(a, v);
}

unsigned char OPLRead(FM_OPL *OPL, int a) {
	return OPL->read(a);
}

void OPLWriteReg(FM_OPL *OPL, int r, int v) {
	OPL->writeReg(r, v);
}

void YM3812UpdateOne(FM_OPL *OPL, int16_t *buffer, int length) {
	OPL->readBuffer(buffer, length);
}

FM_OPL *makeAdLibOPL(int rate, bool bMAME) {
	(void)bMAME;

	FM_OPL *opl = new OPL::Hardware(s_opl_port);
	opl->init(rate);
	return opl;
}

#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <fomu/csr.h>

#include "spi.h"

#define fprintf(...) do {} while(0)

static uint8_t do_mirror;
static uint8_t oe_mirror;

#define PI_OUTPUT 1
#define PI_INPUT 0
#define PI_ALT0 PI_INPUT
static void gpioSetMode(int pin, int mode) {
    if (mode)
        oe_mirror |= 1 << pin;
    else
        oe_mirror &= ~(1 << pin);
    picorvspi_cfg2_write(oe_mirror);
}

static void gpioWrite(int pin, int val) {
    if (val)
        do_mirror |= 1 << pin;
    else
        do_mirror &= ~(1 << pin);
    picorvspi_cfg1_write(do_mirror);
}

static int gpioRead(int pin) {
    return !!(picorvspi_stat1_read() & (1 << pin));
}

enum ff_spi_quirks {
	// There is no separate "Write SR 2" command.  Instead,
	// you must write SR2 after writing SR1
	SQ_SR2_FROM_SR1    = (1 << 0),

	// Don't issue a "Write Enable" command prior to writing
	// a status register
	SQ_SKIP_SR_WEL     = (1 << 1),

	// Security registers are shifted up by 4 bits
	SQ_SECURITY_NYBBLE_SHIFT = (1 << 2),
};

struct ff_spi {
	enum spi_state state;
	enum spi_type type;
	enum spi_type desired_type;
	struct spi_id id;
	enum ff_spi_quirks quirks;
	int size_override;

	struct {
		union {
			int mosi;
			int d0;
		};
		union {
			int miso;
			int d1;
		};
		union {
			int wp;
			int d2;
		};
		union {
			int hold;
			int d3;
		};
		int clk;
		int cs;
	} pins;
};
static struct ff_spi spi;

static void spi_get_id(void);

static void spi_set_state(enum spi_state state) {
	if (spi.state == state)
		return;

	switch (state) {
	case SS_SINGLE:
		gpioSetMode(spi.pins.clk, PI_OUTPUT); // CLK
		gpioSetMode(spi.pins.cs, PI_OUTPUT); // CE0#
		gpioSetMode(spi.pins.mosi, PI_OUTPUT); // MOSI
		gpioSetMode(spi.pins.miso, PI_INPUT); // MISO
		gpioSetMode(spi.pins.hold, PI_OUTPUT);
		gpioSetMode(spi.pins.wp, PI_OUTPUT);
		break;

	case SS_QUAD_RX:
		gpioSetMode(spi.pins.clk, PI_OUTPUT); // CLK
		gpioSetMode(spi.pins.cs, PI_OUTPUT); // CE0#
		gpioSetMode(spi.pins.mosi, PI_INPUT); // MOSI
		gpioSetMode(spi.pins.miso, PI_INPUT); // MISO
		gpioSetMode(spi.pins.hold, PI_INPUT);
		gpioSetMode(spi.pins.wp, PI_INPUT);
		break;

	case SS_QUAD_TX:
		gpioSetMode(spi.pins.clk, PI_OUTPUT); // CLK
		gpioSetMode(spi.pins.cs, PI_OUTPUT); // CE0#
		gpioSetMode(spi.pins.mosi, PI_OUTPUT); // MOSI
		gpioSetMode(spi.pins.miso, PI_OUTPUT); // MISO
		gpioSetMode(spi.pins.hold, PI_OUTPUT);
		gpioSetMode(spi.pins.wp, PI_OUTPUT);
		break;

	case SS_HARDWARE:
		gpioSetMode(spi.pins.clk, PI_ALT0); // CLK
		gpioSetMode(spi.pins.cs, PI_ALT0); // CE0#
		gpioSetMode(spi.pins.mosi, PI_ALT0); // MOSI
		gpioSetMode(spi.pins.miso, PI_ALT0); // MISO
		gpioSetMode(spi.pins.hold, PI_OUTPUT);
		gpioSetMode(spi.pins.wp, PI_OUTPUT);
		break;

	default:
		fprintf(stderr, "Unrecognized spi state\n");
		return;
	}
	spi.state = state;
}

void spiPause(void) {
//	usleep(1);
	return;
}

void spiBegin(void) {
	spi_set_state(SS_SINGLE);
	if ((spi.type == ST_SINGLE)) {
		gpioWrite(spi.pins.wp, 1);
		gpioWrite(spi.pins.hold, 1);
	}
	gpioWrite(spi.pins.cs, 0);
}

void spiEnd(void) {
	(void)spi;
	gpioWrite(spi.pins.cs, 1);
}

static uint8_t spiXfer(uint8_t out) {
	int bit;
	uint8_t in = 0;

	for (bit = 7; bit >= 0; bit--) {
		if (out & (1 << bit)) {
			gpioWrite(spi.pins.mosi, 1);
		}
		else {
			gpioWrite(spi.pins.mosi, 0);
		}
		gpioWrite(spi.pins.clk, 1);
		spiPause();
		in |= ((!!gpioRead(spi.pins.miso)) << bit);
		gpioWrite(spi.pins.clk, 0);
		spiPause();
	}

	return in;
}

static void spiSingleTx(uint8_t out) {
	spi_set_state(SS_SINGLE);
	spiXfer(out);
}

static uint8_t spiSingleRx(void) {
	spi_set_state(SS_SINGLE);
	return spiXfer(0xff);
}

static void spiQuadTx(uint8_t out) {
	int bit;
	spi_set_state(SS_QUAD_TX);
	for (bit = 7; bit >= 0; bit -= 4) {
		if (out & (1 << (bit - 3))) {
			gpioWrite(spi.pins.d0, 1);
		}
		else {
			gpioWrite(spi.pins.d0, 0);
		}

		if (out & (1 << (bit - 2))) {
			gpioWrite(spi.pins.d1, 1);
		}
		else {
			gpioWrite(spi.pins.d1, 0);
		}

		if (out & (1 << (bit - 1))) {
			gpioWrite(spi.pins.d2, 1);
		}
		else {
			gpioWrite(spi.pins.d2, 0);
		}

		if (out & (1 << (bit - 0))) {
			gpioWrite(spi.pins.d3, 1);
		}
		else {
			gpioWrite(spi.pins.d3, 0);
		}
		gpioWrite(spi.pins.clk, 1);
		spiPause();
		gpioWrite(spi.pins.clk, 0);
		spiPause();
	}
}

static uint8_t spiQuadRx(void) {
	int bit;
	uint8_t in = 0;

	spi_set_state(SS_QUAD_RX);
	for (bit = 7; bit >= 0; bit -= 4) {
		gpioWrite(spi.pins.clk, 1);
		spiPause();
		in |= ((!!gpioRead(spi.pins.d0)) << (bit - 3));
		in |= ((!!gpioRead(spi.pins.d1)) << (bit - 2));
		in |= ((!!gpioRead(spi.pins.d2)) << (bit - 1));
		in |= ((!!gpioRead(spi.pins.d3)) << (bit - 0));
		gpioWrite(spi.pins.clk, 0);
		spiPause();
	}
	return in;
}

int spiTx(uint8_t word) {
	switch (spi.type) {
	case ST_SINGLE:
		spiSingleTx(word);
		break;
	case ST_QUAD:
	case ST_QPI:
		spiQuadTx(word);
		break;
	default:
		return -1;
	}
	return 0;
}

uint8_t spiRx(void) {
	switch (spi.type) {
	case ST_SINGLE:
		return spiSingleRx();
	case ST_QUAD:
	case ST_QPI:
		return spiQuadRx();
	default:
		return 0xff;
	}
}

void spiCommand(uint8_t cmd) {
	if (spi.type == ST_QPI)
		spiQuadTx(cmd);
	else
		spiSingleTx(cmd);
}

uint8_t spiCommandRx(void) {
	if (spi.type == ST_QPI)
		return spiQuadRx();
	else
		return spiSingleRx();
}

uint8_t spiReadStatus(uint8_t sr) {
	uint8_t val = 0xff;
	(void)sr;

	switch (sr) {
	case 1:
	case 2:
		spiBegin();
		spiCommand(0x05);
		val = spiCommandRx();
		if (sr == 2)
			val = spiCommandRx();
		spiEnd();
		break;

	case 3:
		spiBegin();
		spiCommand(0x15);
		val = spiCommandRx();
		spiEnd();
		break;

	default:
		fprintf(stderr, "unrecognized status register: %d\n", sr);
		break;
	}
	return val;
}

void spiWriteSecurity(uint8_t sr, uint8_t security[256]) {

	if (spi.quirks & SQ_SECURITY_NYBBLE_SHIFT)
		sr <<= 4;

	spiBegin();
	spiCommand(0x06);
	spiEnd();

	// erase the register
	spiBegin();
	spiCommand(0x44);
	spiCommand(0x00); // A23-16
	spiCommand(sr);   // A15-8
	spiCommand(0x00); // A0-7
	spiEnd();

	spi_get_id();
	sleep(1);

	// write enable
	spiBegin();
	spiCommand(0x06);
	spiEnd();

	spiBegin();
	spiCommand(0x42);
	spiCommand(0x00); // A23-16
	spiCommand(sr);   // A15-8
	spiCommand(0x00); // A0-7
	int i;
	for (i = 0; i < 256; i++)
		spiCommand(security[i]);
	spiEnd();

	spi_get_id();
}

void spiReadSecurity(uint8_t sr, uint8_t security[256]) {
	if (spi.quirks & SQ_SECURITY_NYBBLE_SHIFT)
		sr <<= 4;

	spiBegin();
	spiCommand(0x48);	// Read security registers
	spiCommand(0x00);  // A23-16
	spiCommand(sr);    // A15-8
	spiCommand(0x00);  // A0-7
	int i;
	for (i = 0; i < 256; i++)
		security[i] = spiCommandRx();
	spiEnd();
}

void spiWriteStatus(uint8_t sr, uint8_t val) {

	switch (sr) {
	case 1:
		if (!(spi.quirks & SQ_SKIP_SR_WEL)) {
			spiBegin();
			spiCommand(0x06);
			spiEnd();
		}

		spiBegin();
		spiCommand(0x50);
		spiEnd();

		spiBegin();
		spiCommand(0x01);
		spiCommand(val);
		spiEnd();
		break;

	case 2: {
		uint8_t sr1 = 0x00;
		if (spi.quirks & SQ_SR2_FROM_SR1)
			sr1 = spiReadStatus(1);

		if (!(spi.quirks & SQ_SKIP_SR_WEL)) {
			spiBegin();
			spiCommand(0x06);
			spiEnd();
		}


		spiBegin();
		spiCommand(0x50);
		spiEnd();

		spiBegin();
		if (spi.quirks & SQ_SR2_FROM_SR1) {
			spiCommand(0x01);
			spiCommand(sr1);
			spiCommand(val);
		}
		else {
			spiCommand(0x31);
			spiCommand(val);
		}
		spiEnd();
		break;
	}

	case 3:
		if (!(spi.quirks & SQ_SKIP_SR_WEL)) {
			spiBegin();
			spiCommand(0x06);
			spiEnd();
		}


		spiBegin();
		spiCommand(0x50);
		spiEnd();

		spiBegin();
		spiCommand(0x11);
		spiCommand(val);
		spiEnd();
		break;

	default:
		fprintf(stderr, "unrecognized status register: %d\n", sr);
		break;
	}
}

struct spi_id spiId(void) {
	return spi.id;
}

#if 0
static void spi_decode_id(void) {
	spi.id.bytes = -1; // unknown
	spi.id.manufacturer = "Unknown";
	spi.id.model = "Unknown";
	spi.id.capacity = "Unknown";

	if (spi.id.manufacturer_id == 0xef) {
		spi.id.manufacturer = "Winbond";
		if ((spi.id.memory_type == 0x70)
		 && (spi.id.memory_size == 0x18)) {
			spi.id.model = "W25Q128JV";
			spi.id.capacity = "128 Mbit";
			spi.id.bytes = 16 * 1024 * 1024;
		}
	}

	if (spi.id.manufacturer_id == 0x1f) {
		spi.id.manufacturer = "Adesto";
		 if ((spi.id.memory_type == 0x86)
		  && (spi.id.memory_size == 0x01)) {
			spi.id.model = "AT25SF161";
			spi.id.capacity = "16 Mbit";
			spi.id.bytes = 1 * 1024 * 1024;
		}
	}
	return;
}
#endif

static void spi_get_id(void) {

	spiBegin();
	spiCommand(0x90);	// Read manufacturer ID
	spiCommand(0x00);  // Dummy byte 1
	spiCommand(0x00);  // Dummy byte 2
	spiCommand(0x00);  // Dummy byte 3
	spi.id.manufacturer_id = spiCommandRx();
	spi.id.device_id = spiCommandRx();
	spiEnd();

	spiBegin();
	spiCommand(0x9f);	// Read device id
	spi.id._manufacturer_id = spiCommandRx();
	spi.id.memory_type = spiCommandRx();
	spi.id.memory_size = spiCommandRx();
	spiEnd();

	spiBegin();
	spiCommand(0xab);	// Read electronic signature
	spiCommand(0x00);  // Dummy byte 1
	spiCommand(0x00);  // Dummy byte 2
	spiCommand(0x00);  // Dummy byte 3
	spi.id.signature = spiCommandRx();
	spiEnd();

	spiBegin();
	spiCommand(0x4b);	// Read unique ID
	spiCommand(0x00);  // Dummy byte 1
	spiCommand(0x00);  // Dummy byte 2
	spiCommand(0x00);  // Dummy byte 3
	spiCommand(0x00);  // Dummy byte 4
	spi.id.serial[0] = spiCommandRx();
	spi.id.serial[1] = spiCommandRx();
	spi.id.serial[2] = spiCommandRx();
	spi.id.serial[3] = spiCommandRx();
	spiEnd();

	// spi_decode_id();
	return;
}

#if 0
void spiOverrideSize(uint32_t size) {
	spi.size_override = size;

	// If size is 0, re-read the capacity
	if (!size)
		spi_decode_id();
	else
		spi.id.bytes = size;
}
#endif

int spiSetType(enum spi_type type) {

	if (spi.type == type)
		return 0;

	switch (type) {

	case ST_SINGLE:
		if (spi.type == ST_QPI) {
			spiBegin();
			spiCommand(0xff);	// Exit QPI Mode
			spiEnd();
		}
		spi.type = type;
		spi_set_state(SS_SINGLE);
		break;

	case ST_QUAD:
		if (spi.type == ST_QPI) {
			spiBegin();
			spiCommand(0xff);	// Exit QPI Mode
			spiEnd();
		}

		// Enable QE bit
		spiWriteStatus(1, spiReadStatus(1) | (1 << 6));

		spi.type = type;
		spi_set_state(SS_QUAD_TX);
		break;

	case ST_QPI:
		// Enable QE bit
		spiWriteStatus(1, spiReadStatus(1) | (1 << 6));

		spiBegin();
		spiCommand(0x38);		// Enter QPI Mode
		spiEnd();
		spi.type = type;
		spi_set_state(SS_QUAD_TX);
		break;

	default:
		return 1;
	}
	return 0;
}

int spiIsBusy(void) {
  	return spiReadStatus(1) & (1 << 0);
}

void spi_wait_for_not_busy(void) {
	while (spiIsBusy())
		;
}

int spiBeginErase32(uint32_t erase_addr) {
	// Enable Write-Enable Latch (WEL)
	spiBegin();
	spiCommand(0x06);
	spiEnd();

	spiBegin();
	spiCommand(0x52);
	spiCommand(erase_addr >> 16);
	spiCommand(erase_addr >> 8);
	spiCommand(erase_addr >> 0);
	spiEnd();
	return 0;
}

int spiBeginErase64(uint32_t erase_addr) {
	// Enable Write-Enable Latch (WEL)
	spiBegin();
	spiCommand(0x06);
	spiEnd();

	spiBegin();
	spiCommand(0xD8);
	spiCommand(erase_addr >> 16);
	spiCommand(erase_addr >> 8);
	spiCommand(erase_addr >> 0);
	spiEnd();
	return 0;
}

int spiBeginWrite(uint32_t addr, const void *v_data, unsigned int count) {
	const uint8_t write_cmd = 0x02;
	const uint8_t *data = v_data;
	unsigned int i;

	// Enable Write-Enable Latch (WEL)
	spiBegin();
	spiCommand(0x06);
	spiEnd();

	// uint8_t sr1 = spiReadStatus(1);
	// if (!(sr1 & (1 << 1)))
	// 	fprintf(stderr, "error: write-enable latch (WEL) not set, write will probably fail\n");

	spiBegin();
	spiCommand(write_cmd);
	spiCommand(addr >> 16);
	spiCommand(addr >> 8);
	spiCommand(addr >> 0);
	for (i = 0; (i < count) && (i < 256); i++)
		spiTx(*data++);
	spiEnd();

	return 0;
}

uint8_t spiReset(void) {
	// XXX You should check the "Ready" bit before doing this!

	spiSetType(ST_SINGLE);

	spiBegin();
	spiCommand(0x66); // "Enable Reset" command
	spiEnd();

	spiBegin();
	spiCommand(0x99); // "Reset Device" command
	spiEnd();

	// msleep(30);

	spiBegin();
	spiCommand(0xab); // "Resume from Deep Power-Down" command
	spiEnd();

	return 0;
}

int spi_init(void) {
	spi.state = SS_UNCONFIGURED;
	spi.type = ST_UNCONFIGURED;

	int i;
	for (i = 0; i < 6; i++)
		((uint32_t *)&spi.pins)[i] = i;

	// Disable memory-mapped mode and enable bit-bang mode
	picorvspi_cfg4_write(0);

	// Reset the SPI flash, which will return it to SPI mode even
	// if it's in QPI mode.
	spiReset();

	spiSetType(ST_SINGLE);

	// Have the SPI flash pay attention to us
	gpioWrite(spi.pins.hold, 1);

	// Disable WP
	gpioWrite(spi.pins.wp, 1);

	gpioSetMode(spi.pins.clk, PI_OUTPUT); // CLK
	gpioSetMode(spi.pins.cs, PI_OUTPUT); // CE0#
	gpioSetMode(spi.pins.mosi, PI_OUTPUT); // MOSI
	gpioSetMode(spi.pins.miso, PI_INPUT); // MISO
	gpioSetMode(spi.pins.hold, PI_OUTPUT);
	gpioSetMode(spi.pins.wp, PI_OUTPUT);

	spi_get_id();

	spi.quirks |= SQ_SR2_FROM_SR1;
	if (spi.id.manufacturer_id == 0xef)
		spi.quirks |= SQ_SKIP_SR_WEL | SQ_SECURITY_NYBBLE_SHIFT;

	return 0;
}

void spiEnableQuad(void) {
	spiWriteStatus(1, spiReadStatus(1) | (1 << 6));
}

void spiSetPin(enum spi_pin pin, int val) {
	switch (pin) {
	case SP_MOSI: spi.pins.mosi = val; break;
	case SP_MISO: spi.pins.miso = val; break;
	case SP_HOLD: spi.pins.hold = val; break;
	case SP_WP: spi.pins.wp = val; break;
	case SP_CS: spi.pins.cs = val; break;
	case SP_CLK: spi.pins.clk = val; break;
	case SP_D0: spi.pins.d0 = val; break;
	case SP_D1: spi.pins.d1 = val; break;
	case SP_D2: spi.pins.d2 = val; break;
	case SP_D3: spi.pins.d3 = val; break;
	}
}

void spiHold(void) {
	spiBegin();
	spiCommand(0xb9);
	spiEnd();
}

void spiUnhold(void) {
	spiBegin();
	spiCommand(0xab);
	spiEnd();
}

int spiWrite(uint32_t addr, const uint8_t *data, unsigned int count) {

	unsigned int i;

	if (addr & 0xff) {
		return 1;
	}

	// Erase all applicable blocks
	uint32_t erase_addr;
	for (erase_addr = 0; erase_addr < count; erase_addr += 32768) {
		spiBegin();
		spiCommand(0x06);
		spiEnd();

		spiBegin();
		spiCommand(0x52);
		spiCommand(erase_addr >> 16);
		spiCommand(erase_addr >> 8);
		spiCommand(erase_addr >> 0);
		spiEnd();

		spi_wait_for_not_busy();
	}

	uint8_t write_cmd;
	switch (spi.type) {
	case ST_SINGLE:
	case ST_QPI:
		write_cmd = 0x02;
		break;
	case ST_QUAD:
		write_cmd = 0x32;
		break;
	default:
		return 1;
	}

	while (count) {
		spiBegin();
		spiCommand(0x06);
		spiEnd();

		spiBegin();
		spiCommand(write_cmd);
		spiCommand(addr >> 16);
		spiCommand(addr >> 8);
		spiCommand(addr >> 0);
		for (i = 0; (i < count) && (i < 256); i++)
			spiTx(*data++);
		spiEnd();
		count -= i;
		addr += i;
		spi_wait_for_not_busy();
	}
	return 0;
}

int spiRead(uint32_t addr, uint8_t *data, unsigned int count) {

	unsigned int i;

	spiBegin();
	switch (spi.type) {
	case ST_SINGLE:
	case ST_QPI:
		spiCommand(0x0b);
		break;
	case ST_QUAD:
		spiCommand(0x6b);
		break;
	default:
		spiEnd();
		return 1;
	}
	spiCommand(addr >> 16);
	spiCommand(addr >> 8);
	spiCommand(addr >> 0);
	spiCommand(0x00);
	for (i = 0; i < count; i++) {
		data[i] = spiRx();
	}

	spiEnd();
	return 0;
}

void spiFree(void) {
	// Re-enable memory-mapped mode
	picorvspi_cfg4_write(0x80);
}

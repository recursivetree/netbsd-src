/* $NetBSD$ */

/*-
 * Copyright (c) 2026 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Yuri Honegger.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * SPI controller on the TI AM18XX SoCs.
 */

#include <sys/param.h>
#include <sys/cdefs.h>
#include <sys/device.h>

#include <dev/fdt/fdtvar.h>

struct am18xx_spi_softc {
	bus_space_tag_t sc_bst;
	bus_space_handle_t sc_bsh;
	struct spi_controller sc_spi;
};

static int	am18xx_spi_match(device_t, cfdata_t, void *);
static void	am18xx_spi_attach(device_t, device_t, void *);
static int	am18xx_spi_configure(void *, int, int, int);
static int	am18xx_spi_transfer(void *, struct spi_transfer *);
static void	am18xx_spi_init(struct am18xx_spi_softc *sc);

CFATTACH_DECL_NEW(am18xxspi, sizeof(struct am18xx_spi_softc),
    am18xx_spi_match, am18xx_spi_attach, NULL, NULL);

#define AM18XX_SPI_SPIGCR0	0x0
#define AM18XX_SPI_SPIGCR1	0x4

#define AM18XX_SPI_SPIGCR1_MASTER_MODE	0x3

#define	SPI_READ(sc, reg)					\
	bus_space_read_4((sc)->sc_bst, (sc)->sc_bsh, reg)
#define	SPI_WRITE(sc, reg, val)					\
	bus_space_write_4((sc)->sc_bst, (sc)->sc_bsh, reg, val)

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "ti,da830-spi" },
	DEVICE_COMPAT_EOL
};

static int
am18xx_spi_configure(void *cookie, int slave, int mode, int speed)
{
	// TODO: mode
	// TODO: speed
	// spi_clock = input_clock/(prescaler+1)

	return -1;
}

static int
am18xx_spi_transfer(void *cookie, struct spi_transfer *transfer)
{
	// AM18XX_SPI_SPIGCR1 enable needs to be set

	return -1;
}

static void
am18xx_spi_init(struct am18xx_spi_softc *sc)
{
	/* reset the module */
	SPI_WRITE(sc, AM18XX_SPI_SPIGCR0, 0);
	SPI_WRITE(sc, AM18XX_SPI_SPIGCR0, 1);

	SPI_WRITE(sc, AM18XX_SPI_SPIGCR1, AM18XX_SPI_SPIGCR1_MASTER_MODE);
}

int
am18xx_spi_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

void
am18xx_spi_attach(device_t parent, device_t self, void *aux)
{
	struct am18xx_spi_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
	bus_addr_t addr;
	bus_size_t size;

	sc->sc_bst = faa->faa_bst;

	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0) {
		aprint_error(": couldn't get registers\n");
		return;
	}
	if (bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_bsh)) {
		aprint_error(": couldn't map registers\n");
		return;
	}

	if (fdtbus_clock_enable(phandle, "gpio", true) != 0) {
		aprint_error(": couldn't enable clock\n");
		return;
	}

	sc->sc_spi.sct_cookie = sc;
	sc->sc_spi.sct_configure = am18xx_spi_configure;
	sc->sc_spi.sct_transfer = am18xx_spi_transfer;

	if (of_getprop_uint32(phandle, "num-cs", &sc->sc_spi.sct_nslaves)) {
		aprint_error(": couldn't get CS count\n");
		return;
	}

	am18xx_spi_init(sc);

	spibus_attach(self, &sc->sc_spi);

	aprint_naive("\n");
	aprint_normal("\n");
}


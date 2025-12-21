/* $NetBSD$ */

/*-
 * Copyright (c) 2025 The NetBSD Foundation, Inc.
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
 * Clock control driver for the iMX23. Needed to enable some clock gates.
 */

#include <sys/device.h>

#include <dev/clk/clk_backend.h>
#include <dev/fdt/fdtvar.h>

#include <arm/fdt/arm_fdtvar.h>
#include <arm/imx/imx23var.h>
#include <arm/imx/imx23_clkctrlvar.h>

static int imx23clkctrl_fdt_match(device_t, cfdata_t, void *);
static void imx23clkctrl_fdt_attach(device_t, device_t, void *);

static struct clk *imx23_clockctrl_decode(device_t, int, const void *, size_t);

CFATTACH_DECL_NEW(imx23clkctrl_fdt, sizeof(struct clkctrl_softc),
		  imx23clkctrl_fdt_match, imx23clkctrl_fdt_attach, NULL, NULL);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "fsl,imx23-clkctrl" },
	{ .compat = "fsl,clkctrl" },
	DEVICE_COMPAT_EOL
};

struct fdtbus_clock_controller_func imx23_clockctrl_fdt_funcs = {
	.decode = imx23_clockctrl_decode,
};

static struct clk *
imx23_clockctrl_decode(device_t dev, int phandle, const void *data, size_t len)
{
	struct clkctrl_softc * const sc = device_private(dev);
	const u_int *cells = data;

	if(len != sizeof(u_int)) {
		return NULL;
	}

	/* See the device tree docs for the full mapping */
	uint32_t clk_num = be32toh(cells[0]);
	switch (clk_num) {
	case 40:
		return &sc->sc_clks[IMX23_USB_CLK].clk;
	case 41:
		return &sc->sc_clks[IMX23_USBPHY_CLK].clk;
	default:
		return NULL;
	}
}

static int
imx23clkctrl_fdt_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
imx23clkctrl_fdt_attach(device_t parent, device_t self, void *aux)
{
	struct clkctrl_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;

	sc->sc_dev = self;
	sc->sc_iot = faa->faa_bst;

	/* Map the clkctrl block */
	bus_addr_t addr;
	bus_size_t size;
	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0) {
		aprint_error(": couldn't get register address\n");
		return;
	}
	if (bus_space_map(faa->faa_bst, addr, size, 0, &sc->sc_hdl)) {
		aprint_error(": couldn't map registers\n");
		return;
	}

	aprint_normal("\n");

	clkctrl_attach_common(sc);

	fdtbus_register_clock_controller(self, phandle,
					 &imx23_clockctrl_fdt_funcs);
}
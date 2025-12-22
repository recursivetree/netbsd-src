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
 * Audio output driver for the i.MX23.
 */

#include <sys/device.h>

#include <dev/fdt/fdtvar.h>

#include <arm/fdt/arm_fdtvar.h>
#include <arm/imx/imx23var.h>
#include <arm/imx/imx23_apbdmavar.h>
#include <arm/imx/imx23_digfiltvar.h>


static int imx23digfilt_fdt_match(device_t, cfdata_t, void *);
static void imx23digfilt_fdt_attach(device_t, device_t, void *);
static struct apbdma_softc *imx23digfilt_fdt_get_dma_controller(void);

CFATTACH_DECL_NEW(imx23digfilt_fdt, sizeof(struct digfilt_softc),
		  imx23digfilt_fdt_match, imx23digfilt_fdt_attach, NULL, NULL);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "fsl,imx23-audioout" },
	DEVICE_COMPAT_EOL
};

static int
imx23digfilt_fdt_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
imx23digfilt_fdt_attach(device_t parent, device_t self, void *aux)
{
	struct digfilt_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;

	sc->sc_dev = self;
	sc->sc_iot = faa->faa_bst;
	sc->sc_dmat = faa->faa_dmat;
	sc->sc_dmac = imx23digfilt_fdt_get_dma_controller();

	if(sc->sc_dmac == NULL) {
		aprint_error(": couldn't get dma controller\n");
		return;
	}

	bus_addr_t addr;
	bus_size_t size;
	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0) {
		aprint_error(": couldn't get register address\n");
		return;
	}
	/* Map DIGFILT bus space. */
	if (bus_space_map(faa->faa_bst, addr, size, 0, &sc->sc_hdl)) {
		aprint_error(": couldn't map registers\n");
		return;
	}
	/* Map AUDIOOUT subregion from parent bus space. */
	if (bus_space_subregion(sc->sc_iot, sc->sc_hdl, 0, size,
				&sc->sc_aohdl)) {
		aprint_error_dev(sc->sc_dev,
				 "Unable to submap AUDIOOUT bus space\n");
		return;
	}

	aprint_normal("\n");

	digfilt_attach_common(sc, self);
}

/*
 * The DMA subsystem of the imx23 expects that we pass it an apbdma_softc.
 * Sharing that softc was done via a custom field on the apb bus attach args.
 * Unfortunately, this is no longer possible when working with device trees.
 * As long as we want to keep the non-fdt port alive and share most of the code,
 * it will be hard to refactor so we can use a more fdt-native way. In the
 * meantime, we can get the softc by searching for the DMA controller device.
 * */
static struct apbdma_softc *
imx23digfilt_fdt_get_dma_controller(void) {
	int i = 0;
	device_t dev;
	struct apbdma_softc *sc;
	while (true) {
		dev = device_find_by_driver_unit("imx23apbdma", i++);
		if (dev == NULL) {
			return NULL;
		}
		sc = device_private(dev);
		if (sc->flags & F_APBX_DMA) {
			return sc;
		}
	}
}
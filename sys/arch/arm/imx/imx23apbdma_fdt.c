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
 * Driver for the DMA controller of the APBH and APBX peripheral buses on the
 * imx23.
 */

#include <sys/device.h>
#include <sys/mutex.h>

#include <dev/fdt/fdtvar.h>

#include <arm/fdt/arm_fdtvar.h>
#include <arm/imx/imx23_apbdmavar.h>
#include <arm/imx/imx23var.h>

static int imx23apbdma_fdt_match(device_t, cfdata_t, void *);
static void imx23apbdma_fdt_attach(device_t, device_t, void *);


struct imx23apbdma_fdt_config {
	u_int flags;
};

CFATTACH_DECL_NEW(imx23apbdma_fdt, sizeof(struct apbdma_softc),
		  imx23apbdma_fdt_match, imx23apbdma_fdt_attach, NULL, NULL);

static const struct imx23apbdma_fdt_config apbh_config = {
	.flags = F_APBH_DMA,
};
static const struct imx23apbdma_fdt_config apbx_config = {
	.flags = F_APBX_DMA,
};

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "fsl,imx23-dma-apbh", .data = &apbh_config },
	{ .compat = "fsl,imx23-dma-apbx", .data = &apbx_config },
	DEVICE_COMPAT_EOL
};

static int
imx23apbdma_fdt_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args *const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
imx23apbdma_fdt_attach(device_t parent, device_t self, void *aux)
{
	struct apbdma_softc *const sc = device_private(self);
	struct fdt_attach_args *const faa = aux;
	const int phandle = faa->faa_phandle;
	const struct imx23apbdma_fdt_config *config;

	// find out if we are for APBH or APBX
	config = of_compatible_lookup(phandle, compat_data)->data;
	sc->flags = config->flags;

	sc->sc_dev = self;
	sc->sc_iot = faa->faa_bst;
	sc->sc_dmat = faa->faa_dmat;

	/* Map control registers */
	bus_addr_t addr;
	bus_size_t size;
	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0) {
		aprint_error(": couldn't get register address\n");
		return;
	}
	if (bus_space_map(faa->faa_bst, addr, size, 0, &sc->sc_ioh)) {
		aprint_error(": couldn't map registers\n");
		return;
	}

	apbdma_reset(sc);
	apbdma_init(sc);

	/* Initialize mutex to control concurrent access from the drivers. */
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_HIGH);

	if (sc->flags & F_APBH_DMA) {
		aprint_normal(": type=apbh\n");
	} else if (sc->flags & F_APBX_DMA) {
		aprint_normal(": type=apbx\n");
	}
}
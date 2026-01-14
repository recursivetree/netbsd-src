/* $Id: imx23_clkctrl.c,v 1.4 2025/10/02 06:51:15 skrll Exp $ */

/*
* Copyright (c) 2013 The NetBSD Foundation, Inc.
* All rights reserved.
*
* This code is derived from software contributed to The NetBSD Foundation
* by Petri Laakso.
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

#include <sys/param.h>
#include <sys/types.h>
#include <sys/bus.h>
#include <sys/cdefs.h>
#include <sys/device.h>
#include <sys/errno.h>

#include <dev/clk/clk_backend.h>

#include <arm/imx/imx23_clkctrlreg.h>
#include <arm/imx/imx23_clkctrlvar.h>
#include <arm/imx/imx23_digctlreg.h>
#include <arm/imx/imx23_digctlvar.h>
#include <arm/imx/imx23var.h>

static int	clkctrl_match(device_t, cfdata_t, void *);
static void	clkctrl_attach(device_t, device_t, void *);
static int	clkctrl_activate(device_t, enum devact);

static void     clkctrl_init(struct clkctrl_softc *);
static void 	clkctrl_create_clkgate(struct clkctrl_softc *, u_int,
		       const char *, bus_addr_t, bus_addr_t, u_int, bool);

static struct clk *clkctrl_get(void *, const char *);
static u_int 	clkctrl_get_rate(void *, struct clk *);
static int 	clkctrl_enable(void *, struct clk *);
static int 	clkctrl_disable(void *, struct clk *);

static struct clkctrl_softc *_sc = NULL;

struct clk_funcs clkctrl_funcs = {
	.enable = clkctrl_enable,
	.disable = clkctrl_disable,
	.get = clkctrl_get,
	.get_rate = clkctrl_get_rate,
};

CFATTACH_DECL3_NEW(imx23clkctrl,
        sizeof(struct clkctrl_softc),
        clkctrl_match,
        clkctrl_attach,
        NULL,
        clkctrl_activate,
        NULL,
        NULL,
        0
);

#define CLKCTRL_RD(sc, reg)                                                 \
        bus_space_read_4(sc->sc_iot, sc->sc_hdl, (reg))
#define CLKCTRL_WR(sc, reg, val)                                            \
        bus_space_write_4(sc->sc_iot, sc->sc_hdl, (reg), (val))

#define CLKCTRL_SOFT_RST_LOOP 455 /* At least 1 us ... */

static int
clkctrl_match(device_t parent, cfdata_t match, void *aux)
{
	struct apb_attach_args *aa = aux;

	if ((aa->aa_addr == HW_CLKCTRL_BASE) &&
	    (aa->aa_size == HW_CLKCTRL_SIZE))
		return 1;

	return 0;
}

static void
clkctrl_attach(device_t parent, device_t self, void *aux)
{
	struct clkctrl_softc *sc = device_private(self);
	struct apb_attach_args *aa = aux;
	static int clkctrl_attached = 0;

	sc->sc_dev = self;
	sc->sc_iot = aa->aa_iot;

	if (clkctrl_attached) {
		aprint_error_dev(sc->sc_dev, "already attached\n");
		return;
	}

	if (bus_space_map(sc->sc_iot, aa->aa_addr, aa->aa_size, 0,
	    &sc->sc_hdl))
	{
		aprint_error_dev(sc->sc_dev, "Unable to map bus space\n");
		return;
	}

	clkctrl_attach_common(sc);

	aprint_normal("\n");

	clkctrl_attached = 1;
}
void
clkctrl_attach_common(struct clkctrl_softc *sc) {

	clkctrl_init(sc);

	sc->sc_clk_domain.name = device_xname(sc->sc_dev);
	sc->sc_clk_domain.funcs = &clkctrl_funcs;
	sc->sc_clk_domain.priv = sc;

	clkctrl_create_clkgate(sc, IMX23_USB_CLK, "usb", HW_DIGCTL_CTRL_CLR,
			       HW_DIGCTL_CTRL_SET, HW_DIGCTL_CTRL_USB_CLKGATE,
			       true);
	clkctrl_create_clkgate(sc, IMX23_USBPHY_CLK, "usbphy",
			       HW_CLKCTRL_PLLCTRL0_SET, HW_CLKCTRL_PLLCTRL0_CLR,
			       HW_CLKCTRL_PLLCTRL0_EN_USB_CLKS, false);
	clkctrl_create_clkgate(sc, IMX23_FILT_CLK, "xtal_filt",
			       HW_CLKCTRL_XTAL_CLR, HW_CLKCTRL_XTAL_SET,
			       HW_CLKCTRL_XTAL_FILT_CLK24M_GATE, false);

	return;
}

static int
clkctrl_activate(device_t self, enum devact act)
{
	return EOPNOTSUPP;
}

static void
clkctrl_create_clkgate(struct clkctrl_softc *sc, u_int index, const char *name,
		       bus_addr_t enable_reg, bus_addr_t disable_reg,
		       u_int bitfield, bool is_digctl)
{
	sc->sc_clks[index] = (struct clkctrl_clk){
		.clk = {
		    .name = name,
		    .domain = &sc->sc_clk_domain,
		},
		.enable_reg = enable_reg,
		.disable_reg = disable_reg,
		.bitfield = bitfield,
		.is_digctl = is_digctl,
	};

	clk_attach(&sc->sc_clks[index].clk);
}

static struct clk *
clkctrl_get(void *priv, const char *name)
{
	struct clkctrl_softc * const sc = priv;

	for (size_t i = 0; i < IMX23_NUM_CLK; i++) {
		if (strcmp(name, sc->sc_clks[i].clk.name) == 0)
			return &sc->sc_clks[i].clk;
	}

	return NULL;
}

static u_int
clkctrl_get_rate(void *priv, struct clk *clk)
{
	return 0;
}

static int
clkctrl_enable(void *priv, struct clk *raw)
{
	struct clkctrl_softc * const sc = priv;
	struct clkctrl_clk *clk = (struct clkctrl_clk *) raw;

	if(clk->is_digctl){
		digctl_clkgate_write(clk->enable_reg, clk->bitfield);
	} else {
		CLKCTRL_WR(sc, clk->enable_reg, clk->bitfield);
	}

	return 0;
}

static int
clkctrl_disable(void *priv, struct clk *raw)
{
	struct clkctrl_softc * const sc = priv;
	struct clkctrl_clk *clk = (struct clkctrl_clk *) raw;

	CLKCTRL_WR(sc, clk->disable_reg, clk->bitfield);

	return 0;
}

static void
clkctrl_init(struct clkctrl_softc *sc)
{
	_sc = sc;
	return;
}

/*
 * Power up 8-phase PLL outputs for USB PHY
 *
 */
void
clkctrl_en_usbphy(void)
{
	struct clkctrl_softc *sc = _sc;

        if (sc == NULL) {
                aprint_error("clkctrl is not initialized");
                return;
        }

	struct clk *usb_clk = &sc->sc_clks[IMX23_USBPHY_CLK].clk;
	clk_enable(usb_clk);

	return;
}

/*
 * Enable 24MHz clock for the Digital Filter.
 *
 */
void
clkctrl_en_filtclk(void)
{
	struct clkctrl_softc *sc = _sc;

	if (sc == NULL) {
		aprint_error("clkctrl is not initialized");
		return;
	}

	struct clk *filt_clk = &sc->sc_clks[IMX23_FILT_CLK].clk;
	clk_enable(filt_clk);

	return;
}

/*
 * Control USB controller clocks.
 */
void
clkctrl_en_usbc_clkgate(int value)
{
	struct clkctrl_softc *sc = _sc;

	if (sc == NULL) {
		aprint_error("clkctrl is not initialized");
		return;
	}

	struct clk *usb_clk = &sc->sc_clks[IMX23_USB_CLK].clk;
	clk_enable(usb_clk);

	return;
}

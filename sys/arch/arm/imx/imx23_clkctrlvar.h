/* $Id: imx23_clkctrlvar.h,v 1.2 2015/01/10 12:13:56 jmcneill Exp $ */

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

#ifndef _ARM_IMX_IMX23_CLKCTRLVAR_H_
#define _ARM_IMX_IMX23_CLKCTRLVAR_H_

#include <sys/bus.h>
#include <sys/device.h>

#include <dev/clk/clk_backend.h>

#define IMX23_USB_CLK 		0
#define IMX23_USBPHY_CLK 	1
#define IMX23_FILT_CLK 		2
#define IMX23_NUM_CLK 		3

struct clkctrl_clk {
	struct clk clk; /* must stay first member */
	bus_size_t enable_reg;
	bus_size_t disable_reg;
	uint32_t bitfield;
	bool is_digctl;
};

struct clkctrl_softc {
	device_t sc_dev;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_hdl;
	struct clk_domain sc_clk_domain;
	struct clkctrl_clk sc_clks[IMX23_NUM_CLK];
};

void clkctrl_attach_common(struct clkctrl_softc *);
void clkctrl_en_usbphy(void);
void clkctrl_en_filtclk(void);
void clkctrl_en_usbc_clkgate(int);

#endif /* !_ARM_IMX_IMX23_CLKCTRLVAR_H_ */

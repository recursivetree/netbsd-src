/* $NetBSD $ */

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

#include "opt_console.h"

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD $");

#include <dev/fdt/fdtvar.h>
#include <dev/fdt/fdt_platform.h>

#include <arm/fdt/arm_fdtvar.h>

#include <uvm/uvm_extern.h>

#include <dev/ic/comreg.h>

#include <arch/evbarm/fdt/platform.h>

/*
 * Platform code for the TI AM18XX family of SOCs (AM1806, AM1808). In linux
 * land and in the device trees, this platform is sometimes also called DA830
 * and DA850 (Davinci 8XX) because their silicon has a lot in common.
 * */

#define AM18XX_IO_VBASE KERNEL_IO_VBASE
#define AM18XX_IO_PBASE 0x01c00000
#define AM18XX_IO_SIZE 0x00400000
#define AM18XX_INTC_VBASE (AM18XX_IO_VBASE + AM18XX_IO_SIZE)
#define AM18XX_INTC_PBASE 0xfffee000
#define AM18XX_INTC_SIZE 0x2000


void am18xx_platform_early_putchar(char);

extern struct arm32_bus_dma_tag arm_generic_dma_tag;
extern struct bus_space arm_generic_bs_tag;

void __noasan
am18xx_platform_early_putchar(char c)
{
#ifdef CONSADDR
#define CONSADDR_VA (CONSADDR - AM18XX_IO_PBASE + KERNEL_IO_VBASE)
	volatile uint32_t *uartaddr = cpu_earlydevice_va_p()
					  ? (volatile uint32_t *)CONSADDR_VA
					  : (volatile uint32_t *)CONSADDR;

	while ((le32toh(uartaddr[com_lsr]) & LSR_TXRDY) == 0)
		;

	uartaddr[com_data] = htole32(c);
#endif
}


static const struct pmap_devmap *
am18xx_platform_devmap(void)
{
	static const struct pmap_devmap devmap[] = {
		/* input/output registers */
		DEVMAP_ENTRY(AM18XX_IO_VBASE,
			     AM18XX_IO_PBASE,
			     AM18XX_IO_SIZE),
		/* interrupt controller */
		DEVMAP_ENTRY(AM18XX_INTC_VBASE,
			     AM18XX_INTC_PBASE,
			     AM18XX_INTC_SIZE),
		DEVMAP_ENTRY_END
	};

	return devmap;
}

static void
am18xx_platform_init_attach_args(struct fdt_attach_args *faa)
{
	faa->faa_bst = &arm_generic_bs_tag;
	faa->faa_dmat = &arm_generic_dma_tag;
}

static void
am18xx_platform_delay(u_int n)
{
	panic("am18xx_platform_delay not implemented");
}

static void
am18xx_platform_reset(void)
{
	panic("am18xx_platform_reset not implemented");
}

static const struct fdt_platform am18xx_platform = {
	.fp_devmap = am18xx_platform_devmap,
	.fp_bootstrap = arm_fdt_cpu_bootstrap,
	.fp_init_attach_args = am18xx_platform_init_attach_args,
	.fp_uart_freq = NULL,
	.fp_delay = am18xx_platform_delay,
	.fp_reset = am18xx_platform_reset,
};

FDT_PLATFORM(am18xx, "ti,da850", &am18xx_platform);

/* $NetBSD $ */

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

#include <sys/bus.h>
#include <sys/device.h>
#include <sys/mutex.h>

#include <arm/imx/imx23_apbdmavar.h>

typedef struct issp_softc {
	device_t sc_dev;
	apbdma_softc_t sc_dmac;
	bus_dma_tag_t sc_dmat;
	bus_dmamap_t sc_dmamp;
	bus_size_t sc_chnsiz;
	bus_dma_segment_t sc_ds[1];
	int sc_rseg;
	bus_space_handle_t sc_hdl;
	bus_space_tag_t sc_iot;
	device_t sc_sdmmc;
	kmutex_t sc_lock;
	struct kcondvar sc_intr_cv;
	unsigned int dma_channel;
	uint32_t sc_dma_error;
	uint32_t sc_irq_error;
	uint8_t sc_state;
	uint8_t sc_bus_width;
} *issp_softc_t;

int issp_attach_common(struct issp_softc *,  bus_addr_t);
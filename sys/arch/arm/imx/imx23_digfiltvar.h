/* $Id: imx23_digfiltvar.h,v 1.1 2015/01/10 12:16:28 jmcneill Exp $ */

/*
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
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

#ifndef _ARM_IMX_IMX23_AUDIOOUTVAR_H_
#define _ARM_IMX_IMX23_AUDIOOUTVAR_H_

#include <arm/imx/imx23_apbdmavar.h>

#define DIGFILT_DMA_NSEGS 1

struct digfilt_softc {
	device_t sc_dev;
	device_t sc_audiodev;
	struct audio_format sc_format;
	bus_space_handle_t sc_aohdl;
	apbdma_softc_t sc_dmac;
	bus_dma_tag_t sc_dmat;
	bus_dmamap_t sc_dmamp;
	bus_dmamap_t sc_c_dmamp;
	bus_dma_segment_t sc_ds[DIGFILT_DMA_NSEGS];
	bus_dma_segment_t sc_c_ds[DIGFILT_DMA_NSEGS];
	bus_space_handle_t sc_hdl;
	kmutex_t sc_intr_lock;
	bus_space_tag_t	sc_iot;
	kmutex_t sc_lock;
	audio_params_t sc_pparam;
	void *sc_buffer;
	void *sc_dmachain;
	void *sc_intarg;
	void (*sc_intr)(void*);
	uint8_t sc_mute;
	uint8_t sc_cmd_index;
};

void digfilt_attach_common(struct digfilt_softc *,  device_t);

#endif /* !_ARM_IMX_IMX23_AUDIOOUTVAR_H_ */

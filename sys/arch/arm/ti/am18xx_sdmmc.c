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

/*
 * Driver for the sd card port on the TI AM18XX.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/cdefs.h>
#include <sys/device.h>
#include <sys/mutex.h>

#include <dev/fdt/fdtvar.h>
#include <dev/sdmmc/sdmmcchip.h>
#include <dev/sdmmc/sdmmcreg.h>
#include <dev/sdmmc/sdmmcvar.h>

#include <arm/ti/ti_edma.h>

#define	SDMMC_READ(sc, reg)					\
	bus_space_read_4((sc)->sc_bst, (sc)->sc_bsh, reg)
#define	SDMMC_WRITE(sc, reg, val)				\
	bus_space_write_4((sc)->sc_bst, (sc)->sc_bsh, reg, val)

#define AM18XX_SDMMC_MMCCTL	0x0
#define AM18XX_SDMMC_MMCCLK	0x4
#define AM18XX_SDMMC_MMCST0	0x8
#define AM18XX_SDMMC_MMCST1	0xC
#define AM18XX_SDMMC_MMCIM	0x10
#define AM18XX_SDMMC_MMCTOR	0x14
#define AM18XX_SDMMC_MMCTOD	0x18
#define AM18XX_SDMMC_MMCBLEN	0x1C
#define AM18XX_SDMMC_MMCNBLK	0x20
#define AM18XX_SDMMC_MMCDRR	0x28
#define AM18XX_SDMMC_MMCDXR	0x2C
#define AM18XX_SDMMC_MMCCMD	0x30
#define AM18XX_SDMMC_MMCARGHL	0x34
#define AM18XX_SDMMC_MMCRSP01	0x38
#define AM18XX_SDMMC_MMCRSP23	0x3C
#define AM18XX_SDMMC_MMCRSP45	0x40
#define AM18XX_SDMMC_MMCRSP67	0x44
#define AM18XX_SDMMC_FIFOCTL	0x74

#define AM18XX_SDMMC_MMCCTL_DATARST	__BIT(0)
#define AM18XX_SDMMC_MMCCTL_CMDRST	__BIT(1)
#define AM18XX_SDMMC_MMCCTL_WIDTH0	__BIT(2)
#define AM18XX_SDMMC_MMCCTL_DATEG	__BITS(7,6)
#define AM18XX_SDMMC_MMCCTL_WIDTH1	__BIT(8)
#define AM18XX_SDMMC_MMCCTL_PERMDR	__BIT(9)
#define AM18XX_SDMMC_MMCCTL_PERMDX	__BIT(10)

#define AM18XX_SDMMC_MMCCLK_CLKRT	__BITS(7,0)
#define AM18XX_SDMMC_MMCCLK_CLKEN	__BIT(8)
#define AM18XX_SDMMC_MMCCLK_DIV4	__BIT(9)

#define AM18XX_SDMMC_MMCST0_DATDNE	__BIT(0)
#define AM18XX_SDMMC_MMCST0_BSYDNE	__BIT(1)
#define AM18XX_SDMMC_MMCST0_RSPDNE	__BIT(2)
#define AM18XX_SDMMC_MMCST0_TOUTRD	__BIT(3)
#define AM18XX_SDMMC_MMCST0_TOUTRS	__BIT(4)
#define AM18XX_SDMMC_MMCST0_CRCWR	__BIT(5)
#define AM18XX_SDMMC_MMCST0_CRCRD	__BIT(6)
#define AM18XX_SDMMC_MMCST0_CRCRS	__BIT(7)
#define AM18XX_SDMMC_MMCST0_DXRDY	__BIT(9)
#define AM18XX_SDMMC_MMCST0_DRRDY	__BIT(10)
#define AM18XX_SDMMC_MMCST0_TRNDNE	__BIT(12)

#define AM18XX_SDMMC_MMCST0_ERRMASK	(AM18XX_SDMMC_MMCST0_TOUTRD | \
					 AM18XX_SDMMC_MMCST0_TOUTRS | \
					 AM18XX_SDMMC_MMCST0_CRCWR  | \
					 AM18XX_SDMMC_MMCST0_CRCRD  | \
					 AM18XX_SDMMC_MMCST0_CRCRS)

#define AM18XX_SDMMC_MMCST1_BUSY	__BIT(0)

#define AM18XX_SDMMC_MMCIM_EDATDNE	__BIT(0)
#define AM18XX_SDMMC_MMCIM_ERSPDNE	__BIT(2)
#define AM18XX_SDMMC_MMCIM_ETOUTRD	__BIT(3)
#define AM18XX_SDMMC_MMCIM_ETOUTRS	__BIT(4)
#define AM18XX_SDMMC_MMCIM_ECRCWR	__BIT(5)
#define AM18XX_SDMMC_MMCIM_ECRCRD	__BIT(6)
#define AM18XX_SDMMC_MMCIM_ECRCRS	__BIT(7)
#define AM18XX_SDMMC_MMCIM_EDXRDY	__BIT(9)
#define AM18XX_SDMMC_MMCIM_EDRRDY	__BIT(10)

#define AM18XX_SDMMC_MMCCMD_CMD		__BITS(5,0)
#define AM18XX_SDMMC_MMCCMD_PPLEN	__BIT(7)
#define AM18XX_SDMMC_MMCCMD_BSYEXP	__BIT(8)
#define AM18XX_SDMMC_MMCCMD_RSPFMT	__BITS(10,9)
#define AM18XX_SDMMC_MMCCMD_DTRW	__BIT(11)
#define AM18XX_SDMMC_MMCCMD_WDATX	__BIT(13)
#define AM18XX_SDMMC_MMCCMD_INITCK	__BIT(14)
#define AM18XX_SDMMC_MMCCMD_DMATRIG	__BIT(16)

#define AM18XX_SDMMC_MMCCMD_RSPFMT_R0		0
#define AM18XX_SDMMC_MMCCMD_RSPFMT_R1456	1
#define AM18XX_SDMMC_MMCCMD_RSPFMT_R2		2
#define AM18XX_SDMMC_MMCCMD_RSPFMT_R3		3

#define AM18XX_SDMMC_FIFOCTL_FIFORST	__BIT(0)
#define AM18XX_SDMMC_FIFOCTL_FIFODIRW	__BIT(1)
#define AM18XX_SDMMC_FIFOCTL_FIFOLEV64 	__BIT(2)

#define AM18XX_SDMMC_MAX_CLOCK_DIVIDER (2 * (0xFF + 1))
#define AM18XX_SDMMC_MIN_CLOCK_DIVIDER (2 * (0x00 + 1))

struct am18xx_sdmmc_softc {
	/* bus_space io */
	bus_space_tag_t sc_bst;
	bus_space_handle_t sc_bsh;

	device_t sc_dev;
	device_t sc_sdmmc;

	/* driver internals */
	kmutex_t sc_lock;
	kcondvar_t sc_intr_cv;
	struct sdmmc_command *sc_cmd;
	struct clk *sc_clk;

	/* edma */
	uint32_t sc_phys_base_addr;
	struct edma_channel *sc_rx_chan;
	struct edma_channel *sc_tx_chan;
	uint16_t sc_rx_param;
	uint16_t sc_tx_param;

	/* status flags */
	bool sc_irq_wait;
	bool sc_command_done;
	bool sc_transfer_done;
	bool sc_dma_done;
	bool sc_opendrain;
	bool sc_firstcmd;
};

static int	am18xx_sdmmc_match(device_t, cfdata_t, void *);
static void	am18xx_sdmmc_attach(device_t, device_t, void *);
static void	am18xx_sdmmc_init(struct am18xx_sdmmc_softc *);
static int	am18xx_sdmmc_host_reset(sdmmc_chipset_handle_t);
static uint32_t	am18xx_sdmmc_host_ocr(sdmmc_chipset_handle_t);
static int	am18xx_sdmmc_host_maxblklen(sdmmc_chipset_handle_t);
static int	am18xx_sdmmc_card_detect(sdmmc_chipset_handle_t);
static int	am18xx_sdmmc_write_protect(sdmmc_chipset_handle_t);
static int	am18xx_sdmmc_bus_power(sdmmc_chipset_handle_t, uint32_t);
static int	am18xx_sdmmc_bus_clock(sdmmc_chipset_handle_t, int);
static int	am18xx_sdmmc_bus_width(sdmmc_chipset_handle_t, int);
static int	am18xx_sdmmc_bus_rod(sdmmc_chipset_handle_t, int);
static void	am18xx_sdmmc_exec_command(sdmmc_chipset_handle_t,
    struct sdmmc_command *);
static void 	am18xx_sdmmc_initiate_command(struct am18xx_sdmmc_softc *,
    struct sdmmc_command *);
static void	am18xx_sdmmc_initiate_dma_transfer(struct am18xx_sdmmc_softc *,
    struct sdmmc_command *);
static void	am18xx_sdmmc_initiate_cpu_transfer(struct am18xx_sdmmc_softc *,
    struct sdmmc_command *);
static void	am18xx_sdmmc_check_completion(struct am18xx_sdmmc_softc *,
    bool);
static void	am18xx_sdmmc_card_enable_intr(sdmmc_chipset_handle_t, int);
static void	am18xx_sdmmc_card_intr_ack(sdmmc_chipset_handle_t);
static int	am18xx_sdmmc_intr(void *);
static void	am18xx_sdmmc_dma_callback(void *priv);

CFATTACH_DECL_NEW(am18xxsdmmc, sizeof(struct am18xx_sdmmc_softc),
    am18xx_sdmmc_match, am18xx_sdmmc_attach, NULL, NULL);

static const struct device_compatible_entry compat_data[] = {
	{.compat = "ti,da830-mmc"}, DEVICE_COMPAT_EOL};

static struct sdmmc_chip_functions am18xx_sdmmc_functions = {
	.host_reset	= am18xx_sdmmc_host_reset,
	.host_ocr	= am18xx_sdmmc_host_ocr,
	.host_maxblklen	= am18xx_sdmmc_host_maxblklen,
	.card_detect	= am18xx_sdmmc_card_detect,
	.write_protect	= am18xx_sdmmc_write_protect,
	.bus_power	= am18xx_sdmmc_bus_power,
	.bus_clock	= am18xx_sdmmc_bus_clock,
	.bus_width	= am18xx_sdmmc_bus_width,
	.bus_rod	= am18xx_sdmmc_bus_rod,
	.exec_command	= am18xx_sdmmc_exec_command,
	.card_enable_intr = am18xx_sdmmc_card_enable_intr,
	.card_intr_ack	= am18xx_sdmmc_card_intr_ack
};

static int
am18xx_sdmmc_host_reset(sdmmc_chipset_handle_t sch)
{
	struct am18xx_sdmmc_softc *sc = sch;

	printf("am18xx_sdmmc_host_reset\n");

	am18xx_sdmmc_init(sc);

	return 0;
}

static uint32_t
am18xx_sdmmc_host_ocr(sdmmc_chipset_handle_t sch)
{
	return MMC_OCR_3_2V_3_3V;
}

static int
am18xx_sdmmc_host_maxblklen(sdmmc_chipset_handle_t sch)
{
	return 2048;
}

static int
am18xx_sdmmc_card_detect(sdmmc_chipset_handle_t sch)
{
	return 1; /* todo: change this once we have GPIO */
}

static int
am18xx_sdmmc_write_protect(sdmmc_chipset_handle_t sch)
{
	return 0; /* todo: change this once we have GPIO */
}

static int
am18xx_sdmmc_bus_power(sdmmc_chipset_handle_t sch, uint32_t ocr)
{
	printf("am18xx_sdmmc_bus_power\n");
	return 0; /* do nothing */
}

static int
am18xx_sdmmc_bus_clock(sdmmc_chipset_handle_t sch, int clock)
{
	struct am18xx_sdmmc_softc *sc = sch;

	if (clock > 0) {
		u_int ref_clk = clk_get_rate(sc->sc_clk);

		/* calculate divider */
		u_int divider = (ref_clk / (1000 * clock) / 2) - 1;

		/* round divider */
		u_int effective_rate = ref_clk / (2 * (divider + 1));
		if (effective_rate > clock * 1000) {
			divider++;
		}
		if (divider > 255) {
			divider = 255;
		}

		device_printf(sc->sc_dev, "requested %d Hz, using %d Hz\n",
		    1000 * clock, ref_clk / (2 * (divider + 1)));

		SDMMC_WRITE(sc, AM18XX_SDMMC_MMCCLK,
		    divider | AM18XX_SDMMC_MMCCLK_CLKEN);
	} else {
		device_printf(sc->sc_dev, "clock=0Hz");
		SDMMC_WRITE(sc, AM18XX_SDMMC_MMCCLK, 0);
	}

	return 0;
}

static int
am18xx_sdmmc_bus_width(sdmmc_chipset_handle_t sch, int width)
{
	struct am18xx_sdmmc_softc *sc = sch;

	/* compute bit flags for new width */
	uint32_t val;
	switch(width) {
	case(1):
		val = 0;
		break;
	case(4):
		val = AM18XX_SDMMC_MMCCTL_WIDTH0;
		break;
	case(8):
		val = AM18XX_SDMMC_MMCCTL_WIDTH1;
		break;
	default:
		return 1;
	}

	/* change bus bit width bits in MMCCTL */
	uint32_t regval = SDMMC_READ(sc, AM18XX_SDMMC_MMCCTL);
	regval = val | (regval &
		 (~(AM18XX_SDMMC_MMCCTL_WIDTH0 | AM18XX_SDMMC_MMCCTL_WIDTH1)));
	SDMMC_WRITE(sc, AM18XX_SDMMC_MMCCTL, regval);

	return 0;
}

static int
am18xx_sdmmc_bus_rod(sdmmc_chipset_handle_t sch, int rod)
{
	struct am18xx_sdmmc_softc *sc = sch;

	mutex_enter(&sc->sc_lock);
	if (rod) {
		sc->sc_opendrain = true;
	} else {
		sc->sc_opendrain = false;
	}
	mutex_exit(&sc->sc_lock);

	return 0;
}

static void
am18xx_sdmmc_card_enable_intr(sdmmc_chipset_handle_t sch, int irq)
{
	struct am18xx_sdmmc_softc *sc = sch;
	device_printf(sc->sc_dev, "SDIO interrupts not implemented\n");
}

static void
am18xx_sdmmc_card_intr_ack(sdmmc_chipset_handle_t sch)
{
	struct am18xx_sdmmc_softc *sc = sch;
	device_printf(sc->sc_dev, "SDIO interrupts not implemented\n");
}

static void
am18xx_sdmmc_exec_command(sdmmc_chipset_handle_t sch, struct sdmmc_command *cmd)
{
	struct am18xx_sdmmc_softc *sc = sch;

	mutex_enter(&sc->sc_lock);
	KASSERT(sc->sc_cmd == NULL);
	sc->sc_cmd = cmd;

	/* wait for the card to be ready */
	int timeout = 1000000;
	while (SDMMC_READ(sc, AM18XX_SDMMC_MMCST1) & AM18XX_SDMMC_MMCST1_BUSY) {
		delay(10);
		if (timeout-- == 0) {
			device_printf(sc->sc_dev, "mmcst1 timeout\n");
			cmd->c_error = ETIMEDOUT;
			goto out;
		}
	}

	/* send the command to the controller */
	am18xx_sdmmc_initiate_command(sc, cmd);

	/* wait for a response */
	int err;
	while (sc->sc_irq_wait) {
		err = cv_timedwait(&sc->sc_intr_cv, &sc->sc_lock, mstohz(1000));
		if (err == EWOULDBLOCK) {
			device_printf(sc->sc_dev, "command timeout");
			cmd->c_error = ETIMEDOUT;
			//edma_dump(sc->sc_rx_chan);
			goto out;
		}
	}

	// TODO: halt DMA

	/* read the command response */
	if (ISSET(cmd->c_flags, SCF_RSP_PRESENT)) {
		if (cmd->c_flags & SCF_RSP_136) {
			cmd->c_resp[3] = SDMMC_READ(sc, AM18XX_SDMMC_MMCRSP67);
			cmd->c_resp[2] = SDMMC_READ(sc, AM18XX_SDMMC_MMCRSP45);
			cmd->c_resp[1] = SDMMC_READ(sc, AM18XX_SDMMC_MMCRSP23);
			cmd->c_resp[0] = SDMMC_READ(sc, AM18XX_SDMMC_MMCRSP01);

			cmd->c_resp[0] >>= 8; /* Remove CRC7 + LSB. */
			cmd->c_resp[0] |= (0x000000FF & cmd->c_resp[1]) << 24;
			cmd->c_resp[1] >>= 8;
			cmd->c_resp[1] |= (0x000000FF & cmd->c_resp[2]) << 24;
			cmd->c_resp[2] >>= 8;
			cmd->c_resp[2] |= (0x000000FF & cmd->c_resp[3]) << 24;
			cmd->c_resp[3] >>= 8;
		} else {
			cmd->c_resp[0] = SDMMC_READ(sc, AM18XX_SDMMC_MMCRSP67);
		}
	}

	sc->sc_firstcmd = false;
out:
	sc->sc_cmd = NULL;
	mutex_exit(&sc->sc_lock);
}

static void am18xx_sdmmc_initiate_command(struct am18xx_sdmmc_softc *sc,
    struct sdmmc_command *cmd)
{
	bool use_dma = cmd->c_data != NULL && cmd->c_datalen >= 64;

	/* write block size settings */
	SDMMC_WRITE(sc, AM18XX_SDMMC_MMCBLEN, cmd->c_blklen);
	if (cmd->c_blklen != 0) {
		SDMMC_WRITE(sc, AM18XX_SDMMC_MMCNBLK,
		    cmd->c_datalen / cmd->c_blklen);
	} else {
		SDMMC_WRITE(sc, AM18XX_SDMMC_MMCNBLK, 0);
	}

	/* write command */
	uint32_t command = __SHIFTIN(cmd->c_opcode, AM18XX_SDMMC_MMCCMD_CMD);
	uint32_t command_type;
	if (!ISSET(cmd->c_flags, SCF_RSP_PRESENT)) {
		/* no response */
		command_type = AM18XX_SDMMC_MMCCMD_RSPFMT_R0;
	} else if (ISSET(cmd->c_flags, SCF_RSP_136)) {
		/* 136 bits, CRC */
		command_type = AM18XX_SDMMC_MMCCMD_RSPFMT_R2;
	} else if (ISSET(cmd->c_flags, SCF_RSP_CRC)) {
		/* 48 bits, CRC */
		command_type = AM18XX_SDMMC_MMCCMD_RSPFMT_R1456;
	} else {
		/* 48 bits, no CRC */
		command_type = AM18XX_SDMMC_MMCCMD_RSPFMT_R3;
	}
	command |= __SHIFTIN(command_type, AM18XX_SDMMC_MMCCMD_RSPFMT);
	if (sc->sc_opendrain) {
		command |= AM18XX_SDMMC_MMCCMD_PPLEN;
	}
	if (ISSET(cmd->c_flags, SCF_RSP_BSY)) {
		command |= AM18XX_SDMMC_MMCCMD_BSYEXP;
	}
	if (!ISSET(cmd->c_flags, SCF_CMD_READ)) {
		command |= AM18XX_SDMMC_MMCCMD_DTRW;
	}
	if (cmd->c_data != NULL && cmd->c_datalen > 0) {
		/* command has data transfer */
		command |= AM18XX_SDMMC_MMCCMD_WDATX;
		command |= AM18XX_SDMMC_MMCCMD_DMATRIG; // TODO: figure out when exactly
	}
	if (sc->sc_firstcmd) {
		command |= AM18XX_SDMMC_MMCCMD_INITCK;
	}

	/* configure FIFO register */
	if (ISSET(cmd->c_flags, SCF_CMD_READ)) {
		/* read */
		SDMMC_WRITE(sc, AM18XX_SDMMC_FIFOCTL,
			AM18XX_SDMMC_FIFOCTL_FIFOLEV64 |
			AM18XX_SDMMC_FIFOCTL_FIFORST);
		SDMMC_WRITE(sc, AM18XX_SDMMC_FIFOCTL,
			AM18XX_SDMMC_FIFOCTL_FIFOLEV64);
	} else {
		/* write */
		SDMMC_WRITE(sc, AM18XX_SDMMC_FIFOCTL,
			AM18XX_SDMMC_FIFOCTL_FIFOLEV64 |
			AM18XX_SDMMC_FIFOCTL_FIFODIRW |
			AM18XX_SDMMC_FIFOCTL_FIFORST);
		SDMMC_WRITE(sc, AM18XX_SDMMC_FIFOCTL,
			AM18XX_SDMMC_FIFOCTL_FIFOLEV64 |
			AM18XX_SDMMC_FIFOCTL_FIFODIRW);
	}

	sc->sc_command_done = false;
	sc->sc_transfer_done = cmd->c_datalen == 0 || cmd->c_data == NULL;
	sc->sc_dma_done = !use_dma;

	if (use_dma) {
		am18xx_sdmmc_initiate_dma_transfer(sc, cmd);
	} else if (cmd->c_data != NULL) {
		am18xx_sdmmc_initiate_cpu_transfer(sc, cmd);
	}

	/* write command arguments */
	SDMMC_WRITE(sc, AM18XX_SDMMC_MMCTOR, 0x1FFF);
	SDMMC_WRITE(sc, AM18XX_SDMMC_MMCARGHL, cmd->c_arg);

	/* send the command */
	sc->sc_irq_wait = true;
	SDMMC_WRITE(sc, AM18XX_SDMMC_MMCCMD, command);
}

static void am18xx_sdmmc_initiate_dma_transfer(struct am18xx_sdmmc_softc *sc,
    struct sdmmc_command *cmd)
{
	cmd->c_resid = 0;

	KASSERT(ISSET(cmd->c_flags, SCF_CMD_READ)); /* write isn't implemented*/
	KASSERT((cmd->c_datalen & 0x3) == 0); /* currently, we need word-sized sizes */

	KASSERT((cmd->c_datalen & 0x3f) == 0); /* data access is a multiple of fifo size */
	/* transfer needs to be bigger than the FIFO size */
	KASSERT(cmd->c_datalen >= 64);
	KASSERT(ISSET(cmd->c_flags, SCF_CMD_READ)); /* write isn't implemented*/

	printf("the driver: preparing DMA %d %d\n", cmd->c_datalen / 64, cmd->c_datalen);

	struct edma_channel *channel;
	if (ISSET(cmd->c_flags, SCF_CMD_READ)) {
		channel = sc->sc_rx_chan;
	} else {
		channel = sc->sc_tx_chan;
	}

	// TODO: range checks
	KASSERT(cmd->c_datalen / 64 <= 65535);
	KASSERT(cmd->c_dmamap->dm_nsegs == 1);

	/* Do an AB-synchronized transfer */
	struct edma_param transfer;
	transfer.ep_opt = __SHIFTIN(edma_channel_index(channel), EDMA_PARAM_OPT_TCC) | EDMA_PARAM_OPT_TCINTEN | EDMA_PARAM_OPT_SYNCDIM;
	transfer.ep_src = sc->sc_phys_base_addr + AM18XX_SDMMC_MMCDRR;
	transfer.ep_dst = cmd->c_dmamap->dm_segs[0].ds_addr;
	transfer.ep_acnt = 4;
	transfer.ep_bcnt = 16;
	transfer.ep_ccnt = cmd->c_datalen / 64;
	transfer.ep_dstbidx = 4;
	transfer.ep_dstcidx = 64;
	transfer.ep_srcbidx = 0;
	transfer.ep_srccidx = 0;
	transfer.ep_bcntrld = 0;
	transfer.ep_link = 0xFFFF;

	edma_halt(channel); // TODO: remove in the future (rn to clear pending events)
	//edma_dump(channel);

	if (ISSET(cmd->c_flags, SCF_CMD_READ)) {
		edma_set_param(sc->sc_rx_chan, sc->sc_rx_param,
		    &transfer);
		edma_transfer_enable(sc->sc_rx_chan, sc->sc_rx_param);
	} else {
		edma_set_param(sc->sc_tx_chan, sc->sc_tx_param,
		    &transfer);
		edma_transfer_enable(sc->sc_tx_chan, sc->sc_tx_param);
	}
}

static void am18xx_sdmmc_initiate_cpu_transfer(struct am18xx_sdmmc_softc *sc,
    struct sdmmc_command *cmd)
{
	KASSERT(ISSET(cmd->c_flags, SCF_CMD_READ)); /* write isn't implemented*/
	KASSERT((cmd->c_datalen & 0x3) == 0); /* currently, we need word-sized sizes */

	cmd->c_buf = cmd->c_data;
	cmd->c_resid = cmd->c_datalen;
}

static void am18xx_sdmmc_init(struct am18xx_sdmmc_softc *sc)
{
	/* reset the controller */
	SDMMC_WRITE(sc, AM18XX_SDMMC_MMCCTL, AM18XX_SDMMC_MMCCTL_DATARST
					   | AM18XX_SDMMC_MMCCTL_CMDRST);
	SDMMC_READ(sc, AM18XX_SDMMC_MMCST0);
	SDMMC_READ(sc, AM18XX_SDMMC_MMCST1);
	delay(10);

	/* clocks off */
	SDMMC_WRITE(sc, AM18XX_SDMMC_MMCCLK, 0);

	/* disable all interrupts */
	SDMMC_WRITE(sc, AM18XX_SDMMC_MMCIM, 0);
	/* write timeout values to maximum */
	SDMMC_WRITE(sc, AM18XX_SDMMC_MMCTOR, 0x3FFFF);
	SDMMC_WRITE(sc, AM18XX_SDMMC_MMCTOD, 0xFFFF);

	sc->sc_irq_wait = false;
	sc->sc_opendrain = true;
	sc->sc_firstcmd = true;
	sc->sc_cmd = NULL;

	/* take the controller out of reset */
	SDMMC_WRITE(sc, AM18XX_SDMMC_MMCCTL, 0);

	/* enable the clock */
	am18xx_sdmmc_bus_clock(sc, 400);

	/* enable interrupts  */
	SDMMC_WRITE(sc, AM18XX_SDMMC_MMCIM, AM18XX_SDMMC_MMCIM_EDATDNE |
					    AM18XX_SDMMC_MMCIM_ERSPDNE |
					    AM18XX_SDMMC_MMCIM_ETOUTRD |
					    AM18XX_SDMMC_MMCIM_ETOUTRS |
					    AM18XX_SDMMC_MMCIM_ECRCWR |
					    AM18XX_SDMMC_MMCIM_ECRCRD |
					    AM18XX_SDMMC_MMCIM_ECRCRS |
					    AM18XX_SDMMC_MMCIM_EDXRDY |
					    AM18XX_SDMMC_MMCIM_EDRRDY);
}

static uint32_t
am18xx_sdmmc_cpu_data_transfer(struct am18xx_sdmmc_softc *sc)
{
	/* Disable interrupts during cpu transfer. This allows us to capture
	 * new interrupts and handle them in this interrupt instead of
	 * generating a second interrupt. */
	uint32_t mmcim = SDMMC_READ(sc, AM18XX_SDMMC_MMCIM);
	SDMMC_WRITE(sc, AM18XX_SDMMC_MMCIM, 0);

	KASSERT(ISSET(sc->sc_cmd->c_flags, SCF_CMD_READ));

	uint32_t status, fullstatus = 0;
	do {
		/* read one FIFOfull of data */
		for (int i = 0; i < (64 / 4) && sc->sc_cmd->c_resid > 0; i++) {
			KASSERT((sc->sc_cmd->c_resid & 0x3) == 0);

			*((uint32_t *)sc->sc_cmd->c_buf) =
			    SDMMC_READ(sc, AM18XX_SDMMC_MMCDRR);
			sc->sc_cmd->c_resid -= 4;
			sc->sc_cmd->c_buf += 4;
		}
		status = SDMMC_READ(sc, AM18XX_SDMMC_MMCST0);
		fullstatus |= status;
	} while (status & (AM18XX_SDMMC_MMCST0_DRRDY
			 | AM18XX_SDMMC_MMCST0_DXRDY));

	/* re-enable data receive/transmit interrupts*/
	SDMMC_WRITE(sc, AM18XX_SDMMC_MMCIM, mmcim);
	/* and return our new interrupts */
	return fullstatus;
}

static int
am18xx_sdmmc_intr(void *arg)
{
	bool cmd_failed = false;
	struct am18xx_sdmmc_softc *sc = arg;

	/* ensure we have the lock while touching the softcore */
	mutex_enter(&sc->sc_lock);

	/* Read the interrupt cause; handle it. The order in which we deal with
	 * different interrupt reasons matters a great deal. */
	uint32_t cause = SDMMC_READ(sc, AM18XX_SDMMC_MMCST0);
	if (cause == 0) return 1; /* there is a low chance this happens */

	/* ensure this only runs if we expect irqs */
	KASSERT(sc->sc_irq_wait);

	/* 1. Service the FIFO until it is empty/full (depending on if we are
	 * reading or writing). We can complete multiple FIFO loads in one
	 * interrupts if the FIFO clears fast enough. After servicing the
	 * FIFO, am18xx_sdmmc_cpu_data_transfer checks for new interrupt
	 * requests like CRC errors or command completion events so we can
	 * deal with them without generating a second interrupt. We service
	 * the FIFO first so we can deal with these new events later on. */
	if (cause & (AM18XX_SDMMC_MMCST0_DRRDY | AM18XX_SDMMC_MMCST0_DXRDY )) {
		cause |= am18xx_sdmmc_cpu_data_transfer(sc);
	}

	/* 2. DATDNE indicates the data transfer is complete. If the data size
	 * is not a multiple of the FIFO, we don't get a DXRDY|DRRDY irq,
	 * but the data will be ready to transfer.
	 * This step is the second because the FIFO transfer might still
	 * generate events like CRC errors. */
	if (cause & AM18XX_SDMMC_MMCST0_DATDNE) {
		if (sc->sc_cmd->c_resid > 0) {
			/* transfer remaining outstanding data */
			cause |= am18xx_sdmmc_cpu_data_transfer(sc);
		}
		sc->sc_transfer_done = true;
	}

	/* 3. Check if any operation failed (CRC errors, timeouts). By now, all
	 * FIFO transfers that could generate new events have happened. */
	if (cause & AM18XX_SDMMC_MMCST0_ERRMASK) {
		if (cause & (AM18XX_SDMMC_MMCST0_TOUTRS
			   | AM18XX_SDMMC_MMCST0_TOUTRD)) {
			sc->sc_cmd->c_error = ETIMEDOUT;
		} else {
			sc->sc_cmd->c_error = EIO;
		}

		cmd_failed = true;
	}

	/* 4. Check if the command has been fully transmitted. */
	if (cause & AM18XX_SDMMC_MMCST0_RSPDNE) {
		sc->sc_command_done = true;
	}

	/* signal the main thread if we are done */
	am18xx_sdmmc_check_completion(sc, cmd_failed);

	printf("irq cause2 %x\n", cause);

	mutex_exit(&sc->sc_lock);
	return 1; /* acknowledge IRQ */
}

static void
am18xx_sdmmc_dma_callback(void *priv)
{
	/* todo */
	printf("dma callback reached!\n");

	struct am18xx_sdmmc_softc *sc = priv;

	/* ensure we have the lock while touching the softcore */
	mutex_enter(&sc->sc_lock);

	sc->sc_dma_done = true;
	am18xx_sdmmc_check_completion(sc, false);

	mutex_exit(&sc->sc_lock);
}

static void
am18xx_sdmmc_check_completion(struct am18xx_sdmmc_softc *sc, bool has_err)
{
	bool completed = sc->sc_command_done && sc->sc_transfer_done && sc->sc_dma_done;

	if (completed || has_err) {
		KASSERT(sc->sc_cmd->c_resid == 0);
		sc->sc_irq_wait = false;
		printf("completing \n");
		cv_signal(&sc->sc_intr_cv);
	}
}

int
am18xx_sdmmc_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args *const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

void
am18xx_sdmmc_attach(device_t parent, device_t self, void *aux)
{
	struct am18xx_sdmmc_softc *const sc = device_private(self);
	struct fdt_attach_args *const faa = aux;
	const int phandle = faa->faa_phandle;
	struct sdmmcbus_attach_args saa;
	bus_addr_t addr;
	bus_size_t size;
	char intrstr[128];

	sc->sc_bst = faa->faa_bst;
	sc->sc_dev = self;

	/* we need a spin mutex for intr->lwp synchronization */
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_HIGH);
	cv_init(&sc->sc_intr_cv, "sdmmc_intr");

	/* enable host controller clock */
	sc->sc_clk = fdtbus_clock_get_index(phandle, 0);
	if (sc->sc_clk == NULL) {
		aprint_error(": failed to get sdmmc clk\n");
		return;
	}
	if (clk_enable(sc->sc_clk) != 0) {
		aprint_error(": failed to enable sdmmc clk\n");
		return;
	}
	u_int clk_rate = clk_get_rate(sc->sc_clk);

	/* map bus space */
	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0) {
		aprint_error(": couldn't get registers\n");
		return;
	}
	if (bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_bsh)) {
		aprint_error(": couldn't map registers\n");
		return;
	}
	sc->sc_phys_base_addr = addr;

	/* establish interrupt */
	if (!fdtbus_intr_str(phandle, 0, intrstr, sizeof(intrstr))) {
		aprint_error(": failed to decode interrupt\n");
		return;
	}
	void *ih = fdtbus_intr_establish_xname(phandle, 0, IPL_SDMMC, IST_LEVEL,
	    am18xx_sdmmc_intr, sc, device_xname(self));
	if (ih == NULL) {
		aprint_error(": couldn't install interrupt\n");
		return;
	}

	/* rx dma channels */
	struct fdtbus_dma *rx_dma = fdtbus_dma_get(phandle, "rx",
	    am18xx_sdmmc_dma_callback, sc);
	if (rx_dma == NULL) {
		aprint_error(": couldn't get rx dma handle\n");
		return;
	}
	/* escape fdt_dma because it's interface doesn't match the edma */
	sc->sc_rx_chan = rx_dma->dma_priv;
	sc->sc_rx_param = edma_param_alloc(sc->sc_rx_chan, EDMA_PARAM_TRIGGER);
	if (sc->sc_rx_param == 0xffff) {
		aprint_error(": couldn't get rx dma param entry\n");
		return;
	}

	/* tx dma channels */
	struct fdtbus_dma *tx_dma = fdtbus_dma_get(phandle, "tx",
	    am18xx_sdmmc_dma_callback, sc);
	if (tx_dma == NULL) {
		aprint_error(": couldn't get rx dma handle\n");
		return;
	}
	/* escape fdt_dma because it's interface doesn't match the edma */
	sc->sc_tx_chan = tx_dma->dma_priv;
	sc->sc_tx_param = edma_param_alloc(sc->sc_tx_chan, EDMA_PARAM_TRIGGER);
	if (sc->sc_tx_param == 0xffff) {
		aprint_error(": couldn't get tx dma param entry\n");
		return;
	}

	aprint_normal("\n");

	/* reset the controller */
	am18xx_sdmmc_init(sc);

	/* attach us as an sdmmc device */
	memset(&saa, 0, sizeof(saa));
	saa.saa_busname = "sdmmc";
	saa.saa_sct	= &am18xx_sdmmc_functions;
	saa.saa_spi_sct	= NULL;
	saa.saa_sch	= sc;
	saa.saa_dmat	= faa->faa_dmat;
	saa.saa_clkmin	= clk_rate / AM18XX_SDMMC_MAX_CLOCK_DIVIDER;
	if (of_getprop_uint32(phandle, "max-frequency", &saa.saa_clkmin)) {
		saa.saa_clkmax = 25000000; /* 25MHz is always okay*/
	}
	saa.saa_caps = SMC_CAPS_DMA;

	if (of_hasprop(phandle, "cap-sd-highspeed")) {
		saa.saa_caps |= SMC_CAPS_SD_HIGHSPEED;
	}
	if (of_hasprop(phandle, "cap-mmc-highspeed")) {
		saa.saa_caps |= SMC_CAPS_MMC_HIGHSPEED;
	}

	uint32_t bus_width;
	if (of_getprop_uint32(phandle, "bus-width", &bus_width)) {
		bus_width = 1;
	}
	switch (bus_width) {
	case 8:
		saa.saa_caps |= SMC_CAPS_8BIT_MODE;
		break;
	case 4:
		saa.saa_caps |= SMC_CAPS_4BIT_MODE;
		break;
	default:
		/* use 1-bit mode */
		break;
	}

	sc->sc_sdmmc = config_found(sc->sc_dev, &saa, NULL, CFARGS_NONE);
	if (sc->sc_sdmmc == NULL) {
		aprint_error_dev(sc->sc_dev, "unable to attach sdmmc\n");
		return;
	}
}

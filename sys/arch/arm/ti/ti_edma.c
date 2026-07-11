/* $NetBSD: ti_edma.c,v 1.5 2022/05/21 19:07:23 andvar Exp $ */

/*-
 * Copyright (c) 2014 Jared D. McNeill <jmcneill@invisible.ca>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: ti_edma.c,v 1.5 2022/05/21 19:07:23 andvar Exp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/intr.h>
#include <sys/mutex.h>
#include <sys/bus.h>
#include <sys/bitops.h>

#include <dev/fdt/fdtvar.h>

#include <arm/ti/ti_prcm.h>
#include <arm/ti/ti_edma.h>

#define MAX_DMA_CHANNELS	64
#define MAX_PARAM_SETS		256
#define MAX_PARAM_PER_CHANNEL	32

//#define EDMA_DEBUG
#ifdef EDMA_DEBUG
int edmadebug = 10;
#define DPRINTF(n,s)    do { if ((n) <= edmadebug) device_printf s; } while (0)
#else
#define DPRINTF(n,s)    do {} while (0)
#endif

struct edma_softc;

struct edma_channel {
	struct edma_softc *ch_sc;
	enum edma_type ch_type;
	uint8_t ch_index;
	void (*ch_callback)(void *);
	void *ch_callbackarg;
	unsigned int ch_nparams;
	uint16_t ch_ownedparams[MAX_PARAM_PER_CHANNEL];
};

struct edma_softc {
	device_t sc_dev;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
	kmutex_t sc_lock;
	struct edma_channel sc_dma[MAX_DMA_CHANNELS];

	void *sc_ih;

	uint32_t sc_dmamask[MAX_DMA_CHANNELS / 32];
	uint32_t sc_parammask[MAX_PARAM_SETS / 32];

	/* CCCFG settings  */
	uint32_t sc_num_channels;
	uint32_t sc_num_params;
	bool sc_has_chmap;
};

static int edma_match(device_t, cfdata_t, void *);
static void edma_attach(device_t, device_t, void *);

static void edma_init(struct edma_softc *);
static int edma_intr(void *);
static void edma_write_param(struct edma_softc *,
				  unsigned int, const struct edma_param *);
static void edma_channel_free_params(struct edma_channel *);
static bool edma_bit_isset(uint32_t *, unsigned int);
static void edma_bit_set(uint32_t *, unsigned int);
static void edma_bit_clr(uint32_t *, unsigned int);
static void * edma_fdt_acquire(device_t, const void *, size_t,
    void (*)(void *), void *);
static void edma_fdt_release(device_t, void *);
static int edma_fdt_transfer(device_t, void *, struct fdtbus_dma_req *);
static void edma_fdt_halt(device_t, void *);
static struct edma_channel *edma_channel_alloc_internal(struct edma_softc *,
    enum edma_type, unsigned int, void (*)(void *), void *);

CFATTACH_DECL_NEW(ti_edma, sizeof(struct edma_softc),
    edma_match, edma_attach, NULL, NULL);

#define EDMA_READ(sc, reg) \
	bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg))
#define EDMA_WRITE(sc, reg, val) \
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

/* we only use the fdt system to get the controller to consumers */
static const struct fdtbus_dma_controller_func edma_fdt_funcs = {
	.acquire = edma_fdt_acquire,
	.release = edma_fdt_release,
	.transfer = edma_fdt_transfer,
	.halt = edma_fdt_halt
};

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "ti,edma3-tpcc" },
	DEVICE_COMPAT_EOL
};

static int
edma_match(device_t parent, cfdata_t match, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
edma_attach(device_t parent, device_t self, void *aux)
{
	struct edma_softc *sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
	char intrstr[128];
	bus_addr_t addr;
	bus_size_t size;
	int idx;

	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0) {
		aprint_error(": couldn't get registers\n");
		return;
	}

	if (!fdtbus_intr_str(phandle, 0, intrstr, sizeof(intrstr))) {
		aprint_error(": failed to decode interrupt\n");
		return;
	}

	sc->sc_dev = self;
	sc->sc_iot = faa->faa_bst;
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_VM);
	if (bus_space_map(sc->sc_iot, addr, size, 0, &sc->sc_ioh) != 0) {
		aprint_error(": couldn't map registers\n");
		return;
	}

	aprint_naive("\n");
	aprint_normal(": EDMA Channel Controller\n");

	for (idx = 0; idx < MAX_DMA_CHANNELS; idx++) {
		struct edma_channel *ch = &sc->sc_dma[idx];
		ch->ch_sc = sc;
		ch->ch_type = EDMA_TYPE_DMA;
		ch->ch_index = idx;
		ch->ch_callback = NULL;
		ch->ch_callbackarg = NULL;
		ch->ch_nparams = 0;
	}

	if (of_hasprop(phandle, "power-domains")) {
		/* clocks are configured through the fdt system */
		if (fdtbus_powerdomain_enable(phandle) != 0) {
			aprint_error(": couldn't enable powerdomain\n");
			return;
		}
	} else {
		/* clock are configured through the prcm system  */
		if (ti_prcm_enable_hwmod(phandle, 0) != 0) {
			aprint_error(": couldn't enable module\n");
			return;
		}
	}

	edma_init(sc);

	sc->sc_ih = fdtbus_intr_establish_byname(phandle, "edma3_ccint",
	    IPL_VM, FDT_INTR_MPSAFE, edma_intr, sc, device_xname(self));
	if (sc->sc_ih == NULL) {
		aprint_error_dev(self, "failed to establish interrupt\n");
		return;
	}
	aprint_normal_dev(self, "interrupting on %s\n", intrstr);

	fdtbus_register_dma_controller(self, phandle, &edma_fdt_funcs);
}

/*
 * Hardware initialization
 */
static void
edma_init(struct edma_softc *sc)
{
	struct edma_param param;
	uint32_t cccfg_val;
	int idx;

	cccfg_val = EDMA_READ(sc, EDMA_CCCFG_REG);

	sc->sc_has_chmap = cccfg_val & EDMA_CCCFG_CHMAP_EXIST ? true : false;
	sc->sc_num_channels = 2 << __SHIFTOUT(cccfg_val, EDMA_CCCFG_NUM_DMACH);
	sc->sc_num_params = 16 << __SHIFTOUT(cccfg_val, EDMA_CCCFG_NUM_PAENTRY);
	KASSERT(sc->sc_num_channels <= MAX_DMA_CHANNELS);
	KASSERT(sc->sc_num_params <= MAX_PARAM_SETS);

	if (sc->sc_has_chmap) {
		for (idx = 0; idx < sc->sc_num_channels; idx++) {
			EDMA_WRITE(sc, EDMA_DCHMAP_REG(idx),
			    __SHIFTIN(0, EDMA_DCHMAP_PAENTRY));
		}
	}

	/* fill the PaRAM with dummies */
	memset(&param, 0, sizeof(param));
	param.ep_bcnt = 1;
	for (idx = 0; idx < sc->sc_num_params; idx++) {
		edma_write_param(sc, idx, &param);
	}

	/* reserve PaRAM entry 0 for dummy slot */
	edma_bit_set(sc->sc_parammask, 0);
}

/*
 * Write a PaRAM entry
 */
static void
edma_write_param(struct edma_softc *sc,
    unsigned int idx, const struct edma_param *ep)
{
	EDMA_WRITE(sc, EDMA_PARAM_OPT_REG(idx), ep->ep_opt);
	EDMA_WRITE(sc, EDMA_PARAM_SRC_REG(idx), ep->ep_src);
	EDMA_WRITE(sc, EDMA_PARAM_CNT_REG(idx),
	    __SHIFTIN(ep->ep_bcnt, EDMA_PARAM_CNT_BCNT) |
	    __SHIFTIN(ep->ep_acnt, EDMA_PARAM_CNT_ACNT));
	EDMA_WRITE(sc, EDMA_PARAM_DST_REG(idx), ep->ep_dst);
	EDMA_WRITE(sc, EDMA_PARAM_BIDX_REG(idx),
	    __SHIFTIN(ep->ep_dstbidx, EDMA_PARAM_BIDX_DSTBIDX) |
	    __SHIFTIN(ep->ep_srcbidx, EDMA_PARAM_BIDX_SRCBIDX));
	EDMA_WRITE(sc, EDMA_PARAM_LNK_REG(idx),
	    __SHIFTIN(ep->ep_bcntrld, EDMA_PARAM_LNK_BCNTRLD) |
	    __SHIFTIN(ep->ep_link, EDMA_PARAM_LNK_LINK));
	EDMA_WRITE(sc, EDMA_PARAM_CIDX_REG(idx),
	    __SHIFTIN(ep->ep_dstcidx, EDMA_PARAM_CIDX_DSTCIDX) |
	    __SHIFTIN(ep->ep_srccidx, EDMA_PARAM_CIDX_SRCCIDX));
	EDMA_WRITE(sc, EDMA_PARAM_CCNT_REG(idx),
	    __SHIFTIN(ep->ep_ccnt, EDMA_PARAM_CCNT_CCNT));
}

static bool
edma_bit_isset(uint32_t *bits, unsigned int bit)
{
	return !!(bits[bit >> 5] & (1 << (bit & 0x1f)));
}

static void
edma_bit_set(uint32_t *bits, unsigned int bit)
{
	bits[bit >> 5] |= (1 << (bit & 0x1f));
}

static void
edma_bit_clr(uint32_t *bits, unsigned int bit)
{
	bits[bit >> 5] &= ~(1 << (bit & 0x1f));
}

static int
edma_intr(void *priv)
{
	struct edma_softc *sc = priv;
	uint64_t ipr, ier;
	int bit, idx;

	ipr = EDMA_READ(sc, EDMA_IPR_REG);
	ipr |= (uint64_t)EDMA_READ(sc, EDMA_IPRH_REG) << 32;
	if (ipr == 0)
		return 0;

	ier = EDMA_READ(sc, EDMA_IER_REG);
	ier |= (uint64_t)EDMA_READ(sc, EDMA_IERH_REG) << 32;

	DPRINTF(2, (sc->sc_dev, "ipr = 0x%016llx ier 0x%016llx\n", ipr, ier));

	EDMA_WRITE(sc, EDMA_ICR_REG, ipr & 0xffffffff);
	EDMA_WRITE(sc, EDMA_ICRH_REG, ipr >> 32);

	while ((bit = ffs64(ipr)) != 0) {
		idx = bit - 1;
		ipr &= ~__BIT(idx);
		if (!(ier & __BIT(idx)))
			continue;
		if (!edma_bit_isset(sc->sc_dmamask, idx))
			continue;

		struct edma_channel *chan = &sc->sc_dma[idx];

		edma_channel_free_params(chan);

		chan->ch_callback(sc->sc_dma[idx].ch_callbackarg);
	}

	EDMA_WRITE(sc, EDMA_IEVAL_REG, EDMA_IEVAL_EVAL);

	return 1;
}

static void *
edma_fdt_acquire(device_t dev, const void *data, size_t len, void (*cb)(void *),
    void *cbarg)
{
	struct edma_softc *sc = device_private(dev);
	const uint32_t *specifier = data;

	/* get channel index */
	if (len != 8) {
		return NULL;
	}
	const u_int chan_index = be32toh(specifier[0]);
	printf("edma_fdt_acquire chan_index=%d\n", chan_index);

	return edma_channel_alloc_internal(sc, EDMA_TYPE_DMA, chan_index, cb,
	    cbarg);
}

static void
edma_fdt_release(device_t dev, void *priv)
{
	struct edma_channel *chan = priv;

	edma_channel_free(chan);
}

static int
edma_fdt_transfer(device_t dev, void *priv, struct fdtbus_dma_req *req)
{
	struct edma_channel *chan = priv;
	struct edma_param transfer;
	int acnt, bcnt, ccnt;

	KASSERT(req->dreq_nsegs <= MAX_PARAM_PER_CHANNEL);

	acnt = req->dreq_dev_opt.opt_bus_width;
	KASSERT(acnt <= UINT16_MAX);
	KASSERT(req->dreq_dev_opt.opt_burst_len % acnt == 0);
	bcnt = req->dreq_dev_opt.opt_burst_len / acnt;
	KASSERT(bcnt <= UINT16_MAX);

	/* allocate param entries */
	KASSERT(chan->ch_nparams == 0);
	for(int i = 0; i < req->dreq_nsegs; i++) {
		uint16_t param = edma_param_alloc(chan,
		    (i == 0) ? EDMA_PARAM_TRIGGER : EDMA_PARAM_OTHER);

		if(param == 0xffff) {
			/* failed to allocate, free all and return */
			edma_channel_free_params(chan);
			return EAGAIN;
		}

		chan->ch_ownedparams[i] = param;
	}

	/* fill param entries */
	for(int i = 0; i < req->dreq_nsegs; i++) {
		bus_dma_segment_t seg = req->dreq_segs[i];

		KASSERT(seg.ds_len <= UINT32_MAX);
		KASSERT(seg.ds_len % (acnt * bcnt) == 0);
		ccnt = seg.ds_len / (acnt * bcnt);
		KASSERT(ccnt <= UINT16_MAX);

		/* Do an AB-synchronized transfer */
		transfer.ep_opt =
		    __SHIFTIN(edma_channel_index(chan), EDMA_PARAM_OPT_TCC)
		    | EDMA_PARAM_OPT_SYNCDIM;
		transfer.ep_acnt = acnt;
		transfer.ep_bcnt = bcnt;
		transfer.ep_ccnt = ccnt;
		transfer.ep_bcntrld = 0;

		if (i == req->dreq_nsegs - 1) {
			transfer.ep_opt |= EDMA_PARAM_OPT_TCINTEN;
			transfer.ep_link = 0xffff;
		} else {
			transfer.ep_link = EDMA_PARAM_BASE(chan->ch_ownedparams[i+1]);
		}

		if (req->dreq_sel == 1) {
			transfer.ep_opt |= __SHIFTIN(2, EDMA_PARAM_OPT_FWID);
			transfer.ep_opt |= (req->dreq_dir == FDT_DMA_READ)
					       ? EDMA_PARAM_OPT_SAM
					       : EDMA_PARAM_OPT_DAM;
		}

		if (req->dreq_dir == FDT_DMA_READ) {
			transfer.ep_src = req->dreq_dev_phys;
			transfer.ep_dst = seg.ds_addr;
			transfer.ep_dstbidx = acnt;
			transfer.ep_dstcidx = acnt * bcnt;
			transfer.ep_srcbidx = 0;
			transfer.ep_srccidx = 0;
		} else {
			transfer.ep_src = seg.ds_addr;
			transfer.ep_dst = req->dreq_dev_phys;
			transfer.ep_dstbidx = 0;
			transfer.ep_dstcidx = 0;
			transfer.ep_srcbidx = acnt;
			transfer.ep_srccidx = acnt * bcnt;
		}

		edma_set_param(chan, chan->ch_ownedparams[i], &transfer);
	}

	edma_transfer_enable(chan, chan->ch_ownedparams[0]);

	return 0;
}

static void
edma_fdt_halt(device_t dev, void *priv)
{
	struct edma_channel *chan = priv;

	edma_halt(chan);
}


struct edma_channel *
edma_channel_alloc(enum edma_type type, unsigned int drq,
    void (*cb)(void *), void *cbarg)
{
	struct edma_softc *sc;
	device_t dev = device_find_by_driver_unit("tiedma", 0);
	if (dev == NULL) {
		return NULL;
	}
	sc = device_private(dev);

	return edma_channel_alloc_internal(sc, type, drq, cb, cbarg);
}

/*
 * Allocate a DMA channel. Currently only DMA types are supported, not QDMA.
 * Returns NULL on failure.
 */
struct edma_channel *
edma_channel_alloc_internal(struct edma_softc *sc, enum edma_type type,
    unsigned int drq, void (*cb)(void *), void *cbarg)
{
	struct edma_channel *ch = NULL;

	KASSERT(drq < sc->sc_num_channels);
	KASSERT(type == EDMA_TYPE_DMA);	/* QDMA not implemented */
	KASSERT(cb != NULL);
	KASSERT(cbarg != NULL);

	mutex_enter(&sc->sc_lock);
	if (!edma_bit_isset(sc->sc_dmamask, drq)) {
		ch = &sc->sc_dma[drq];
		KASSERT(ch->ch_callback == NULL);
		KASSERT(ch->ch_index == drq);
		ch->ch_callback = cb;
		ch->ch_callbackarg = cbarg;
		edma_bit_set(sc->sc_dmamask, drq);
	}

	if (ch == NULL)
		goto done;

	EDMA_WRITE(sc, EDMA_DRAE_REG(0), sc->sc_dmamask[0]);
	EDMA_WRITE(sc, EDMA_DRAEH_REG(0), sc->sc_dmamask[1]);

	if (ch->ch_index < 32) {
		EDMA_WRITE(sc, EDMA_ICR_REG, __BIT(ch->ch_index));
		EDMA_WRITE(sc, EDMA_IESR_REG, __BIT(ch->ch_index));
	} else {
		EDMA_WRITE(sc, EDMA_ICRH_REG, __BIT(ch->ch_index - 32));
		EDMA_WRITE(sc, EDMA_IESRH_REG, __BIT(ch->ch_index - 32));
	}

done:
	mutex_exit(&sc->sc_lock);

	return ch;
}

/*
 * Free a DMA channel allocated with edma_channel_alloc
 */
void
edma_channel_free(struct edma_channel *ch)
{
	struct edma_softc *sc = ch->ch_sc;

	KASSERT(ch->ch_nparams == 0);

	mutex_enter(&sc->sc_lock);
	if (ch->ch_index < 32) {
		EDMA_WRITE(sc, EDMA_IECR_REG, __BIT(ch->ch_index));
	} else {
		EDMA_WRITE(sc, EDMA_IECRH_REG, __BIT(ch->ch_index - 32));
	}
	ch->ch_callback = NULL;
	ch->ch_callbackarg = NULL;
	edma_bit_clr(sc->sc_dmamask, ch->ch_index);
	mutex_exit(&sc->sc_lock);
}

/*
 * Allocate a PaRAM entry. The driver artificially restricts the number
 * of PaRAM entries available for each channel to MAX_PARAM_PER_CHANNEL.
 * If the number of entries for the channel has been exceeded, or there
 * are no entries available, 0xffff is returned.
 */
uint16_t
edma_param_alloc(struct edma_channel *ch, enum edma_param_usage usage)
{
	struct edma_softc *sc = ch->ch_sc;
	uint16_t param_entry = 0xffff;
	int idx;

	if (ch->ch_nparams == MAX_PARAM_PER_CHANNEL)
		return param_entry;

	mutex_enter(&sc->sc_lock);

	/* without channel map, some params need to be in a specific place */
	if (!sc->sc_has_chmap && usage == EDMA_PARAM_TRIGGER) {
		param_entry = ch->ch_index;
		if (edma_bit_isset(sc->sc_parammask,  param_entry)) {
			param_entry = 0xffff;
			goto out;
		}
	} else {
		/* we can allocate any PaRAM */
		//TODO: figure out why the first 32 channels aren't used
		for (idx = 32; idx < sc->sc_num_params; idx++) {
			if (!edma_bit_isset(sc->sc_parammask, idx)) {
				param_entry = idx;
				break;
			}
		}
	}
	if (param_entry != 0xffff) {
		edma_bit_set(sc->sc_parammask, param_entry);
		ch->ch_nparams++;
	}
	DPRINTF(1, (sc->sc_dev, "edma_param_alloc: giving %d\n", param_entry));

out:
	mutex_exit(&sc->sc_lock);

	return param_entry;
}

/*
 * Free a PaRAM entry allocated with edma_param_alloc
 */
static void
edma_channel_free_params(struct edma_channel *chan)
{
	struct edma_softc *sc = chan->ch_sc;

	mutex_enter(&sc->sc_lock);
	int num_params = chan->ch_nparams;
	for (int i = 0; i < num_params; i++) {
		uint16_t param_entry = chan->ch_ownedparams[i];

		KASSERT(param_entry < sc->sc_num_params);
		KASSERT(chan->ch_nparams > 0);
		KASSERT(edma_bit_isset(sc->sc_parammask, param_entry));

		edma_bit_clr(sc->sc_parammask, param_entry);
		chan->ch_nparams--;
	}
	mutex_exit(&sc->sc_lock);
}

/*
 * Update a PaRAM entry register set with caller-provided values
 */
void
edma_set_param(struct edma_channel *ch, uint16_t param_entry,
    struct edma_param *ep)
{
	struct edma_softc *sc = ch->ch_sc;

	KASSERT(param_entry < sc->sc_num_params);
	KASSERT(ch->ch_nparams > 0);
	KASSERT(edma_bit_isset(sc->sc_parammask, param_entry));

	DPRINTF(1, (sc->sc_dev, "write param entry ch# %d pe %d: 0x%08x -> 0x%08x (%u, %u, %u)\n", ch->ch_index, param_entry, ep->ep_src, ep->ep_dst, ep->ep_acnt, ep->ep_bcnt, ep->ep_ccnt));
	edma_write_param(sc, param_entry, ep);
}

/*
 * Enable a DMA channel: Point channel to the PaRam entry,
 * clear error if any, and only set the Event Enable bit.
 * The Even will either be generated by hardware, or with
 * edma_transfer_start()
 */
int
edma_transfer_enable(struct edma_channel *ch, uint16_t param_entry)
{
	struct edma_softc *sc = ch->ch_sc;
	bus_size_t off = (ch->ch_index < 32 ? 0 : 4);
	uint32_t bit = __BIT(ch->ch_index < 32 ?
			     ch->ch_index : ch->ch_index - 32);

	DPRINTF(1, (sc->sc_dev, "enable transfer ch# %d off %d bit %x pe %d\n", ch->ch_index, (int)off, bit, param_entry));

	if (sc->sc_has_chmap) {
		EDMA_WRITE(sc, EDMA_DCHMAP_REG(ch->ch_index),
		    __SHIFTIN(param_entry, EDMA_DCHMAP_PAENTRY));
	}

	uint32_t ccerr = EDMA_READ(sc, EDMA_CCERR_REG);
	if (ccerr) {
		device_printf(sc->sc_dev, " !!! CCER %08x\n", ccerr);
		EDMA_WRITE(sc, EDMA_CCERRCLR_REG, ccerr);
	}

	EDMA_WRITE(sc, EDMA_EESR_REG + off, bit);
	return 0;
}

/*
 * Software-start a DMA channel: Set the Event bit.
 */
int
edma_transfer_start(struct edma_channel *ch)
{
	struct edma_softc *sc = ch->ch_sc;
	bus_size_t off = (ch->ch_index < 32 ? 0 : 4);
	uint32_t bit = __BIT(ch->ch_index < 32 ?
			     ch->ch_index : ch->ch_index - 32);

	DPRINTF(1, (sc->sc_dev, "start transfer ch# %d off %d bit %x\n", ch->ch_index, (int)off, bit));

	EDMA_WRITE(sc, EDMA_ESR_REG + off, bit);
	return 0;
}

/*
 * Halt a DMA transfer. Called after successful transfer, or to abort
 * a transfer.
 */
void
edma_halt(struct edma_channel *ch)
{
	struct edma_softc *sc = ch->ch_sc;
	bus_size_t off = (ch->ch_index < 32 ? 0 : 4);
	uint32_t bit = __BIT(ch->ch_index < 32 ?
			     ch->ch_index : ch->ch_index - 32);

	EDMA_WRITE(sc, EDMA_EECR_REG + off, bit);
	EDMA_WRITE(sc, EDMA_ECR_REG + off, bit);
	EDMA_WRITE(sc, EDMA_SECR_REG + off, bit);
	EDMA_WRITE(sc, EDMA_EMCR_REG + off, bit);

	// TODO: implementation if we don't have chmap
	if (sc->sc_has_chmap) {
		EDMA_WRITE(sc, EDMA_DCHMAP_REG(ch->ch_index),
		    __SHIFTIN(0, EDMA_DCHMAP_PAENTRY));
	}

	edma_channel_free_params(ch);
}

uint8_t
edma_channel_index(struct edma_channel *ch)
{
	return ch->ch_index;
}


void
edma_dump(struct edma_channel *ch)
{
	static const struct {
		const char *name;
		uint16_t off;
	} regs[] = {
		{ "ER", EDMA_ER_REG },
		{ "ERH", EDMA_ERH_REG },
		{ "EER", EDMA_EER_REG },
		{ "EERH", EDMA_EERH_REG },
		{ "SER", EDMA_SER_REG },
		{ "SERH", EDMA_SERH_REG },
		{ "IER", EDMA_IER_REG },
		{ "IERH", EDMA_IERH_REG },
		{ "IPR", EDMA_IPR_REG },
		{ "IPRH", EDMA_IPRH_REG },
		{ "CCERR", EDMA_CCERR_REG },
		{ "CCSTAT", EDMA_CCSTAT_REG },
		{ "DRAE0", EDMA_DRAE_REG(0) },
		{ "DRAEH0", EDMA_DRAEH_REG(0) },
		{ NULL, 0 }
	};
	struct edma_softc *sc = ch->ch_sc;
	int i;

	for (i = 0; regs[i].name; i++) {
		device_printf(sc->sc_dev, "%s: %08x\n",
		    regs[i].name, EDMA_READ(sc, regs[i].off));
	}
	device_printf(sc->sc_dev, "DCHMAP%d: %08x\n", ch->ch_index,
	    EDMA_READ(sc, EDMA_DCHMAP_REG(ch->ch_index)));
}

void
edma_dump_param(struct edma_channel *ch, uint16_t param_entry)
{
	struct {
		const char *name;
		uint16_t off;
	} regs[] = {
		{ "OPT", EDMA_PARAM_OPT_REG(param_entry) },
		{ "SRC", EDMA_PARAM_SRC_REG(param_entry) },
		{ "CNT", EDMA_PARAM_CNT_REG(param_entry) },
		{ "DST", EDMA_PARAM_DST_REG(param_entry) },
		{ "BIDX", EDMA_PARAM_BIDX_REG(param_entry) },
		{ "LNK", EDMA_PARAM_LNK_REG(param_entry) },
		{ "CIDX", EDMA_PARAM_CIDX_REG(param_entry) },
		{ "CCNT", EDMA_PARAM_CCNT_REG(param_entry) },
		{ NULL, 0 }
	};
	struct edma_softc *sc = ch->ch_sc;
	int i;

	for (i = 0; regs[i].name; i++) {
		device_printf(sc->sc_dev, "%s%d: %08x\n",
		    regs[i].name, param_entry, EDMA_READ(sc, regs[i].off));
	}
}

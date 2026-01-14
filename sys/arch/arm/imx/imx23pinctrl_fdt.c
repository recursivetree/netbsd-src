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
 * GPIO for the imx23 using the pinctrl HW block.
 *
 * The existing gpio driver uses one device with pins numbered 0-95.
 * Unfortunately, the device tree from linux uses 3 devices "banks" with pins
 * from 0-31. We register three fdt gpio controllers and then translate between
 * these numbering schemes.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/kmem.h>

#include <dev/fdt/fdtvar.h>

#include <arm/fdt/arm_fdtvar.h>
#include <arm/imx/imx23_pinctrlvar.h>

#define NUM_GPIO_BANKS 3

struct imx23_pinctrl_fdt_softc {
	struct imx23_pinctrl_softc sc_parent;
	int bank_phandles[NUM_GPIO_BANKS];
};

struct imx23_pinctrl_fdt_pin {
	int pin_nr;
	bool pin_actlo;
};

static int imx23pinctrl_fdt_match(device_t, cfdata_t, void *);
static void imx23pinctrl_fdt_attach(device_t, device_t, void *);

static void *imx23_gpio_acquire(device_t, const void *, size_t, int);
static void imx23_gpio_release(device_t, void *);
static int imx23_gpio_read(device_t, void *, bool);
static void imx23_gpio_write(device_t, void *, int, bool);

CFATTACH_DECL_NEW(imx23pctl_fdt, sizeof(struct imx23_pinctrl_fdt_softc),
		  imx23pinctrl_fdt_match, imx23pinctrl_fdt_attach, NULL, NULL);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "fsl,imx23-pinctrl" },
	DEVICE_COMPAT_EOL
};

static const struct device_compatible_entry child_compat_data[] = {
	{ .compat = "fsl,imx23-gpio" },
	DEVICE_COMPAT_EOL
};

static struct fdtbus_gpio_controller_func imx23_gpio_funcs = {
	.acquire = imx23_gpio_acquire,
	.release = imx23_gpio_release,
	.read = imx23_gpio_read,
	.write = imx23_gpio_write
};

static void *
imx23_gpio_acquire(device_t dev, const void *data, size_t len, int flags) {
	struct imx23_pinctrl_fdt_softc * const sc = device_private(dev);
	struct imx23_pinctrl_fdt_pin *pin;
	const uint32_t *gpio = data;

	if (len != 12) return NULL;

	const int bank_phandle =
	    fdtbus_get_phandle_from_native(be32toh(gpio[0]));
	int pin_nr_offset = 0;
	for(int i=0;i<NUM_GPIO_BANKS;i++) {
		if(sc->bank_phandles[i] == bank_phandle) {
			pin_nr_offset = 32*i;
			break;
		}
	}

	const int pin_nr = pin_nr_offset + be32toh(gpio[1]);
	const bool actlo = be32toh(gpio[2]) & 1;

	pin = kmem_zalloc(sizeof(struct imx23_pinctrl_fdt_pin), KM_SLEEP);
	pin->pin_nr = pin_nr;
	pin->pin_actlo = actlo;

	gpiobus_pin_ctl(&sc->sc_parent.gc, pin->pin_nr, flags);

	return pin;
}

static void
imx23_gpio_release(device_t dev, void *priv)
{
	struct imx23_pinctrl_fdt_softc * const sc = device_private(dev);
	struct imx23_pinctrl_fdt_pin *pin = priv;

	gpiobus_pin_ctl(&sc->sc_parent.gc, pin->pin_nr, GPIO_PIN_INPUT);

	kmem_free(pin, sizeof(*pin));
}

static int
imx23_gpio_read(device_t dev, void *priv, bool raw)
{
	struct imx23_pinctrl_fdt_softc * const sc = device_private(dev);
	struct imx23_pinctrl_fdt_pin *pin = priv;
	int val;

	val = gpiobus_pin_read(&sc->sc_parent.gc, pin->pin_nr);

	if (!raw && pin->pin_actlo)
		val = !val;

	return val;
}

static void
imx23_gpio_write(device_t dev, void *priv, int val, bool raw)
{
	struct imx23_pinctrl_fdt_softc * const sc = device_private(dev);
	struct imx23_pinctrl_fdt_pin *pin = priv;

	if (!raw && pin->pin_actlo)
		val = !val;

	gpiobus_pin_write(&sc->sc_parent.gc, pin->pin_nr, val);
}

static int
imx23pinctrl_fdt_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}


static void
imx23pinctrl_fdt_attach(device_t parent, device_t self, void *aux)
{
	struct imx23_pinctrl_fdt_softc *const sc = device_private(self);
	struct fdt_attach_args *const faa = aux;
	const int phandle = faa->faa_phandle;
	int child;

	sc->sc_parent.sc_dev = self;
	sc->sc_parent.sc_iot = faa->faa_bst;

	for(int i=0;i<NUM_GPIO_BANKS;i++) {
		sc->bank_phandles[i] = 0;
	}

	bus_addr_t addr;
	bus_size_t size;
	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0) {
		aprint_error(": couldn't get register address\n");
		return;
	}
	if (bus_space_map(faa->faa_bst, addr, size, 0, &sc->sc_parent.sc_hdl)) {
		aprint_error(": couldn't map registers\n");
		return;
	}

	imx23_pinctrl_attach_common(&sc->sc_parent);

	/* Process children for fdt-capable device tree  */
	for (child = OF_child(phandle); child; child = OF_peer(child)) {
		if(!of_compatible_match(child, child_compat_data))
			continue;

		bus_addr_t gpio_instance;
		if (fdtbus_get_reg(child, 0, &gpio_instance, NULL) != 0) {
			aprint_error(": couldn't get register address\n");
			return;
		}

		if(NUM_GPIO_BANKS <= gpio_instance){
			aprint_error(": bank %ld out of range", gpio_instance);
			continue;
		}
		sc->bank_phandles[gpio_instance] = child;

		fdtbus_register_gpio_controller(self, child, &imx23_gpio_funcs);
	}
}
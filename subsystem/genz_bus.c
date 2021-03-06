/*
 * (C) Copyright 2018 Hewlett Packard Enterprise Development LP.
 * All rights reserved.
 *
 * This source code file is part of the EmerGen-Z project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "genz_baseline.h"
#include "genz_device.h"

MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

//-------------------------------------------------------------------------
// Boolean

static int genz_match(struct device *dev, struct device_driver *drv)
{
	struct genz_device __unused *genz_dev = to_genz_dev(dev);

	pr_info("%s()\n", __FUNCTION__);
	return 1;
};

//-------------------------------------------------------------------------

static int genz_num_vf(struct device *dev)
{
	pr_info("%s()\n", __FUNCTION__);
	return 1;
}

//-------------------------------------------------------------------------
// Kernel bitches at you without one of these.

static int genz_remove_one_device(struct device *dev)
{
	struct genz_device __unused *genz_dev = to_genz_dev(dev);
	// struct genz_driver *drv = genz_dev->driver;

	pr_info("%s()\n", __FUNCTION__);
	return 0;
}

//-----------------------------------------------------------------------
// FIXME: how is this called?

static int genz_dev_init(struct genz_device *dev)
{
	pr_info("%s()\n", __FUNCTION__);

	return 0;
}

static void genz_dev_uninit(struct genz_device *dev)
{
	pr_info("%s()\n", __FUNCTION__);
}

static const struct genz_device_ops devops = {
	.init		= genz_dev_init,
	.uninit		= genz_dev_uninit,
};

//-----------------------------------------------------------------------
// This is a global common setup for all genz_devs, followed by a "personal"
// customization callback.  See the dummy driver and alloc_netdev.

struct genz_device *alloc_genzdev(const char *namefmt,
				  void (*customize_cb)(struct genz_device *))
{
	struct genz_device *genz_dev;

	pr_info("%s()\n", __FUNCTION__);

	if (!(genz_dev = kzalloc(sizeof(struct genz_device), GFP_KERNEL))) {
		pr_err("%s() failed kzalloc\n", __FUNCTION__);
		return NULL;
	}
	// FIXME: does it need to be 32-byte aligned like alloc_netdev_mqs?
	strcpy(genz_dev->namefmt, namefmt);
	customize_cb(genz_dev);
	return genz_dev;
}
EXPORT_SYMBOL(alloc_genzdev);

//-----------------------------------------------------------------------

static struct genz_device *the_one = NULL;

int register_genzdev(struct genz_device *genz_dev) {
	pr_info("%s()\n", __FUNCTION__);
	if (the_one)
		return -EALREADY;
	the_one = genz_dev;
	return 0;
}
EXPORT_SYMBOL(register_genzdev);

//-----------------------------------------------------------------------

void unregister_genzdev(struct genz_device *genz_dev) {
	pr_info("%s()\n", __FUNCTION__);
	if (the_one && the_one == genz_dev)
		the_one = NULL;
}
EXPORT_SYMBOL(unregister_genzdev);

//-----------------------------------------------------------------------
// A callback for the global alloc_genzdev()

static void genz_device_customize(struct genz_device *genz_dev)
{
	pr_info("%s()\n", __FUNCTION__);
}

int genz_init_one(struct device *dev)
{
	struct genz_device *genz_dev;
	int ret = 0;

	pr_info("%s()\n", __FUNCTION__);

	if (!(genz_dev = alloc_genzdev("genz%02d", genz_device_customize))) {
		pr_err("%s()->alloc_genzdev failed \n", __FUNCTION__);
		return -ENOMEM;		// It had ONE job...
	}

	if ((ret = register_genzdev(genz_dev)))
		pr_err("%s()->register_genzdev() failed\n", __FUNCTION__);

	return ret;
}

struct bus_type genz_bus = {		// Only one directly in /sys/bus
	.name = "genz",
	.dev_name = "genz%u",		// "subsystem enumeration"
	.dev_root = NULL,		// "Default parent device"
	.match = genz_match,		// Verify driver<->device permutations
	.probe = genz_init_one,		// if match() call drv.probe
	.remove = genz_remove_one_device,
	.num_vf = genz_num_vf,
};

#define MAXBUSES	254		// Fit into "0x%02x" format
#define POTPOURRID	(MAXBUSES + 1)	// The "pick it for me" bus

static DEFINE_MUTEX(bus_mutex);
static LIST_HEAD(bus_list);

struct genz_bus_entry {
	struct list_head bus_lister;
	unsigned id;			// from hints to genz_find_bus_by_xxx
	struct device bus_dev;
};

#define to_genz_bus(pDeV) container_of(pDeV, struct genz_bus_entry, bus_dev);

struct device *genz_find_bus_by_instance(int desired)
{
	struct genz_bus_entry *tmp, *foundit = NULL;

	if (desired > MAXBUSES)
		return NULL;
	if (desired < 0)
		desired = POTPOURRID;

	mutex_lock(&bus_mutex);

	foundit = NULL;
	list_for_each_entry(tmp, &bus_list, bus_lister) {
		if (desired == tmp->id) {
			foundit = tmp;
			break;
		}
	}
	if (!foundit) {
		if (!(foundit = kzalloc(sizeof(*foundit), GFP_KERNEL)))
			goto all_done;
		foundit->id = desired;
		INIT_LIST_HEAD(&foundit->bus_lister);

	// LDD3:14 Device Model -> "Device Registration"; see also source for
	// "subsys_register()".  Need a separate object from bus to form an
	// anchor point; it's not a fully fleshed-out struct device but serves
	// the anchor purpose.  device_register() does both of these but I
	// want to change the bus and name, so do it in two pieces.
	// .parent = NULL (ie, after kzalloc) lands at the top of /sys/devices
	// which seems good.  Start with the lists/mutex/kobj of struct device.

		device_initialize(&(foundit->bus_dev));
		foundit->bus_dev.bus = &genz_bus;
		dev_set_name(&foundit->bus_dev, "genz%02x", foundit->id);
		if (device_add(&foundit->bus_dev)) {
			pr_err("%s()->device_add(0x%02x) failed\n",
				__FUNCTION__, foundit->id);
			kfree(foundit);
			foundit = NULL;
			goto all_done;
		}
		list_add_tail(&foundit->bus_lister, &bus_list);
	}

all_done:
	mutex_unlock(&bus_mutex);

	return foundit ? &(foundit->bus_dev) : NULL;
}

void genz_bus_exit(void)
{
	struct genz_bus_entry *bus, *tmp;

	pr_info("%s()\n", __FUNCTION__);
	list_for_each_entry_safe(bus, tmp, &bus_list, bus_lister) {
		// if device_add() was called, must use this
		device_del(&bus->bus_dev);	
		put_device(&bus->bus_dev);
		kfree(bus);
	}
	bus_unregister(&genz_bus);
	genz_classes_destroy();
}

int __init genz_bus_init(void)
{
	int ret = 0;

	pr_info("%s()\n", __FUNCTION__);

	if ((ret = genz_classes_init())) {
		pr_err("%s()->genz_classes_init() failed\n", __FUNCTION__);
		return ret;
	}
	if ((ret = bus_register(&genz_bus))) {
		pr_err("%s()->bus_register() failed\n", __FUNCTION__);
		genz_classes_destroy();
		return ret;
	}
	return ret;
}

module_init(genz_bus_init);
module_exit(genz_bus_exit);

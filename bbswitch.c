/**
 * Disable discrete graphics (currently nvidia only)
 *
 * Usage:
 * Disable discrete card
 * # echo OFF > /proc/acpi/bbswitch
 * Enable discrete card
 * # echo ON > /proc/acpi/bbswitch
 * Get status
 * # cat /proc/acpi/bbswitch
 */
/*
 *  Copyright (C) 2011-2013 Bumblebee Project
 *  Author: Peter Wu <lekensteyn@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/suspend.h>
#include <linux/seq_file.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/vga_switcheroo.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)
#   define ACPI_HANDLE DEVICE_ACPI_HANDLE
#endif

#define BBSWITCH_VERSION "0.8"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Toggle the discrete graphics card");
MODULE_AUTHOR("Peter Wu <lekensteyn@gmail.com>");
MODULE_VERSION(BBSWITCH_VERSION);

enum {
    CARD_UNCHANGED = -1,
    CARD_OFF = 0,
    CARD_ON = 1,
};

static int load_state = CARD_UNCHANGED;
MODULE_PARM_DESC(load_state, "Initial card state (0 = off, 1 = on, -1 = unchanged)");
module_param(load_state, int, 0400);
static int unload_state = CARD_UNCHANGED;
MODULE_PARM_DESC(unload_state, "Card state on unload (0 = off, 1 = on, -1 = unchanged)");
module_param(unload_state, int, 0600);
static bool skip_optimus_dsm = false;
MODULE_PARM_DESC(skip_optimus_dsm, "Skip probe of Optimus discrete DSM (default = false)");
module_param(skip_optimus_dsm, bool, 0400);

extern struct proc_dir_entry *acpi_root_dir;

static const char acpi_optimus_dsm_muid[16] = {
    0xF8, 0xD8, 0x86, 0xA4, 0xDA, 0x0B, 0x1B, 0x47,
    0xA7, 0x2B, 0x60, 0x42, 0xA6, 0xB5, 0xBE, 0xE0,
};

static const char acpi_nvidia_dsm_muid[16] = {
    0xA0, 0xA0, 0x95, 0x9D, 0x60, 0x00, 0x48, 0x4D,
    0xB3, 0x4D, 0x7E, 0x5F, 0xEA, 0x12, 0x9F, 0xD4
};

/*
The next UUID has been found as well in
https://bugs.launchpad.net/lpbugreporter/+bug/752542:

0xD3, 0x73, 0xD8, 0x7E, 0xD0, 0xC2, 0x4F, 0x4E,
0xA8, 0x54, 0x0F, 0x13, 0x17, 0xB0, 0x1C, 0x2C 
It looks like something for Intel GPU:
http://lxr.linux.no/#linux+v3.1.5/drivers/gpu/drm/i915/intel_acpi.c
 */

#define DSM_TYPE_UNSUPPORTED    0
#define DSM_TYPE_OPTIMUS        1
#define DSM_TYPE_NVIDIA         2
static int dsm_type = DSM_TYPE_UNSUPPORTED;

/* The cached name of the discrete device (of the form "0000:01:00.0"). */
static char dis_dev_name[16];
/* dis_dev is non-NULL iff it is currently bound by bbswitch (and off). */
static struct pci_dev *dis_dev;
static acpi_handle dis_handle;

/* The PM domain that wraps the PCI device, used to ensure that power is
 * available before the device is put in D0. ("Nvidia" DSM and PR3). */
static struct dev_pm_domain pm_domain;

static char *buffer_to_string(const char *buffer, size_t n, char *target) {
    int i;
    for (i=0; i<n; i++) {
        snprintf(target + i * 5, 5 * (n - i), "0x%02X,", buffer ? buffer[i] & 0xFF : 0);
    }
    return target;
}

// Returns 0 if the call succeeded and non-zero otherwise. If the call
// succeeded, the result is stored in "result" providing that the result is an
// integer or a buffer containing 4 values
static int acpi_call_dsm(acpi_handle handle, const char muid[16], int revid,
    int func, char args[4], uint32_t *result) {
    struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
    struct acpi_object_list input;
    union acpi_object params[4];
    union acpi_object *obj;
    int err;

    input.count = 4;
    input.pointer = params;
    params[0].type = ACPI_TYPE_BUFFER;
    params[0].buffer.length = 16;
    params[0].buffer.pointer = (char *)muid;
    params[1].type = ACPI_TYPE_INTEGER;
    params[1].integer.value = revid;
    params[2].type = ACPI_TYPE_INTEGER;
    params[2].integer.value = func;
    /* Although the ACPI spec defines Arg3 as a Package, in practise
     * implementations expect a Buffer (CreateWordField and Index functions are
     * applied to it). */
    params[3].type = ACPI_TYPE_BUFFER;
    params[3].buffer.length = 4;
    if (args) {
        params[3].buffer.pointer = args;
    } else {
        // Some implementations (Asus U36SD) seem to check the args before the
        // function ID and crash if it is not a buffer.
        params[3].buffer.pointer = (char[4]){0, 0, 0, 0};
    }

    err = acpi_evaluate_object(handle, "_DSM", &input, &output);
    if (err) {
        struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
        char muid_str[5 * 16];
        char args_str[5 * 4];

        acpi_get_name(handle, ACPI_FULL_PATHNAME, &buf);

        pr_warn("failed to evaluate %s._DSM {%s} 0x%X 0x%X {%s}: %s\n",
            (char *)buf.pointer,
            buffer_to_string(muid, 16, muid_str), revid, func,
            buffer_to_string(args,  4, args_str), acpi_format_exception(err));
        return err;
    }

    obj = (union acpi_object *)output.pointer;

    if (obj->type == ACPI_TYPE_INTEGER && result) {
        *result = obj->integer.value;
    } else if (obj->type == ACPI_TYPE_BUFFER) {
        if (obj->buffer.length == 4 && result) {
            *result = 0;
            *result |= obj->buffer.pointer[0];
            *result |= (obj->buffer.pointer[1] << 8);
            *result |= (obj->buffer.pointer[2] << 16);
            *result |= (obj->buffer.pointer[3] << 24);
        }
    } else {
        pr_warn("_DSM call yields an unsupported result type: %#x\n",
            obj->type);
    }

    kfree(output.pointer);
    return 0;
}

// Returns 1 if a _DSM function and its function index exists and 0 otherwise
static int has_dsm_func(const char muid[16], int revid, int sfnc) {
    u32 result = 0;

    // fail if the _DSM call failed
    if (acpi_call_dsm(dis_handle, muid, revid, 0, 0, &result))
        return 0;

    // ACPI Spec v4 9.14.1: if bit 0 is zero, no function is supported. If
    // the n-th bit is enabled, function n is supported
    return result & 1 && result & (1 << sfnc);
}

static int bbswitch_optimus_dsm(void) {
    if (dsm_type == DSM_TYPE_OPTIMUS) {
        char args[] = {1, 0, 0, 3};
        u32 result = 0;

        if (acpi_call_dsm(dis_handle, acpi_optimus_dsm_muid, 0x100, 0x1A, args,
            &result)) {
            // failure
            return 1;
        }
        pr_debug("Result of Optimus _DSM call: %08X\n", result);
    }
    return 0;
}

static int bbswitch_acpi_off(void) {
    if (dsm_type == DSM_TYPE_NVIDIA) {
        char args[] = {2, 0, 0, 0};
        u32 result = 0;

        if (acpi_call_dsm(dis_handle, acpi_nvidia_dsm_muid, 0x102, 0x3, args,
            &result)) {
            // failure
            return 1;
        }
        pr_debug("Result of _DSM call for OFF: %08X\n", result);
    }
    return 0;
}

static int bbswitch_acpi_on(void) {
    if (dsm_type == DSM_TYPE_NVIDIA) {
        char args[] = {1, 0, 0, 0};
        u32 result = 0;

        if (acpi_call_dsm(dis_handle, acpi_nvidia_dsm_muid, 0x102, 0x3, args,
            &result)) {
            // failure
            return 1;
        }
        pr_debug("Result of _DSM call for ON: %08X\n", result);
    }
    return 0;
}

// Returns 1 if the card is disabled, 0 if enabled
static int is_card_disabled(void) {
    /* Assume that the device is disabled when our PCI driver found a device. */
    return dis_dev != NULL;
}


/* Power source handling. */

static int bbswitch_pmd_runtime_suspend(struct device *dev)
{
    int ret;

    pr_debug("Preparing for runtime suspend.\n");

    /* Put the device in D3. */
    ret = dev->bus->pm->runtime_suspend(dev);
    if (ret)
        return ret;

    bbswitch_acpi_off();
    /* TODO For PR3, disable them. */
    return 0;
}

static int bbswitch_pmd_runtime_resume(struct device *dev)
{
    pr_info("enabling discrete graphics\n");

    bbswitch_acpi_on();
    /* TODO For PR3, enable them. */

    /* Now ensure that the device is actually put in D0 by PCI. */
    return dev->bus->pm->runtime_resume(dev);
}

static void bbswitch_pmd_set(struct device *dev)
{
    pm_domain.ops = *dev->bus->pm;
    pm_domain.ops.runtime_resume = bbswitch_pmd_runtime_resume;
    pm_domain.ops.runtime_suspend = bbswitch_pmd_runtime_suspend;
    dev_pm_domain_set(dev, &pm_domain);
}


/* Nvidia device itself. */

static int bbswitch_pci_runtime_suspend(struct device *dev)
{
    struct pci_dev *pdev = to_pci_dev(dev);

    pr_info("disabling discrete graphics\n");

    /* Ensure that the audio driver knows not to touch us. */
    vga_switcheroo_set_dynamic_switch(pdev, VGA_SWITCHEROO_OFF);

    bbswitch_optimus_dsm();

    /* Save state now that the device is still awake, makes PCI layer happy */
    pci_save_state(pdev);
    /* TODO if _PR3 is supported, should this be PCI_D3hot? */
    pci_set_power_state(pdev, PCI_D3hot);
    return 0;
}

static int bbswitch_pci_runtime_resume(struct device *dev)
{
    struct pci_dev *pdev = to_pci_dev(dev);

    pr_debug("Finishing runtime resume.\n");

    /* Resume audio driver. */
    vga_switcheroo_set_dynamic_switch(pdev, VGA_SWITCHEROO_ON);
    return 0;
}

static const struct dev_pm_ops bbswitch_pci_pm_ops = {
    .runtime_suspend = bbswitch_pci_runtime_suspend,
    .runtime_resume = bbswitch_pci_runtime_resume,
    /* No runtime_idle callback, the default zero delay is sufficient. */
};

static int bbswitch_switcheroo_switchto(enum vga_switcheroo_client_id id)
{
    /* We do not support switching, only power on/off. */
    return -ENOSYS;
}

static enum vga_switcheroo_client_id bbswitch_switcheroo_get_client_id(struct pci_dev *pdev)
{
    /* Our registered client is always the discrete GPU. */
    return VGA_SWITCHEROO_DIS;
}

static const struct vga_switcheroo_handler bbswitch_handler = {
    .switchto = bbswitch_switcheroo_switchto,
    .get_client_id = bbswitch_switcheroo_get_client_id,
};


static void bbswitch_switcheroo_set_gpu_state(struct pci_dev *pdev, enum vga_switcheroo_state state)
{
    /* Nothing to do, we handle the PM domain ourselves. Perhaps we can add
     * backwards compatibility with older kernels in this way and workaround
     * bugs? */
    pr_debug("set_gpu_state to %s\n", state == VGA_SWITCHEROO_ON ? "ON" : "OFF");
}

static bool bbswitch_switcheroo_can_switch(struct pci_dev *pdev)
{
    /* We do not support switching between IGD/DIS. */
    return false;
}

static const struct vga_switcheroo_client_ops bbswitch_switcheroo_ops = {
    .set_gpu_state = bbswitch_switcheroo_set_gpu_state,
    .can_switch = bbswitch_switcheroo_can_switch,
};

static int bbswitch_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    /* Only bind to the device which we discovered before. */
    if (strcmp(dev_name(&pdev->dev), dis_dev_name))
        return -ENODEV;

    pr_debug("Found PCI device\n");
    dis_dev = pdev;

    bbswitch_pmd_set(&pdev->dev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,5,0)
    vga_switcheroo_register_handler(&bbswitch_handler, 0);
#else
    vga_switcheroo_register_handler(&bbswitch_handler);
#endif
    vga_switcheroo_register_client(pdev, &bbswitch_switcheroo_ops, true);

    /* Prevent kernel from detaching the PCI device for some devices that
     * generate hotplug events. The graphics card is typically not physically
     * removable. */
    pci_ignore_hotplug(pdev);

    pm_runtime_set_active(&pdev->dev); /* clear any errors */
    /* Use autosuspend to avoid lspci waking up the device multiple times. */
    pm_runtime_set_autosuspend_delay(&pdev->dev, 2000);
    pm_runtime_use_autosuspend(&pdev->dev);
    pm_runtime_allow(&pdev->dev);
    pm_runtime_put_autosuspend(&pdev->dev);
    return 0;
}

static void bbswitch_pci_remove(struct pci_dev *pdev)
{
    pr_debug("Removing PCI device\n");

    pm_runtime_get_noresume(&pdev->dev);
    pm_runtime_dont_use_autosuspend(&pdev->dev);
    pm_runtime_forbid(&pdev->dev);

    vga_switcheroo_unregister_client(pdev);
    vga_switcheroo_unregister_handler();
    dev_pm_domain_set(&pdev->dev, NULL);

    dis_dev = NULL;
}

static const struct pci_device_id pciidlist[] = {
    { PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID),
      PCI_CLASS_DISPLAY_VGA << 8, 0xffff00 },
    { PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID),
      PCI_CLASS_DISPLAY_3D << 8, 0xffff00 },
    { 0, 0, 0 },
};

static struct pci_driver bbswitch_pci_driver = {
    .name = KBUILD_MODNAME,
    .id_table = pciidlist,
    .probe  = bbswitch_pci_probe,
    .remove = bbswitch_pci_remove,
    .driver.pm = &bbswitch_pci_pm_ops,
};


static void bbswitch_off(void) {
    int ret;

    /* Do nothing if the device was disabled before. */
    if (dis_dev)
        return;

    ret = pci_register_driver(&bbswitch_pci_driver);
    if (ret) {
        pr_warn("Cannot register PCI device\n");
        return;
    }

    /* If the probe failed, remove the driver such that it can be reprobed on
     * the next registration. */
    if (!dis_dev) {
#if 0
        /* TODO discover the other driver name if possible. */
        pr_warn("device %s is in use by driver '%s', refusing OFF\n",
            dev_name(&dis_dev->dev), dis_dev->driver->name);
#endif

        pr_warn("Could not bind to device, is it in use by an other driver?\n");
        pci_unregister_driver(&bbswitch_pci_driver);
    }
}

static void bbswitch_on(void) {
    /* Do nothing if no device exists that was previously disabled. */
    if (!dis_dev)
        return;

    pci_unregister_driver(&bbswitch_pci_driver);
}

static ssize_t bbswitch_proc_write(struct file *fp, const char __user *buff,
    size_t len, loff_t *off) {
    char cmd[8];

    if (len >= sizeof(cmd))
        len = sizeof(cmd) - 1;

    if (copy_from_user(cmd, buff, len))
        return -EFAULT;

    if (strncmp(cmd, "OFF", 3) == 0)
        bbswitch_off();

    if (strncmp(cmd, "ON", 2) == 0)
        bbswitch_on();

    return len;
}

static int bbswitch_proc_show(struct seq_file *seqfp, void *p) {
    // show the card state. Example output: 0000:01:00:00 ON
    seq_printf(seqfp, "%s %s\n", dis_dev_name,
             is_card_disabled() ? "OFF" : "ON");
    return 0;
}
static int bbswitch_proc_open(struct inode *inode, struct file *file) {
    return single_open(file, bbswitch_proc_show, NULL);
}

static struct file_operations bbswitch_fops = {
    .open   = bbswitch_proc_open,
    .read   = seq_read,
    .write  = bbswitch_proc_write,
    .llseek = seq_lseek,
    .release= single_release
};

static int __init bbswitch_init(void) {
    struct proc_dir_entry *acpi_entry;
    struct pci_dev *pdev = NULL;
    acpi_handle igd_handle = NULL;

    pr_info("version %s\n", BBSWITCH_VERSION);

    while ((pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev)) != NULL) {
        struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
        acpi_handle handle;
        int pci_class = pdev->class >> 8;

        if (pci_class != PCI_CLASS_DISPLAY_VGA &&
            pci_class != PCI_CLASS_DISPLAY_3D)
            continue;

        handle = ACPI_HANDLE(&pdev->dev);
        if (!handle) {
            pr_warn("cannot find ACPI handle for VGA device %s\n",
                dev_name(&pdev->dev));
            continue;
        }

        acpi_get_name(handle, ACPI_FULL_PATHNAME, &buf);

        if (pdev->vendor == PCI_VENDOR_ID_INTEL) {
            igd_handle = handle;
            pr_info("Found integrated VGA device %s: %s\n",
                dev_name(&pdev->dev), (char *)buf.pointer);
        } else {
            strlcpy(dis_dev_name, dev_name(&pdev->dev), sizeof(dis_dev_name));
            dis_handle = handle;
            pr_info("Found discrete VGA device %s: %s\n",
                dev_name(&pdev->dev), (char *)buf.pointer);
        }
        kfree(buf.pointer);
    }

    if (dis_handle == NULL) {
        pr_err("No discrete VGA device found\n");
        return -ENODEV;
    }

    if (!skip_optimus_dsm &&
            has_dsm_func(acpi_optimus_dsm_muid, 0x100, 0x1A)) {
        dsm_type = DSM_TYPE_OPTIMUS;
        pr_info("detected an Optimus _DSM function\n");
    } else if (has_dsm_func(acpi_nvidia_dsm_muid, 0x102, 0x3)) {
        dsm_type = DSM_TYPE_NVIDIA;
        pr_info("detected a nVidia _DSM function\n");
    } else {
       /* At least two Acer machines are known to use the intel ACPI handle
        * with the legacy nvidia DSM */
        dis_handle = igd_handle;
        if (dis_handle && has_dsm_func(acpi_nvidia_dsm_muid, 0x102, 0x3)) {
            dsm_type = DSM_TYPE_NVIDIA;
            pr_info("detected a nVidia _DSM function on the"
                " integrated video card\n");
        } else {
            pr_err("No suitable _DSM call found.\n");
            return -ENODEV;
        }
    }

    acpi_entry = proc_create("bbswitch", 0664, acpi_root_dir, &bbswitch_fops);
    if (acpi_entry == NULL) {
        pr_err("Couldn't create proc entry\n");
        return -ENOMEM;
    }

    if (load_state == CARD_OFF)
        bbswitch_off();

    pr_info("Succesfully loaded. Discrete card %s is %s\n",
        dis_dev_name, is_card_disabled() ? "off" : "on");

    return 0;
}

static void __exit bbswitch_exit(void) {
    remove_proc_entry("bbswitch", acpi_root_dir);

    bbswitch_on();
    pr_info("Unloaded\n");
}

module_init(bbswitch_init);
module_exit(bbswitch_exit);

/* vim: set sw=4 ts=4 et: */

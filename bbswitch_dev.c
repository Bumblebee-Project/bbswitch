/*
 * TODO merge into main bbswitch module.
 * TODO on ON call device_release_driver
 * TODO how to bind to a specific device from kernel space? Can't use
 *      driver_probe_device (https://lkml.org/lkml/2014/2/14/628). Maybe use
 *      driver_override or new_id/bind/remove_id from userspace?
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Toggle the discrete graphics card (PCI driver)");
MODULE_AUTHOR("Peter Wu <peter@lekensteyn.nl>");

static int bbswitch_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
    /* TODO how to discover devices? */
    /* Prevent kernel from detaching the PCI device for some devices that
     * generate hotplug events. The graphics card is typically not physically
     * removable. */
    pci_ignore_hotplug(dev);

    pm_runtime_set_active(&dev->dev); /* clear any errors */
    pm_runtime_allow(&dev->dev);
    pm_runtime_put_noidle(&dev->dev);
    return 0;
}

static void bbswitch_pci_remove(struct pci_dev *dev)
{
    pm_runtime_get_noresume(&dev->dev);
}

static int bbswitch_runtime_suspend(struct device *dev) {
    struct pci_dev *pdev = to_pci_dev(dev);

    pr_info("disabling discrete graphics\n");

    /* TODO if _PR3 is not supported, call Optimus DSM here. */
    /* TODO for v1 Optimus, call DSM here. */

    /* Save state now that the device is still awake, makes PCI layer happy */
    pci_save_state(pdev);
    /* TODO if _PR3 is supported, should this be PCI_D3hot? */
    pci_set_power_state(pdev, PCI_D3cold);
    return 0;
}

static int bbswitch_runtime_resume(struct device *dev) {
    pr_info("enabling discrete graphics\n");

    /* TODO for v1 Optimus, call DSM here. */
    /* Nothing to do for Optimus, the PCI layer already moved into D0 state. */
    return 0;
}

static struct dev_pm_ops bbswitch_pm_ops = {
    .runtime_suspend = bbswitch_runtime_suspend,
    .runtime_resume = bbswitch_runtime_resume,
    /* No runtime_idle callback, the default zero delay is sufficient. */
};

static struct pci_driver bbswitch_pci_driver = {
    .name = KBUILD_MODNAME,
    .id_table = NULL, /* will be added dynamically */
    .probe  = bbswitch_pci_probe,
    .remove = bbswitch_pci_remove,
    .driver.pm = &bbswitch_pm_ops,
};

static int __init bbswitch_dev_init(void) {
    int ret;

    ret = pci_register_driver(&bbswitch_pci_driver);
#if 0
    ret = pci_add_dynid(&bbswitch_pci_driver, PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID,
            PCI_ANY_ID, PCI_ANY_ID, PCI_BASE_CLASS_DISPLAY << 16, 0xff0000);
#endif

    return ret;
}

static void __exit bbswitch_dev_exit(void) {
    pci_unregister_driver(&bbswitch_pci_driver);
}

module_init(bbswitch_dev_init);
module_exit(bbswitch_dev_exit);

/* vim: set sw=4 ts=4 et: */

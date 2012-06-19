/**
 * Very ugly hack to work around a wrongly detected ACPI handle, see
 * https://bugzilla.kernel.org/show_bug.cgi?id=42696
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/dmi.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Dirty ACPI handle hack for Lenovo IdeaPad Y[45]70");
MODULE_AUTHOR("Peter Lekensteyn <lekensteyn@gmail.com>");
MODULE_VERSION("0.0.1");

static struct pci_dev *dis_dev;
static acpi_handle orig_handle;

/**
 * Returns true if the system needs an ACPI handle hack
 */
static bool __init need_acpi_handle_hack(void) {
	return dmi_match(DMI_PRODUCT_VERSION, "Lenovo IdeaPad Y470             ")
		|| dmi_match(DMI_PRODUCT_VERSION, "Lenovo IdeaPad Y570             ")
		|| dmi_match(DMI_PRODUCT_VERSION, "LENOVO IDEAPAD Y570 ") /* sys-product-name: PIQY0 */
		|| dmi_match(DMI_PRODUCT_VERSION, "Lenovo IdeaPad Y580")
		|| dmi_match(DMI_PRODUCT_VERSION, "PSPLBE-01V00HFR") /* TOSHIBA SATELLITE P870 */
		;
}

static struct pci_dev __init *get_discrete_device(void) {
	struct pci_dev *pdev = NULL;
	int class = PCI_CLASS_DISPLAY_VGA << 8;
	while ((pdev = pci_get_class(class, pdev)) != NULL) {
		if (pdev->vendor != PCI_VENDOR_ID_INTEL) {
			return pdev;
		}
	}
	return NULL;
}

/**
 * Very ugly hack to set the ACPI handle, do not use this as exemplary code!
 */
static void dev_set_acpi_handle(struct pci_dev *pdev, acpi_handle handle) {
	pdev->dev.archdata.acpi_handle = handle;
}

static int __init hack_apply(void) {
	acpi_handle tmp_handle, new_handle;
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
	if (!need_acpi_handle_hack()) {
		printk(KERN_ERR "Machine does not need ACPI handle hack\n");
		return -ENODEV;
	}
	dis_dev = get_discrete_device();
	if (!dis_dev) {
		printk(KERN_ERR "No discrete video card found\n");
		return -ENODEV;
	}
	orig_handle = DEVICE_ACPI_HANDLE(&dis_dev->dev);
	if (!orig_handle) {
		printk(KERN_ERR "No ACPI handle found for discrete\n");
		return -ENODEV;
	}
	acpi_get_name(orig_handle, ACPI_SINGLE_NAME, &buf);
	if (strcmp((char *)buf.pointer, "PEGP") == 0) {
		printk(KERN_ERR "Handle has already be changed\n");
		return -ENODEV;
	}
	/* \_SB.PCI0.PEG0.VGA_ -> \_SB.PCI0.PEG0.PEGP */
	acpi_get_parent(orig_handle, &tmp_handle);
	acpi_get_handle(tmp_handle, "PEGP", &new_handle);
	printk(KERN_INFO "Setting new ACPI handle for discrete video card\n");
	dev_set_acpi_handle(dis_dev, new_handle);
	pci_dev_put(dis_dev);
	return 0;
}

static void __exit hack_undo(void) {
	if (orig_handle) {
		printk(KERN_INFO "Restoring original ACPI handle for discrete"
			" video card\n");
		dev_set_acpi_handle(dis_dev, orig_handle);
	}
}

module_init(hack_apply);
module_exit(hack_undo);

/**
 * Very ugly hack to work around a wrongly detected ACPI handle, see
 * https://bugzilla.kernel.org/show_bug.cgi?id=42696
 * https://bugzilla.kernel.org/show_bug.cgi?id=60829
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/dmi.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Dirty ACPI handle hack for Lenovo IdeaPad Y[45]70");
MODULE_AUTHOR("Peter Lekensteyn <lekensteyn@gmail.com>");
MODULE_VERSION("0.0.2");

static struct pci_dev *dis_dev;
static acpi_handle orig_handle;

/**
 * Returns true if the system needs an ACPI handle hack
 */
static bool __init need_acpi_handle_hack(void) {
	return dmi_match(DMI_PRODUCT_VERSION, "Lenovo IdeaPad Y470             ")
		|| dmi_match(DMI_PRODUCT_VERSION, "Lenovo IdeaPad Y480")	
		|| dmi_match(DMI_PRODUCT_VERSION, "Lenovo IdeaPad Y570             ")
		|| dmi_match(DMI_PRODUCT_VERSION, "LENOVO IDEAPAD Y570 ") /* sys-product-name: PIQY0 */
		|| dmi_match(DMI_PRODUCT_VERSION, "Lenovo IdeaPad Y580")
		|| dmi_match(DMI_PRODUCT_VERSION, "Lenovo IdeaPad U510")
		|| dmi_match(DMI_PRODUCT_VERSION, "PSPLBE-01V00HFR") /* TOSHIBA SATELLITE P870 */
		|| dmi_match(DMI_PRODUCT_VERSION, "PSPLBA-02300S") /* TOSHIBA Satellite P870 */
		|| dmi_match(DMI_PRODUCT_VERSION, "PSPLFE-00E009FR") /* TOSHIBA Satellite P870 */
		|| dmi_match(DMI_PRODUCT_VERSION, "Lenovo G580")
		|| dmi_match(DMI_PRODUCT_VERSION, "Lenovo G780")
		|| dmi_match(DMI_PRODUCT_VERSION, "Lenovo IdeaPad Z500")
		|| (dmi_match(DMI_SYS_VENDOR, "LENOVO") && dmi_match(DMI_PRODUCT_NAME, "PIQY0")) /* Lenovo IdeaPad Y570 */
		|| dmi_match(DMI_PRODUCT_NAME, "Aspire V5-573G")
		|| dmi_match(DMI_PRODUCT_NAME, "Aspire V5-573PG")
		;
}

static struct pci_dev __init *get_discrete_device(void) {
	struct pci_dev *pdev = NULL;
	while ((pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev)) != NULL) {
		int pci_class = pdev->class >> 8;

		if (pci_class != PCI_CLASS_DISPLAY_VGA &&
			pci_class != PCI_CLASS_DISPLAY_3D)
			continue;

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
#ifdef ACPI_HANDLE_SET
	ACPI_HANDLE_SET(&pdev->dev, handle);
#else
	/* for Linux 3.7 and earlier */
	pdev->dev.archdata.acpi_handle = handle;
#endif
}

static int __init hack_apply(void) {
	acpi_handle tmp_handle, new_handle;
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
	if (!need_acpi_handle_hack()) {
		pr_err("Machine does not need ACPI handle hack\n");
		return -ENODEV;
	}
	dis_dev = get_discrete_device();
	if (!dis_dev) {
		pr_err("No discrete video card found\n");
		return -ENODEV;
	}

#ifdef ACPI_HANDLE
	/* since Linux 3.8 */
	orig_handle = ACPI_HANDLE(&dis_dev->dev);
#else
	/* removed since Linux 3.13 */
	orig_handle = DEVICE_ACPI_HANDLE(&dis_dev->dev);
#endif
	if (!orig_handle) {
		pr_err("No ACPI handle found for discrete video card\n");
		goto free_dev;
	}
	if (ACPI_FAILURE(acpi_get_name(orig_handle, ACPI_SINGLE_NAME, &buf))) {
		pr_err("Could not acquire name for discrete video card\n");
		goto free_dev;
	}
	if (strcmp((char *)buf.pointer, "PEGP") == 0) {
		pr_err("Handle has already been changed to PEGP\n");
		goto free_name;
	}
	/* \_SB.PCI0.PEG0.VGA_ -> \_SB.PCI0.PEG0.PEGP */
	if (ACPI_FAILURE(acpi_get_parent(orig_handle, &tmp_handle))) {
		pr_err("No parent device found for %s\n", (char *)buf.pointer);
		goto free_name;
	}
	if (ACPI_FAILURE(acpi_get_handle(tmp_handle, "PEGP", &new_handle))) {
		pr_err("No PEGP handle found on %s\n", (char *)buf.pointer);
		goto free_name;
	}
	pr_info("Setting new ACPI handle for discrete video card\n");
	dev_set_acpi_handle(dis_dev, new_handle);
	kfree(buf.pointer);
	pci_dev_put(dis_dev);
	return 0;
free_name:
	kfree(buf.pointer);
free_dev:
	pci_dev_put(dis_dev);
	return -ENODEV;
}

static void __exit hack_undo(void) {
	if (orig_handle) {
		pr_info("Restoring original ACPI handle for discrete"
			" video card\n");
		dev_set_acpi_handle(dis_dev, orig_handle);
	}
}

module_init(hack_apply);
module_exit(hack_undo);

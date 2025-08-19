/*
 * MT7927 Test: PCI Enumeration
 * Category: 01_basic (Safe)
 * 
 * Purpose: Verify PCI device enumeration and basic identification
 * Expected: Device responds with ID 0x792714c3
 * Safe: Read-only PCI config space access
 */

#include <linux/module.h>
#include <linux/pci.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    u16 vendor, device;
    u32 class_rev;
    u8 revision;
    int ret;
    
    printk(KERN_INFO "\n=== MT7927 TEST: PCI Enumeration ===\n");
    printk(KERN_INFO "Test Category: 01_basic (Safe)\n\n");
    
    // Enable device
    ret = pci_enable_device(pdev);
    if (ret) {
        printk(KERN_ERR "FAIL: Cannot enable PCI device (error %d)\n", ret);
        return ret;
    }
    
    // Read PCI IDs
    pci_read_config_word(pdev, PCI_VENDOR_ID, &vendor);
    pci_read_config_word(pdev, PCI_DEVICE_ID, &device);
    pci_read_config_dword(pdev, PCI_CLASS_REVISION, &class_rev);
    revision = class_rev & 0xff;
    
    // Verify device identity
    printk(KERN_INFO "Test Results:\n");
    printk(KERN_INFO "  Vendor ID: 0x%04x %s\n", vendor,
           vendor == MT7927_VENDOR_ID ? "✓ PASS" : "✗ FAIL");
    printk(KERN_INFO "  Device ID: 0x%04x %s\n", device,
           device == MT7927_DEVICE_ID ? "✓ PASS" : "✗ FAIL");
    printk(KERN_INFO "  Combined: 0x%04x%04x\n", device, vendor);
    printk(KERN_INFO "  Revision: 0x%02x\n", revision);
    printk(KERN_INFO "  Class: 0x%06x (Network controller)\n", class_rev >> 8);
    
    // Check BARs
    printk(KERN_INFO "\nBAR Configuration:\n");
    for (int i = 0; i < 6; i++) {
        u32 start = pci_resource_start(pdev, i);
        u32 len = pci_resource_len(pdev, i);
        u32 flags = pci_resource_flags(pdev, i);
        
        if (len > 0) {
            printk(KERN_INFO "  BAR%d: 0x%08x, size: %d%s, %s\n",
                   i, start, 
                   len > 1024*1024 ? len/(1024*1024) : len/1024,
                   len > 1024*1024 ? "MB" : "KB",
                   flags & IORESOURCE_MEM ? "Memory" : "I/O");
        }
    }
    
    // Overall test result
    if (vendor == MT7927_VENDOR_ID && device == MT7927_DEVICE_ID) {
        printk(KERN_INFO "\n✓ TEST PASSED: MT7927 detected and enumerated correctly\n");
    } else {
        printk(KERN_ERR "\n✗ TEST FAILED: Device mismatch\n");
    }
    
    pci_disable_device(pdev);
    return -ENODEV; // Always return error to prevent driver binding
}

static void test_remove(struct pci_dev *pdev) {}

static struct pci_device_id test_ids[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { 0, }
};

static struct pci_driver test_driver = {
    .name = "test_pci_enumerate",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Test: PCI Enumeration");
MODULE_AUTHOR("MT7927 Linux Driver Project");

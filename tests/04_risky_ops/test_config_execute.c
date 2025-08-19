/*
 * Test: Configuration Command Executor
 * Category: 04_risky_ops
 * Purpose: Execute the 79 configuration commands to activate memory
 * Strategy: Use discovered mappings to execute commands phase by phase
 * Risk: High - May trigger chip errors or achieve breakthrough
 * Duration: ~10 seconds
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#define MT7927_VENDOR_ID 0x14c3
#define MT7927_DEVICE_ID 0x7927
#define CONFIG_OFFSET    0x080000

/* Command structure from our analysis */
struct config_command {
    u32 raw;
    u8 prefix;     /* 0x16 */
    u8 cmd_type;   /* Command type */
    u8 reg_addr;   /* Target register */
    u8 value;      /* Value/parameter */
};

/* Register mapping hypotheses from test_config_mapper results */
static u32 get_bar2_offset(u8 config_reg)
{
    /* Try alternative mapping strategy based on test results */
    static int mapping_strategy = 0;
    
    /* Cycle through different strategies on each run */
    mapping_strategy = (mapping_strategy + 1) % 3;
    
    switch (mapping_strategy) {
        case 0:  /* Original hypothesis */
            switch (config_reg) {
                case 0x20: return 0x0020;
                case 0x24: return 0x0024;
                case 0x70: return 0x0070;
                case 0x74: return 0x0074;
                case 0x00: return 0x0000;
                case 0x01: return 0x0004;
                case 0x81: return 0x0204;  /* DMA_ENABLE area */
                case 0x13: return 0x004c;
                case 0x30: return 0x00c0;
                case 0x60: return 0x0180;
                default:
                    if (config_reg < 0x80)
                        return config_reg;
                    else
                        return 0x0200 + (config_reg & 0x7F);
            }
            break;
            
        case 1:  /* Direct 1:1 mapping */
            printk(KERN_INFO "Using direct 1:1 mapping strategy\n");
            return config_reg;
            
        case 2:  /* Shifted mapping (x4 for word alignment) */
            printk(KERN_INFO "Using shifted x4 mapping strategy\n");
            return config_reg * 4;
    }
    
    return config_reg;  /* Fallback */
}

/* Execute a single configuration command */
static int execute_command(void __iomem *bar2, struct config_command *cmd, int dry_run)
{
    u32 bar2_offset;
    u32 original, new_val;
    
    /* Get BAR2 offset for this register */
    bar2_offset = get_bar2_offset(cmd->reg_addr);
    
    /* Skip danger zones */
    if (bar2_offset == 0x00a4 || bar2_offset == 0x00b8 ||
        bar2_offset == 0x00cc || bar2_offset == 0x00dc) {
        printk(KERN_WARNING "    Skipping danger zone BAR2[0x%04x]\n", bar2_offset);
        return -1;
    }
    
    /* Read current value */
    original = ioread32(bar2 + bar2_offset);
    
    /* Interpret command type (based on our analysis) */
    switch (cmd->cmd_type) {
        case 0x00:  /* Basic write */
            new_val = cmd->value;
            break;
        case 0x01:  /* OR operation */
            new_val = original | cmd->value;
            break;
        case 0x10:  /* AND operation */
            new_val = original & cmd->value;
            break;
        case 0x11:  /* XOR operation */
            new_val = original ^ cmd->value;
            break;
        case 0x20:  /* Set bit(s) */
            new_val = original | (1 << (cmd->value & 0x1F));
            break;
        case 0x21:  /* Clear bit(s) */
            new_val = original & ~(1 << (cmd->value & 0x1F));
            break;
        default:
            printk(KERN_INFO "    Unknown command type 0x%02x\n", cmd->cmd_type);
            return -1;
    }
    
    if (dry_run) {
        printk(KERN_INFO "    [DRY] Would write 0x%08x to BAR2[0x%04x] (was 0x%08x)\n",
               new_val, bar2_offset, original);
    } else {
        printk(KERN_INFO "    Writing 0x%08x to BAR2[0x%04x] (was 0x%08x)\n",
               new_val, bar2_offset, original);
        iowrite32(new_val, bar2 + bar2_offset);
        wmb();
    }
    
    return 0;
}

/* Check if memory has been activated */
static int check_memory_activation(void __iomem *bar0, void __iomem *bar2)
{
    u32 main_mem, dma_mem, fw_status;
    int activated = 0;
    
    main_mem = ioread32(bar0);
    dma_mem = ioread32(bar0 + 0x020000);
    fw_status = ioread32(bar2 + 0x0200);
    
    if (main_mem != 0x00000000 && main_mem != 0xffffffff) {
        printk(KERN_INFO "\n✓✓✓ BREAKTHROUGH: Main memory ACTIVATED! Value: 0x%08x\n", main_mem);
        activated = 1;
    }
    
    if (dma_mem != 0x00000000 && dma_mem != 0xffffffff) {
        printk(KERN_INFO "✓ DMA memory activated! Value: 0x%08x\n", dma_mem);
        activated = 1;
    }
    
    if (fw_status != 0xffff10f1) {
        printk(KERN_INFO "✓ FW_STATUS changed! New value: 0x%08x\n", fw_status);
    }
    
    return activated;
}

static int test_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    void __iomem *bar0 = NULL, *bar2 = NULL;
    struct config_command cmd;
    u32 val;
    int ret, i;
    int phase = 0, cmd_count = 0;
    int memory_activated = 0;
    int dry_run = 1;  /* Start with dry run for safety */
    
    printk(KERN_INFO "\n=== MT7927 TEST: Configuration Command Executor ===\n");
    printk(KERN_INFO "Category: 04_risky_ops\n");
    printk(KERN_INFO "Risk: High - Executing initialization sequence\n");
    printk(KERN_INFO "Goal: Activate memory at BAR0[0x000000]\n\n");
    
    ret = pci_enable_device(pdev);
    if (ret) {
        printk(KERN_ERR "FAIL: Cannot enable device\n");
        return ret;
    }
    
    pci_set_master(pdev);
    
    ret = pci_request_regions(pdev, "test_config_execute");
    if (ret) {
        printk(KERN_ERR "FAIL: Cannot request regions\n");
        goto out_disable;
    }
    
    bar0 = pci_iomap(pdev, 0, 0);
    if (!bar0) {
        printk(KERN_ERR "FAIL: Cannot map BAR0\n");
        goto out_release;
    }
    
    bar2 = pci_iomap(pdev, 2, 0);
    if (!bar2) {
        printk(KERN_ERR "FAIL: Cannot map BAR2\n");
        goto out_unmap0;
    }
    
    /* Check initial state */
    val = ioread32(bar2);
    if (val == 0xffffffff) {
        printk(KERN_ERR "Chip in error state! Aborting.\n");
        goto out_unmap;
    }
    
    printk(KERN_INFO "Initial state:\n");
    printk(KERN_INFO "  Chip status: 0x%08x\n", val);
    printk(KERN_INFO "  BAR0[0x000000]: 0x%08x\n", ioread32(bar0));
    printk(KERN_INFO "  BAR0[0x020000]: 0x%08x\n", ioread32(bar0 + 0x020000));
    printk(KERN_INFO "  FW_STATUS: 0x%08x\n", ioread32(bar2 + 0x0200));
    printk(KERN_INFO "  DMA_ENABLE: 0x%08x\n\n", ioread32(bar2 + 0x0204));
    
    /* First pass: Dry run to see what would happen */
    printk(KERN_INFO "=== PHASE 1: Dry Run (no writes) ===\n");
    
    for (i = 0; i < 0x400 && cmd_count < 20; i += 4) {
        val = ioread32(bar0 + CONFIG_OFFSET + i);
        
        if ((val & 0xFF000000) == 0x16000000) {
            cmd.raw = val;
            cmd.prefix = (val >> 24) & 0xFF;
            cmd.cmd_type = (val >> 16) & 0xFF;
            cmd.reg_addr = (val >> 8) & 0xFF;
            cmd.value = val & 0xFF;
            
            printk(KERN_INFO "  Cmd %d: 0x%08x -> Type:0x%02x Reg:0x%02x Val:0x%02x\n",
                   cmd_count, val, cmd.cmd_type, cmd.reg_addr, cmd.value);
            
            execute_command(bar2, &cmd, 1);  /* Dry run */
            cmd_count++;
        } else if (val == 0x31000100) {
            phase++;
            printk(KERN_INFO "  --- Phase %d delimiter ---\n", phase);
        }
    }
    
    /* Ask user to proceed */
    printk(KERN_INFO "\n=== PHASE 2: Actual Execution ===\n");
    printk(KERN_INFO "⚠️  WARNING: Now executing commands for real!\n");
    printk(KERN_INFO "Focusing on register 0x81 commands first...\n\n");
    
    /* Execute commands targeting register 0x81 first */
    cmd_count = 0;
    phase = 0;
    
    for (i = 0; i < 0x400 && !memory_activated; i += 4) {
        val = ioread32(bar0 + CONFIG_OFFSET + i);
        
        if ((val & 0xFF000000) == 0x16000000) {
            cmd.raw = val;
            cmd.prefix = (val >> 24) & 0xFF;
            cmd.cmd_type = (val >> 16) & 0xFF;
            cmd.reg_addr = (val >> 8) & 0xFF;
            cmd.value = val & 0xFF;
            
            /* Focus on register 0x81 commands */
            if (cmd.reg_addr == 0x81) {
                printk(KERN_INFO "Phase %d, Cmd %d: REG 0x81 command 0x%08x\n",
                       phase, cmd_count, val);
                
                execute_command(bar2, &cmd, 0);  /* Real execution */
                msleep(10);  /* Give chip time to respond */
                
                /* Check for activation after each 0x81 command */
                if (check_memory_activation(bar0, bar2)) {
                    memory_activated = 1;
                    printk(KERN_INFO "\n🎉 SUCCESS after register 0x81 command!\n");
                    break;
                }
            }
            cmd_count++;
        } else if (val == 0x31000100) {
            phase++;
            printk(KERN_INFO "  Entering phase %d\n", phase);
        }
    }
    
    /* If not activated yet, try executing first phase completely */
    if (!memory_activated) {
        printk(KERN_INFO "\n=== PHASE 3: Full First Phase Execution ===\n");
        
        cmd_count = 0;
        for (i = 0; i < 0x100 && !memory_activated; i += 4) {
            val = ioread32(bar0 + CONFIG_OFFSET + i);
            
            if ((val & 0xFF000000) == 0x16000000) {
                cmd.raw = val;
                cmd.prefix = (val >> 24) & 0xFF;
                cmd.cmd_type = (val >> 16) & 0xFF;
                cmd.reg_addr = (val >> 8) & 0xFF;
                cmd.value = val & 0xFF;
                
                printk(KERN_INFO "  Executing: 0x%08x\n", val);
                execute_command(bar2, &cmd, 0);
                cmd_count++;
                
                /* Check every 5 commands */
                if (cmd_count % 5 == 0) {
                    msleep(10);
                    if (check_memory_activation(bar0, bar2)) {
                        memory_activated = 1;
                        printk(KERN_INFO "\n🎉 SUCCESS after %d commands!\n", cmd_count);
                        break;
                    }
                }
            } else if (val == 0x31000100) {
                printk(KERN_INFO "  First phase complete\n");
                break;
            }
        }
    }
    
    /* Final state check */
    printk(KERN_INFO "\n=== Final State ===\n");
    val = ioread32(bar2);
    if (val == 0xffffffff) {
        printk(KERN_ERR "⚠️  Chip entered error state\n");
    } else {
        printk(KERN_INFO "  Chip status: 0x%08x ✓\n", val);
    }
    
    printk(KERN_INFO "  BAR0[0x000000]: 0x%08x %s\n", 
           ioread32(bar0), 
           ioread32(bar0) != 0 ? "✓ ACTIVE!" : "");
    printk(KERN_INFO "  BAR0[0x020000]: 0x%08x %s\n", 
           ioread32(bar0 + 0x020000),
           ioread32(bar0 + 0x020000) != 0 ? "✓ ACTIVE!" : "");
    printk(KERN_INFO "  FW_STATUS: 0x%08x %s\n", 
           ioread32(bar2 + 0x0200),
           ioread32(bar2 + 0x0200) != 0xffff10f1 ? "✓ CHANGED!" : "");
    
    if (memory_activated) {
        printk(KERN_INFO "\n✅✅✅ TEST PASSED: MEMORY ACTIVATION ACHIEVED! ✅✅✅\n");
        printk(KERN_INFO "This is a MAJOR BREAKTHROUGH!\n");
        printk(KERN_INFO "Next: Document exact sequence and continue driver development\n");
    } else {
        printk(KERN_INFO "\n❌ Memory not activated yet\n");
        printk(KERN_INFO "But we've learned more about the process\n");
        printk(KERN_INFO "Try alternative register mappings next\n");
    }
    
out_unmap:
    pci_iounmap(pdev, bar2);
out_unmap0:
    pci_iounmap(pdev, bar0);
out_release:
    pci_release_regions(pdev);
out_disable:
    pci_disable_device(pdev);
    
    return -ENODEV;
}

static void test_remove(struct pci_dev *pdev) {}

static struct pci_device_id test_ids[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { 0, }
};

static struct pci_driver test_driver = {
    .name = "test_config_execute",
    .id_table = test_ids,
    .probe = test_probe,
    .remove = test_remove,
};

module_pci_driver(test_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 Test: Configuration Command Executor");
MODULE_AUTHOR("MT7927 Linux Driver Project");

# MT7927 WiFi 7 Linux Driver Project
# Main Makefile

# Kernel build directory
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Default target
all: driver tests tools

# Driver module (when ready)
driver:
	@echo "Driver not yet implemented - in development"
	@# $(MAKE) -C $(KDIR) M=$(PWD)/src modules

# Test modules
tests:
	$(MAKE) -C $(KDIR) M=$(PWD)/tests modules

# Tool modules
tools:
	$(MAKE) -C $(KDIR) M=$(PWD)/tests modules

# Clean everything
clean:
	$(MAKE) -C $(KDIR) M=$(PWD)/tests clean
	@# $(MAKE) -C $(KDIR) M=$(PWD)/src clean
	find . -name "*.log" -type f -delete
	find . -name "*.o.cmd" -type f -delete
	find . -name ".*.cmd" -type f -delete
	find . -name "*.mod" -type f -delete
	find . -name "*.mod.c" -type f -delete
	find . -name "*.ko.cmd" -type f -delete
	find . -name "modules.order" -type f -delete
	find . -name "Module.symvers" -type f -delete

# Install driver (when ready)
install:
	@echo "Driver installation not yet available"
	@# sudo cp src/mt7927.ko /lib/modules/$(shell uname -r)/kernel/drivers/net/wireless/
	@# sudo depmod -a

# Check chip state
check:
	@echo "Checking MT7927 chip state..."
	@chip_id=$$(sudo setpci -s 0a:00.0 00.l 2>/dev/null || echo "none"); \
	if [ "$$chip_id" = "792714c3" ]; then \
		echo "✓ Chip responding normally (ID: $$chip_id)"; \
	elif [ "$$chip_id" = "ffffffff" ]; then \
		echo "✗ Chip in error state - recovery needed"; \
		echo "Run: make recover"; \
		exit 1; \
	else \
		echo "✗ Chip not found or not responding"; \
		exit 1; \
	fi

# Recover chip from error state
recover:
	@echo "Attempting chip recovery via PCI rescan..."
	@echo 1 | sudo tee /sys/bus/pci/devices/0000:0a:00.0/remove > /dev/null
	@sleep 2
	@echo 1 | sudo tee /sys/bus/pci/rescan > /dev/null
	@sleep 2
	@$(MAKE) check

# Run safe baseline tests
test-safe:
	@echo "Running safe baseline tests..."
	@$(MAKE) tests
	@cd tests/01_safe_basic && ./run_tests.sh

# Run discovery tests
test-discovery:
	@echo "Running discovery tests..."
	@$(MAKE) tests
	@cd tests && ./run_analysis.sh

# Development helpers
help:
	@echo "MT7927 WiFi 7 Linux Driver - Development Makefile"
	@echo "================================================="
	@echo ""
	@echo "Main targets:"
	@echo "  make all          - Build everything"
	@echo "  make driver       - Build driver module (when ready)"
	@echo "  make tests        - Build all test modules"
	@echo "  make tools        - Build exploration tools"
	@echo "  make clean        - Clean all build artifacts"
	@echo ""
	@echo "Testing targets:"
	@echo "  make check        - Check chip state"
	@echo "  make recover      - Recover chip from error state"
	@echo "  make test-safe    - Run safe baseline tests"
	@echo "  make test-discovery - Run discovery tests"
	@echo ""
	@echo "Development:"
	@echo "  make install      - Install driver (when ready)"
	@echo "  make help         - Show this help"
	@echo ""
	@echo "Current chip slot: 0a:00.0"
	@echo "PCI ID: 14c3:7927 (MediaTek MT7927)"

# Create initial directory structure
setup:
	@echo "Setting up project structure..."
	@mkdir -p src docs tests/{01_safe_basic,02_safe_discovery,03_careful_write,04_risky_ops,05_danger_zone,tools} logs
	@echo "✓ Directory structure created"

.PHONY: all driver tests tools clean install check recover test-safe test-discovery help setup

/*
 * Copyright (c) 2009 Corey Tabaka
 * Copyright (c) 2015 Intel Corporation
 * Copyright (c) 2016 Travis Geiselbrecht
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

#include <lk/err.h>
#include <lk/init.h>
#include <lk/trace.h>
#include <arch/x86/mmu.h>
#include <platform.h>
#include "platform_p.h"
#include <platform/pc.h>
#include <platform/multiboot.h>
#include <platform/console.h>
#include <platform/keyboard.h>
#include <dev/uart.h>
#include <arch/x86.h>
#include <arch/mmu.h>
#include <malloc.h>
#include <string.h>
#include <assert.h>
#include <kernel/vm.h>
#include <lib/acpi_lite.h>

#if WITH_DEV_BUS_PCI
#include <dev/bus/pci.h>
#endif
#if WITH_LIB_MINIP
#include <lib/minip.h>
#endif

#define LOCAL_TRACE 0

/* multiboot information passed in, if present */
extern multiboot_info_t *_multiboot_info;

#define DEFAULT_MEMEND (16*1024*1024)

static paddr_t mem_top = DEFAULT_MEMEND;
extern uint64_t __code_start;
extern uint64_t __code_end;
extern uint64_t __rodata_start;
extern uint64_t __rodata_end;
extern uint64_t __data_start;
extern uint64_t __data_end;
extern uint64_t __bss_start;
extern uint64_t __bss_end;

void platform_init_mmu_mappings(void) {
    // XXX move into arch/x86 setup
#if 0
    struct map_range range;
    arch_flags_t access;
    map_addr_t *init_table, phy_init_table;

    LTRACE_ENTRY;

    /* Creating the First page in the page table hirerachy */
    /* Can be pml4, pdpt or pdt based on x86_64, x86 PAE mode & x86 non-PAE mode respectively */
    init_table = memalign(PAGE_SIZE, PAGE_SIZE);
    ASSERT(init_table);
    memset(init_table, 0, PAGE_SIZE);

    phy_init_table = (map_addr_t)X86_VIRT_TO_PHYS(init_table);
    LTRACEF("phy_init_table: %p\n", phy_init_table);

    /* kernel code section mapping */
    LTRACEF("mapping kernel code\n");
    access = ARCH_MMU_FLAG_PERM_RO;
    range.start_vaddr = range.start_paddr = (map_addr_t) &__code_start;
    range.size = ((map_addr_t)&__code_end) - ((map_addr_t)&__code_start);
    x86_mmu_map_range(phy_init_table, &range, access);

    /* kernel data section mapping */
    LTRACEF("mapping kernel data\n");
    access = 0;
#if defined(ARCH_X86_64) || defined(PAE_MODE_ENABLED)
    access |= ARCH_MMU_FLAG_PERM_NO_EXECUTE;
#endif
    range.start_vaddr = range.start_paddr = (map_addr_t) &__data_start;
    range.size = ((map_addr_t)&__data_end) - ((map_addr_t)&__data_start);
    x86_mmu_map_range(phy_init_table, &range, access);

    /* kernel rodata section mapping */
    LTRACEF("mapping kernel rodata\n");
    access = ARCH_MMU_FLAG_PERM_RO;
#if defined(ARCH_X86_64) || defined(PAE_MODE_ENABLED)
    access |= ARCH_MMU_FLAG_PERM_NO_EXECUTE;
#endif
    range.start_vaddr = range.start_paddr = (map_addr_t) &__rodata_start;
    range.size = ((map_addr_t)&__rodata_end) - ((map_addr_t)&__rodata_start);
    x86_mmu_map_range(phy_init_table, &range, access);

    /* kernel bss section and kernel heap mappings */
    LTRACEF("mapping kernel bss+heap\n");
    access = 0;
#ifdef ARCH_X86_64
    access |= ARCH_MMU_FLAG_PERM_NO_EXECUTE;
#endif
    range.start_vaddr = range.start_paddr = (map_addr_t) &__bss_start;
    range.size = ((map_addr_t)_heap_end) - ((map_addr_t)&__bss_start);
    x86_mmu_map_range(phy_init_table, &range, access);

    /* Mapping for BIOS, devices */
    LTRACEF("mapping bios devices\n");
    access = 0;
    range.start_vaddr = range.start_paddr = (map_addr_t) 0;
    range.size = ((map_addr_t)&__code_start);
    x86_mmu_map_range(phy_init_table, &range, access);

    /* Moving to the new CR3 */
    g_CR3 = (map_addr_t)phy_init_table;
    x86_set_cr3((map_addr_t)phy_init_table);

    LTRACE_EXIT;
#endif
}

#if WITH_KERNEL_VM
struct mmu_initial_mapping mmu_initial_mappings[] = {
#if ARCH_X86_64
    /* 64GB of memory mapped where the kernel lives */
    {
        .phys = MEMBASE,
        .virt = KERNEL_ASPACE_BASE,
        .size = 64ULL*GB, /* x86-64 maps first 64GB by default */
        .flags = 0,
        .name = "memory"
    },
#endif
    /* 1GB of memory mapped where the kernel lives */
    {
        .phys = MEMBASE,
        .virt = KERNEL_BASE,
        .size = 1*GB, /* x86 maps first 1GB by default */
        .flags = 0,
        .name = "kernel"
    },

    /* null entry to terminate the list */
    { 0 }
};

static pmm_arena_t mem_arena = {
    .name = "memory",
    .base = MEMBASE,
    .size = DEFAULT_MEMEND, /* default amount of memory in case we don't have multiboot */
    .priority = 1,
    .flags = PMM_ARENA_FLAG_KMAP
};

/* set up the size of the physical memory map based on the end of memory we detected in
 * platform_init_multiboot_info()
 */
static void mem_arena_init(void) {
    uintptr_t mem_base = (uintptr_t)MEMBASE;
    uintptr_t mem_size = mem_top;

    mem_arena.base = PAGE_ALIGN(mem_base) + MB;
    mem_arena.size = PAGE_ALIGN(mem_size) - MB;
}
#endif

static void platform_init_multiboot_info(void) {
    LTRACEF("_multiboot_info %p\n", _multiboot_info);
    if (_multiboot_info) {
        /* bump the multiboot pointer up to the kernel mapping */
        _multiboot_info = (void *)((uintptr_t)_multiboot_info + KERNEL_BASE);

        if (_multiboot_info->flags & MB_INFO_MEM_SIZE) {
            LTRACEF("memory lower 0x%x\n", _multiboot_info->mem_lower * 1024U);
            LTRACEF("memory upper 0x%llx\n", _multiboot_info->mem_upper * 1024ULL);
            mem_top = _multiboot_info->mem_upper * 1024;
        }

        if (_multiboot_info->flags & MB_INFO_MMAP) {
            memory_map_t *mmap = (memory_map_t *)(uintptr_t)_multiboot_info->mmap_addr;
            mmap = (void *)((uintptr_t)mmap + KERNEL_BASE);

            LTRACEF("memory map:\n");
            for (uint i = 0; i < _multiboot_info->mmap_length / sizeof(memory_map_t); i++) {

                LTRACEF("\ttype %u addr 0x%x %x len 0x%x %x\n",
                        mmap[i].type, mmap[i].base_addr_high, mmap[i].base_addr_low,
                        mmap[i].length_high, mmap[i].length_low);
                if (mmap[i].type == MB_MMAP_TYPE_AVAILABLE && mmap[i].base_addr_low >= mem_top) {
                    mem_top = mmap[i].base_addr_low + mmap[i].length_low;
                } else if (mmap[i].type != MB_MMAP_TYPE_AVAILABLE && mmap[i].base_addr_low >= mem_top) {
                    /*
                     * break on first memory hole above default heap end for now.
                     * later we can add facilities for adding free chunks to the
                     * heap for each segregated memory region.
                     */
                    break;
                }
            }
        }
    }

#if ARCH_X86_32
    if (mem_top > 1*GB) {
        /* trim the memory map to 1GB, since that's what's already mapped in the kernel */
        TRACEF("WARNING: trimming memory to first 1GB\n");
        mem_top = 1*GB;
    }
#endif
    LTRACEF("mem_top 0x%lx\n", mem_top);
}

void platform_early_init(void) {
    /* get the debug output working */
    platform_init_debug_early();

    /* get the text console working */
    platform_init_console();

    /* initialize the interrupt controller */
    platform_init_interrupts();

    /* initialize the timer */
    platform_init_timer();

    /* look at multiboot to determine our memory size */
    platform_init_multiboot_info();

#ifdef WITH_KERNEL_VM
    mem_arena_init();
    pmm_add_arena(&mem_arena);
#endif
}

void local_apic_callback(const void *_entry, size_t entry_len) {
    const struct acpi_madt_local_apic_entry *entry = _entry;

    printf("\tLOCAL APIC id %d, processor id %d, flags %#x\n",
            entry->apic_id, entry->processor_id, entry->flags);
}

void io_apic_callback(const void *_entry, size_t entry_len) {
    const struct acpi_madt_io_apic_entry *entry = _entry;

    printf("\tIO APIC id %d, address %#x gsi base %u\n",
            entry->io_apic_id, entry->io_apic_address, entry->global_system_interrupt_base);
}

void int_source_override_callback(const void *_entry, size_t entry_len) {
    const struct acpi_madt_int_source_override_entry *entry = _entry;

    printf("\tINT OVERRIDE bus %u, source %u, gsi %u, flags %#x\n",
            entry->bus, entry->source, entry->global_sys_interrupt, entry->flags);
}

void platform_init(void) {
    platform_init_debug();

    platform_init_keyboard(&console_input_buf);

#if WITH_DEV_BUS_PCI
    bool pci_initted = false;
    if (acpi_lite_init(0) == NO_ERROR) {
        if (LOCAL_TRACE) {
            acpi_lite_dump_tables();
        }

        // dump the APIC table
        printf("MADT/APIC table:\n");
        acpi_process_madt_entries_etc(ACPI_MADT_TYPE_LOCAL_APIC, &local_apic_callback);
        acpi_process_madt_entries_etc(ACPI_MADT_TYPE_IO_APIC, &io_apic_callback);
        acpi_process_madt_entries_etc(ACPI_MADT_TYPE_INT_SOURCE_OVERRIDE, &int_source_override_callback);

        // try to find the mcfg table
        const struct acpi_mcfg_table *table = (const struct acpi_mcfg_table *)acpi_get_table_by_sig(ACPI_MCFG_SIG);
        if (table) {
            if (table->header.length >= sizeof(*table) + sizeof(struct acpi_mcfg_entry)) {
                const struct acpi_mcfg_entry *entry = (const void *)(table + 1);
                printf("PCI MCFG: segment %#hx bus [%hhu...%hhu] address %#llx\n",
                        entry->segment, entry->start_bus, entry->end_bus, entry->base_address);

                // try to initialize pci based on the MCFG ecam aperture
                status_t err = pci_init_ecam(entry->base_address, entry->segment, entry->start_bus, entry->end_bus);
                if (err == NO_ERROR) {
                    pci_bus_mgr_init();
                    pci_initted = true;
                }
            }
        }
    }

    // fall back to legacy pci if we couldn't find the pcie aperture
    if (!pci_initted) {
        status_t err = pci_init_legacy();
        if (err == NO_ERROR) {
            pci_bus_mgr_init();
        }
    }
#endif

    platform_init_mmu_mappings();
}

#if WITH_LIB_MINIP
void _start_minip(uint level) {
    extern status_t e1000_register_with_minip(void);
    status_t err = e1000_register_with_minip();
    if (err == NO_ERROR) {
        minip_start_dhcp();
    }
}

LK_INIT_HOOK(start_minip, _start_minip, LK_INIT_LEVEL_APPS - 1);
#endif

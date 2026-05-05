#include "video.h"
#include "ahci.h"
#include "string_utils.h"
#include <stdint.h>

uint16_t ata_base_port = 0;
char detected_model[41] = "NO DRIVE";
uint64_t total_sectors = 0;
uint64_t free_sectors = 0;

volatile uint32_t* ahci_base = 0;
int ahci_port_count = 0;
int ahci_port_mask = 0;
int current_port = -1;

#define MAX_DISKS 16
#define MAX_PARTITIONS 16

typedef struct {
    int port;
    uint64_t sectors;
    uint32_t size_mb;
    char model[41];
    char serial[21];
    uint8_t is_atapi;
} DiskInfo;

typedef struct {
    int disk_index;
    uint32_t lba_start;
    uint32_t sector_count;
    uint8_t type;
    char fs_type[16];
    int bootable;
} PartitionInfo;

static DiskInfo disks[MAX_DISKS];
static int disk_count = 0;
static PartitionInfo partitions[MAX_PARTITIONS];
static int partition_count = 0;

static uint8_t cmd_list_buf[1024] __attribute__((aligned(1024)));
static uint8_t cmd_table_buf[8192] __attribute__((aligned(256)));

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved;
    uint32_t dbc;
} __attribute__((packed)) prdt_t;

typedef struct {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    prdt_t prdt[1];
} __attribute__((packed)) cmd_table_t;

typedef struct {
    uint16_t prdtl;
    uint16_t prdbc;
    uint32_t paddr;
} __attribute__((packed)) cmd_header_t;

#define HBA_PxCMD_ST    0x0001
#define HBA_PxCMD_FRE   0x0010
#define HBA_PxCMD_FR    0x4000
#define HBA_PxCMD_CR    0x8000

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(0xCF8, address);
    return inl(0xCFC);
}

static void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t data) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(0xCF8, address);
    outl(0xCFC, data);
}

void find_all_controllers(void) {
    kprint("\n=== SCANNING ALL STORAGE CONTROLLERS ===\n");
    
    for(int bus = 0; bus < 256; bus++) {
        for(int slot = 0; slot < 32; slot++) {
            for(int func = 0; func < 8; func++) {
                uint32_t vendor = pci_read(bus, slot, func, 0);
                if(vendor == 0xFFFFFFFF) continue;
            
                uint32_t class_reg = pci_read(bus, slot, func, 0x08);
                uint8_t dev_class = (class_reg >> 24) & 0xFF;
                uint8_t subclass = (class_reg >> 16) & 0xFF;
                uint16_t vendor_id = vendor & 0xFFFF;
                uint16_t device_id = vendor >> 16;
                
                kprint("PCI ");
                kprint_int(bus); kprint(":");
                kprint_int(slot); kprint(".");
                kprint_int(func); kprint(" | ");
                kprint_hex16(vendor_id); kprint(":");
                kprint_hex16(device_id);
                
                if(dev_class == 0x01) {
                    kprint("STORAGE");
                    if(subclass == 0x01) kprint(" (IDE)");
                    if(subclass == 0x06) {
                        kprint(" (SATA/AHCI)");
                        
                        kprint("\n");
                        kprint("  Vendor: ");
                        if(vendor_id == 0x8086) kprint("Intel");
                        else if(vendor_id == 0x1002) kprint("AMD");
                        else if(vendor_id == 0x10DE) kprint("NVIDIA");
                        else if(vendor_id == 0x197B) kprint("JMicron");
                        else if(vendor_id == 0x1B4B) kprint("Marvell");
                        else kprint("Unknown");
                        kprint("\n");
                        
                        uint32_t bars[6];
                        for(int b = 0; b < 6; b++) {
                            bars[b] = pci_read(bus, slot, func, 0x10 + b*4);
                            kprint("    BAR"); kprint_int(b); kprint(": 0x");
                            kprint_hex32(bars[b]);
                            if(bars[b] & 1) kprint(" (I/O)");
                            else {
                                kprint(" (MEM)");
                                if((bars[b] & 0xF) == 0x0) kprint(" 32-bit");
                                else if((bars[b] & 0xF) == 0x4) kprint(" 64-bit");
                            }
                            kprint("\n");
                        }
                        
                        uint32_t cmd = pci_read(bus, slot, func, 0x04);
                        kprint("  Command before: 0x"); kprint_hex16(cmd & 0xFFFF); kprint("\n");
                        cmd |= 0x06;
                        pci_write(bus, slot, func, 0x04, cmd);
                        
                        uint32_t cmd_after = pci_read(bus, slot, func, 0x04);
                        kprint("  Command after: 0x"); kprint_hex16(cmd_after & 0xFFFF); kprint("\n");
                        
                        if(bars[5] && !(bars[5] & 1)) {
                            uint32_t bar5 = bars[5] & ~0xF;
                            ahci_base = (uint32_t*)(uintptr_t)bar5;
                            kprint("  Using BAR5: 0x"); kprint_hex32(bar5); kprint("\n");
                            
                            volatile uint32_t test = ahci_base[0];
                            if(test != 0xFFFFFFFF && test != 0) {
                                kprint("  CAP register: 0x"); kprint_hex32(test); kprint("\n");
                                
                                uint32_t cap = ahci_base[0];
                                ahci_port_count = (cap & 0x1F) + 1;
                                ahci_port_mask = ahci_base[3];
                                
                                kprint("    Ports implemented: "); kprint_int(ahci_port_count); kprint("\n");
                                kprint("    Port mask: 0x"); kprint_hex32(ahci_port_mask); kprint("\n");
                                
                                ahci_base[1] |= 1;
                                
                                int timeout = 1000000;
                                while(timeout--) {
                                    if(ahci_base[1] & 1) break;
                                }
                            } else {
                                kprint("  BAR5 reads as 0xFFFFFFFF - invalid!\n");
                                ahci_base = 0;
                            }
                        }
                        
                        if(!ahci_base && bars[0] && !(bars[0] & 1)) {
                            uint32_t bar0 = bars[0] & ~0xF;
                            ahci_base = (uint32_t*)(uintptr_t)bar0;
                            kprint("  Trying BAR0: 0x"); kprint_hex32(bar0); kprint("\n");
                            
                            volatile uint32_t test = ahci_base[0];
                            if(test != 0xFFFFFFFF) {
                                uint32_t cap = ahci_base[0];
                                ahci_port_count = (cap & 0x1F) + 1;
                                ahci_port_mask = ahci_base[3];
                                
                                kprint("    Ports: "); kprint_int(ahci_port_count); 
                                kprint(" (mask 0x"); kprint_hex32(ahci_port_mask); kprint(")\n");
                                
                                ahci_base[1] |= 1;
                            }
                        }
                    }
                    if(subclass == 0x08) kprint(" (NVMe)");
                }
                kprint("\n");
            }
        }
    }
}

static int port_ready(int port) {
    volatile uint32_t* port_base = ahci_base + (0x80 + port * 0x20)/4;
    int timeout = 10000000;
    
    while(timeout--) {
        uint32_t tfd = port_base[8];
        if(!(tfd & 0x80)) return 1;  
        if(!(tfd & 0x08)) return 1;  
    }
    return 0;
}

static int reset_port(int port) {
    volatile uint32_t* port_base = ahci_base + (0x80 + port * 0x20)/4;
    
    kprint("  Resetting port "); kprint_int(port); kprint("...\n");
    
    port_base[6] &= ~HBA_PxCMD_ST;
    port_base[6] &= ~HBA_PxCMD_FRE;
    
    int timeout = 10000000;
    while(timeout--) {
        if(!(port_base[6] & HBA_PxCMD_CR)) break;
        if(!(port_base[6] & HBA_PxCMD_FR)) break;
    }
    
    port_base[11] = 0x301;
    for(volatile int i = 0; i < 100000; i++);
    port_base[11] = 0;
    
    timeout = 10000000;
    int device_found = 0;
    while(timeout--) {
        uint32_t ssts = port_base[10];
        int det = ssts & 0x0F;
        if(det == 0x03) {
            device_found = 1;
            break;
        }
    }
    
    if(!device_found) {
        kprint("  No device detected on port "); kprint_int(port); kprint("\n");
        return 0;
    }
    
    port_base[0] = (uint32_t)(uintptr_t)cmd_list_buf;
    port_base[1] = 0;
    port_base[2] = (uint32_t)(uintptr_t)cmd_table_buf;
    port_base[3] = 0;
    
    port_base[6] |= HBA_PxCMD_FRE; 
    port_base[6] |= HBA_PxCMD_ST; 
    
    kprint("  Port "); kprint_int(port); kprint(" ready\n");
    
    return 1;
}

int send_cmd(int port, uint8_t cmd, uint32_t lba, uint16_t* buffer) {
    if(!ahci_base) return 0;
    
    volatile uint32_t* port_base = ahci_base + (0x80 + port * 0x20)/4;
    
    if(!(port_base[6] & HBA_PxCMD_ST)) {
        kprint("  Port not started\n");
        return 0;
    }
    
    if(!port_ready(port)) {
        kprint("  Port not ready\n");
        return 0;
    }
    
    uint32_t ci = port_base[14];
    int slot = 0;
    while(slot < 32 && (ci & (1 << slot))) slot++;
    if(slot >= 32) {
        kprint("  No free command slot\n");
        return 0;
    }
    
    cmd_header_t* header = (cmd_header_t*)cmd_list_buf + slot;
    cmd_table_t* table = (cmd_table_t*)cmd_table_buf;
    
    for(int i = 0; i < sizeof(cmd_table_t)/4; i++) {
        ((uint32_t*)table)[i] = 0;
    }
    
    header->prdtl = 1;
    header->paddr = (uint32_t)(uintptr_t)table;
    
    table->prdt[0].dba = (uint32_t)(uintptr_t)buffer;
    table->prdt[0].dbc = (512 - 1) | (1 << 31);  
    
    uint8_t* fis = table->cfis;
    fis[0] = 0x27; 
    fis[1] = 0x80; 
    fis[2] = cmd;  
    
    if(cmd == 0xEC) {  
        fis[3] = 0;  
        fis[4] = 0;     
        fis[5] = 0;    
        fis[6] = 0;    
        fis[7] = 0;    
        fis[8] = 0;     
        fis[9] = 0;     
        fis[10] = 0;   
        fis[11] = 0;   
        fis[12] = 0;   
        fis[13] = 0;   
    } else {
        fis[4] = lba & 0xFF;
        fis[5] = (lba >> 8) & 0xFF;
        fis[6] = (lba >> 16) & 0xFF;
        fis[7] = 0x40;
        fis[8] = (lba >> 24) & 0xFF;
        fis[9] = (lba >> 32) & 0xFF;
        fis[10] = (lba >> 40) & 0xFF;
        fis[12] = 1;
    }
    
    port_base[14] = (1 << slot);
    
    int timeout = 50000000;
    while(timeout--) {
        if(!(port_base[14] & (1 << slot))) {
            uint32_t tfd = port_base[8];
            if(tfd & 0x01) {  
                kprint("  Error: tfd=0x"); kprint_hex32(tfd); kprint("\n");
                return 0;
            }
            return 1;
        }
    }
    
    kprint("  Command timeout\n");
    return 0;
}

static int identify_port(int port) {
    static uint16_t buf[256] __attribute__((aligned(512)));
    
    kprint("  Sending IDENTIFY command...\n");
    
    if(!send_cmd(port, 0xEC, 0, buf)) {
        kprint("  IDENTIFY command failed\n");
        return 0;
    }
    
    if(buf[0] == 0 && buf[1] == 0 && buf[2] == 0) {
    }
    
    for(int i = 0; i < 20; i++) {
        uint16_t w = buf[27 + i];
        detected_model[i*2] = w & 0xFF;
        detected_model[i*2+1] = (w >> 8) & 0xFF;
    }
    detected_model[40] = 0;
    for(int i = 39; i >= 0; i--) {
        if(detected_model[i] != ' ') break;
        detected_model[i] = '\0';
    }
    
    kprint("  Raw model: ");
    for(int i = 0; i < 40 && detected_model[i]; i++) {
        char s[2] = {detected_model[i], '\0'};
        kprint(s);
    }
    kprint("\n");
    char serial[21] = {0};
    for(int i = 0; i < 10; i++) {
        uint16_t w = buf[10 + i];
        serial[i*2] = w & 0xFF;
        serial[i*2+1] = (w >> 8) & 0xFF;
    }
    if(buf[83] & 0x40) {
        total_sectors = *(uint64_t*)&buf[100];
    } else if(buf[83] & 0x10) {
        total_sectors = *(uint64_t*)&buf[100];
    } else {
        total_sectors = *(uint32_t*)&buf[60];
    }
    
    uint32_t size_mb = (uint32_t)(total_sectors / 2048);
    uint32_t size_gb = size_mb / 1024;
    
    kprint("  Size: ");
    if(size_gb > 0) {
        kprint_int(size_gb); kprint(" GB");
        if(size_mb % 1024 > 0) {
            kprint(" ("); kprint_int(size_mb % 1024); kprint(" MB)");
        }
    } else {
        kprint_int(size_mb); kprint(" MB");
    }
    kprint("\n");
    
    return 1;
}

static int is_atapi_device(int port) {
    volatile uint32_t* port_base = ahci_base + (0x80 + port * 0x20)/4;
    uint32_t sig = port_base[5];
    return (sig == 0xEB140101); 
}

#define MBR_SIGNATURE 0xAA55

typedef struct {
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t lba_first;
    uint32_t sector_count;
} __attribute__((packed)) mbr_entry_t;

typedef struct {
    uint8_t bootstrap[446];
    mbr_entry_t partitions[4];
    uint16_t signature;
} __attribute__((packed)) mbr_t;

static const char* get_partition_type_name(uint8_t type) {
    switch(type) {
        case 0x00: return "Empty";
        case 0x01: return "FAT12";
        case 0x04: return "FAT16 (<32M)";
        case 0x05: return "Extended";
        case 0x06: return "FAT16 (>32M)";
        case 0x07: return "NTFS/exFAT";
        case 0x0B: return "FAT32";
        case 0x0C: return "FAT32 (LBA)";
        case 0x0E: return "FAT16 (LBA)";
        case 0x0F: return "Extended (LBA)";
        case 0x82: return "Linux swap";
        case 0x83: return "Linux ext2/3/4";
        case 0xDA: return "WNKFS";
        default: return "Unknown";
    }
}

static void read_mbr(int disk_index, int port) {
    uint16_t temp[256];
    uint8_t buffer[512];
    
    kprint("  Reading MBR...\n");
    
    if(!send_cmd(port, 0x25, 0, temp)) {
        kprint("  Failed to read sector 0\n");
        return;
    }
    
    for(int i = 0; i < 256; i++) {
        buffer[i*2] = temp[i] & 0xFF;
        buffer[i*2+1] = (temp[i] >> 8) & 0xFF;
    }
    
    mbr_t* mbr = (mbr_t*)buffer;
    
    if(mbr->signature != MBR_SIGNATURE) {
        kprint("  No valid MBR (signature: 0x");
        kprint_hex16(mbr->signature);
        kprint(")\n");
        
        PartitionInfo* p = &partitions[partition_count++];
        p->disk_index = disk_index;
        p->lba_start = 0;
        p->sector_count = (uint32_t)total_sectors;
        p->type = 0;
        p->bootable = 0;
        my_strcpy(p->fs_type, "raw");
        return;
    }
    
    kprint("  Valid MBR found\n");
    
    for(int i = 0; i < 4; i++) {
        if(mbr->partitions[i].type != 0) {
            PartitionInfo* p = &partitions[partition_count++];
            p->disk_index = disk_index;
            p->lba_start = mbr->partitions[i].lba_first;
            p->sector_count = mbr->partitions[i].sector_count;
            p->type = mbr->partitions[i].type;
            p->bootable = (mbr->partitions[i].status & 0x80) ? 1 : 0;
            
            const char* type_name = get_partition_type_name(p->type);
            my_strcpy(p->fs_type, type_name);
            
            kprint("    Partition "); kprint_int(i+1);
            kprint(": "); kprint(p->fs_type);
            if(p->bootable) kprint(" [BOOT]");
            kprint(" at sector "); kprint_int(p->lba_start);
            kprint(" size "); kprint_int(p->sector_count / 2048); kprint(" MB\n");
        }
    }
}

int eject_cdrom(int port) {
    kprint("Opening tray...\n");
    static uint16_t dummy_buf[256];
    if(send_cmd(port, 0x1B, 0, dummy_buf)) {
        kprint("Tray opened!\n");
        return 1;
    } else {
        kprint("Failed to open tray\n");
        return 0;
    }
}

void ahci_scan_ports(void) {
    if(!ahci_base) {
        kprint("\n[ERROR] AHCI not initialized!\n");
        return;
    }
    
    kprint("\n=== AHCI SCAN ===\n");
    disk_count = 0;
    partition_count = 0;
    
    for(int port = 0; port < ahci_port_count; port++) {
        if(!(ahci_port_mask & (1 << port))) continue;
        
        kprint("\nPort "); kprint_int(port); kprint(": ");
        
        volatile uint32_t* port_base = ahci_base + (0x80 + port * 0x20)/4;
        uint32_t ssts = port_base[10];
        uint32_t sig = port_base[5];
        
        kprint("SSTS=0x"); kprint_hex32(ssts);
        kprint(" SIG=0x"); kprint_hex32(sig);
        
        int det = ssts & 0x0F;
        if(det != 0x03) {
            kprint(" - NO DEVICE\n");
            continue;
        }
        
        kprint("\n");
        
        if(!reset_port(port)) {
            kprint("  Port reset failed\n");
            continue;
        }
        
        if(sig == 0xEB140101) {
            kprint("  CD/DVD-ROM detected\n");
            eject_cdrom(port);
            
            DiskInfo* d = &disks[disk_count++];
            d->port = port;
            d->is_atapi = 1;
            my_strcpy(d->model, "CD/DVD-ROM");
            d->sectors = 0;
            d->size_mb = 0;
            continue;
        }
        
        kprint("  Attempting IDENTIFY...\n");
        if(identify_port(port)) {
            kprint("  Drive identified successfully!\n");
            
            DiskInfo* d = &disks[disk_count++];
            d->port = port;
            d->is_atapi = 0;
            my_strcpy(d->model, detected_model);
            d->sectors = total_sectors;
            d->size_mb = (uint32_t)(total_sectors / 2048);
            
            read_mbr(disk_count - 1, port);
            
            if(current_port == -1) {
                current_port = port;
                kprint("Selected as primary drive\n");
            }
        } else {
            kprint("IDENTIFY FAILED\n");
        }
    }
    
    kprint("\n=== SCAN COMPLETE ===\n");
    kprint("Drives found: "); kprint_int(disk_count); kprint("\n");
    kprint("Partitions found: "); kprint_int(partition_count); kprint("\n");
    
    if(disk_count > 0) {
        kprint("\nAvailable drives:\n");
        for(int i = 0; i < disk_count; i++) {
            kprint("  "); kprint_int(i+1); kprint(". ");
            kprint(disks[i].model);
            if(disks[i].is_atapi) {
                kprint(" [CD/DVD]");
            } else {
                kprint(" ("); kprint_int(disks[i].size_mb); kprint(" MB)");
                if(disks[i].port == current_port) {
                    kprint(" [ACTIVE]");
                }
            }
            kprint("\n");
        }
    }
}

void ahci_init(void) {
    kprint("\n=== AHCI INIT ===\n");
    find_all_controllers();
    
    if(ahci_base) {
        kprint("\nAHCI Controller initialized\n");
        kprint("  Base address: 0x"); kprint_hex32((uint32_t)ahci_base); kprint("\n");
        kprint("  Ports: "); kprint_int(ahci_port_count); kprint("\n");
        ahci_scan_ports();
    } else {
        kprint("\nNo AHCI controller found\n");
        kprint("Falling back to IDE mode...\n");
    }
}

int ahci_read_sector(uint32_t lba, uint16_t* buf) {
    if(current_port == -1) {
        kprint("[AHCI] No drive selected\n");
        return 0;
    }
    return send_cmd(current_port, 0x25, lba, buf);
}

int ahci_write_sector(uint32_t lba, uint16_t* buf) {
    if(current_port == -1) {
        kprint("[AHCI] No drive selected\n");
        return 0;
    }
    return send_cmd(current_port, 0x35, lba, buf);
}

int ahci_identify_drive(void) {
    if(current_port == -1) {
        kprint("[AHCI] No drive selected\n");
        return 0;
    }
    
    static uint16_t buf[256] __attribute__((aligned(512)));
    
    if(!send_cmd(current_port, 0xEC, 0, buf)) {
        kprint("[AHCI] IDENTIFY command failed\n");
        return 0;
    }
    
    for(int i = 0; i < 20; i++) {
        uint16_t w = buf[27 + i];
        detected_model[i*2] = w & 0xFF;
        detected_model[i*2+1] = (w >> 8) & 0xFF;
    }
    detected_model[40] = '\0';
    
    for(int i = 39; i >= 0; i--) {
        if(detected_model[i] != ' ') break;
        detected_model[i] = '\0';
    }
    
    if(buf[83] & 0x40) {
        total_sectors = *(uint64_t*)&buf[100];
    } else {
        total_sectors = *(uint32_t*)&buf[60];
    }
    
    kprint("[AHCI] Drive identified: ");
    kprint(detected_model);
    kprint("\n");
    
    return 1;
}

void cmd_scan_all(void) {
    ahci_scan_ports();
}

void cmd_show_disks(void) {
    if(disk_count == 0) {
        kprint("No disks found. Run 'scan' first.\n");
        return;
    }
    
    kprint("\n=== DISKS ===\n");
    for(int i = 0; i < disk_count; i++) {
        DiskInfo* d = &disks[i];
        kprint_int(i+1); kprint(". ");
        kprint(d->model);
        if(d->is_atapi) {
            kprint(" [CD/DVD]");
        } else {
            kprint(" ("); kprint_int(d->size_mb); kprint(" MB)");
            if(d->port == current_port) kprint(" [ACTIVE]");
        }
        kprint("\n");
    }
}

void cmd_show_partitions(void) {
    if(partition_count == 0) {
        kprint("No partitions found\n");
        return;
    }
    
    kprint("\n=== PARTITIONS ===\n");
    for(int i = 0; i < partition_count; i++) {
        PartitionInfo* p = &partitions[i];
        kprint_int(i+1); kprint(". ");
        kprint(p->fs_type);
        if(p->bootable) kprint(" [BOOT]");
        kprint(" - ");
        
        uint32_t size_mb = p->sector_count / 2048;
        if(size_mb > 1024) {
            kprint_int(size_mb / 1024); kprint(" GB");
            if(size_mb % 1024 > 0) {
                kprint(" ("); kprint_int(size_mb % 1024); kprint(" MB)");
            }
        } else {
            kprint_int(size_mb); kprint(" MB");
        }
        
        kprint(" at sector "); kprint_int(p->lba_start);
        kprint("\n");
    }
}

void cmd_select_disk(int idx) {
    if(idx < 1 || idx > disk_count) {
        kprint("Invalid disk index (1-"); kprint_int(disk_count); kprint(")\n");
        return;
    }
    current_port = disks[idx-1].port;
    kprint("Selected: "); kprint(disks[idx-1].model);
    if(disks[idx-1].is_atapi) {
        kprint(" [CD/DVD]");
    }
    kprint("\n");
}

void cmd_partition_info(int idx) {
    if(idx < 1 || idx > partition_count) {
        kprint("Invalid partition index (1-"); kprint_int(partition_count); kprint(")\n");
        return;
    }
    
    PartitionInfo* p = &partitions[idx-1];
    kprint("\n=== PARTITION INFO ===\n");
    kprint("Type: "); kprint(p->fs_type); kprint("\n");
    kprint("Start sector: "); kprint_int(p->lba_start); kprint("\n");
    kprint("Sectors: "); kprint_int(p->sector_count); kprint("\n");
    kprint("Size: "); kprint_int(p->sector_count / 2048); kprint(" MB\n");
    kprint("Bootable: "); kprint(p->bootable ? "Yes\n" : "No\n");
}

void cmd_ahci_debug(void) {
    kprint("\n=== AHCI DEBUG ===\n");
    
    if(!ahci_base) {
        kprint("AHCI not initialized!\n");
        return;
    }
    
    kprint("AHCI base: 0x"); kprint_hex32((uint32_t)ahci_base); kprint("\n");
    
    uint32_t cap = ahci_base[0];
    kprint("CAP: 0x"); kprint_hex32(cap); kprint("\n");
    kprint("  Ports: "); kprint_int((cap & 0x1F) + 1); kprint("\n");
    kprint("  Supports: ");
    if(cap & (1 << 5)) kprint("64-bit ");
    if(cap & (1 << 8)) kprint("NCQ ");
    if(cap & (1 << 15)) kprint("PIO ");
    if(cap & (1 << 17)) kprint("SXS ");
    kprint("\n");
    
    uint32_t ghc = ahci_base[1];
    kprint("GHC: 0x"); kprint_hex32(ghc); kprint(" - ");
    if(ghc & 1) kprint("AHCI enabled");
    else kprint("AHCI disabled");
    kprint("\n");
    
    uint32_t pi = ahci_base[3];
    kprint("PI (port mask): 0x"); kprint_hex32(pi); kprint("\n");
    
    uint32_t vs = ahci_base[4];
    kprint("VS: "); 
    kprint_int((vs >> 8) & 0xFF); kprint(".");
    kprint_int(vs & 0xFF); kprint("\n");
    
    for(int port = 0; port < 32; port++) {
        if(pi & (1 << port)) {
            kprint("\nPort "); kprint_int(port); kprint(":\n");
            
            volatile uint32_t* port_base = ahci_base + (0x80 + port * 0x20)/4;
            
            uint32_t ssts = port_base[10];
            kprint("  SSTS: 0x"); kprint_hex32(ssts); kprint(" - ");
            
            int det = ssts & 0x0F;
            if(det == 0) kprint("No device");
            else if(det == 1) kprint("Device present but PHY offline");
            else if(det == 3) kprint("Device present and PHY online");
            else if(det == 4) kprint("PHY offline (reset)");
            kprint("\n");
            
            uint32_t sig = port_base[5];
            kprint("  SIG: 0x"); kprint_hex32(sig); kprint(" - ");
            if(sig == 0x00000101) kprint("SATA drive");
            else if(sig == 0xEB140101) kprint("ATAPI (CD/DVD)");
            else if(sig == 0) kprint("No device");
            else kprint("Unknown");
            kprint("\n");
            
            uint32_t cmd = port_base[6];
            kprint("  CMD: 0x"); kprint_hex32(cmd); kprint("\n");
            
            uint32_t tfd = port_base[8];
            kprint("  TFD: 0x"); kprint_hex32(tfd); kprint("\n");
        }
    }
}
#include <stdint.h>
#include "video.h"
#include "cdrom.h"
#include "string_utils.h"
#include "ata.h"

extern "C" {
    int atapi_init(void);
    int atapi_read_sector(uint32_t lba, uint8_t* buffer);
    void read_cdrom_root(uint32_t root_lba);
}
struct DiskDriver {
    void (*read)(uint32_t lba, uint16_t* buf);
    void (*write)(uint32_t lba, uint16_t* buf);
    const char* name;
    uint8_t present;
    char model[41];
};

extern "C" {
    uint16_t zero_buffer[256];
    
    DiskDriver ide_driver;
    DiskDriver real_pc_driver;
    DiskDriver* current_driver = &ide_driver;
}

extern "C" {
    void kprint(const char* str);
    void kprint_int(uint32_t n);
    void kprint_hex(uint16_t n);
    void clear_screen();
    uint8_t inb(uint16_t port);
    void outb(uint16_t port, uint8_t val);
    uint16_t inw(uint16_t port);
    void outw(uint16_t port, uint16_t val);
}

void kprint_int(uint32_t n) {
    if(n == 0) { kprint("0"); return; }
    char buf[12]; int i = 10; buf[11] = '\0';
    while(n > 0 && i >= 0) { 
        buf[i--] = (n % 10) + '0'; 
        n /= 10; 
    }
    kprint(&buf[i+1]);
}

void kprint_hex(uint16_t n) {
    const char* hex = "0123456789ABCDEF";
    char buf[5];
    buf[0] = hex[(n >> 12) & 0xF];
    buf[1] = hex[(n >> 8) & 0xF];
    buf[2] = hex[(n >> 4) & 0xF];
    buf[3] = hex[n & 0xF];
    buf[4] = '\0';
    kprint(buf);
}

static void ata_wait_bsy(void) {
    for(volatile int i = 0; i < 5000000; i++) {
        uint8_t status = inb(ata_base_port + 7);
        if((status & 0x80) == 0) return;
    }
    kprint("ATA: timeout waiting for not busy\n");
}

static void ata_wait_drq(void) {
    for(volatile int i = 0; i < 5000000; i++) {
        uint8_t status = inb(ata_base_port + 7);
        if(status & 0x08) return;
        if(status & 0x01) {
            kprint("ATA: error\n");
            return;
        }
    }
    kprint("ATA: timeout waiting for DRQ\n");
}

extern "C" void read_sector(uint32_t lba, uint16_t* buf) {
    for(int i = 0; i < 256; i++) buf[i] = 0;
    
    outb(ata_base_port + 6, 0xE0 | ((lba >> 24) & 0x0F));
    ata_wait_bsy();
    
    outb(ata_base_port + 2, 1);           
    outb(ata_base_port + 3, (uint8_t)(lba & 0xFF));     
    outb(ata_base_port + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(ata_base_port + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(ata_base_port + 7, 0x20);
    ata_wait_drq();
    for(int i = 0; i < 256; i++) {
        buf[i] = inw(ata_base_port);
    }
    ata_wait_bsy();
}

extern "C" void write_sector(uint32_t lba, uint16_t* buf) {
    outb(ata_base_port + 6, 0xE0 | ((lba >> 24) & 0x0F));
    ata_wait_bsy();
    outb(ata_base_port + 2, 1);
    outb(ata_base_port + 3, (uint8_t)(lba & 0xFF));   
    outb(ata_base_port + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(ata_base_port + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(ata_base_port + 7, 0x30);
    ata_wait_drq();
    for(int i = 0; i < 256; i++) {
        outw(ata_base_port, buf[i]);
    }
    ata_wait_bsy();
    outb(ata_base_port + 7, 0xE7);
    ata_wait_bsy();
}
static int detect_ata_port(void) {
    uint16_t ports[] = {0x1F0, 0x170};
    const char* port_names[] = {"Primary", "Secondary"};
    
    for(int p = 0; p < 2; p++) {
        ata_base_port = ports[p];
        
        kprint("[ATA] Trying ");
        kprint(port_names[p]);
        kprint(" port (0x");
        kprint_hex(ata_base_port);
        kprint(")...\n");
        
        outb(ata_base_port + 6, 0xA0);
        ata_wait_bsy();
        
        outb(ata_base_port + 7, 0xEC);
        
        for(volatile int i = 0; i < 500000; i++);
        
        uint8_t status = inb(ata_base_port + 7);
        if(status != 0 && status != 0xFF) {
            kprint("[ATA] Disk found on ");
            kprint(port_names[p]);
            kprint(" port!\n");
            return 1;
        }
    }
    return 0;
}
extern "C" int test_disk(void) {
    kprint("[ATA] Testing disk on port 0x");
    kprint_hex(ata_base_port);
    kprint("...\n");
    
    uint16_t write_buf[256];
    uint16_t read_buf[256];
    
    for(int i = 0; i < 256; i++) {
        write_buf[i] = i;
    }
    
    kprint("[ATA] Writing to sector 1...\n");
    write_sector(1, write_buf);
    
    kprint("[ATA] Reading from sector 1...\n");
    read_sector(1, read_buf);
    
    int ok = 1;
    for(int i = 0; i < 256; i++) {
        if(read_buf[i] != write_buf[i]) {
            ok = 0;
            break;
        }
    }
    
    if(ok) {
        kprint("[ATA] DISK OK! Read/Write works.\n");
        return 1;
    } else {
        kprint("[ATA] DISK FAILED! Read/Write error.\n");
        return 0;
    }
}

extern "C" void init_disk_system(void) {
    kprint("[ATA] Initializing...\n");
    
    if(!detect_ata_port()) {
        kprint("[ATA] No hard disk found\n");
    } else {
        ata_wait_drq();
        uint16_t identify_buf[256];
        for(int i = 0; i < 256; i++) {
            identify_buf[i] = inw(ata_base_port);
        }
        
        char model[41] = {0};
        for(int i = 0; i < 20; i++) {
            uint16_t w = identify_buf[27 + i];
            model[i*2] = (char)(w >> 8);
            model[i*2+1] = (char)(w & 0xFF);
        }
        for(int i = 39; i >= 0; i--) {
            if(model[i] != ' ') break;
            model[i] = '\0';
        }
        
        kprint("[ATA] Found HDD: ");
        kprint(model);
        kprint("\n");
        
        test_disk();
    }
    
    if(atapi_init()) {
        fat_mount();   
        atapi_mount_iso();
    }
}
void format_disk(void) {
    kprint("[DISK] Formatting disk (preserving WnkaSXS)...\n");
    
    uint16_t old_dir_buf[256];
    read_sector(100, old_dir_buf);
    
    int sxs_slot = -1;
    int sxs_sector = 0;
    for(int i = 0; i < 32; i++) {
        char name[12] = {0};
        for(int j = 0; j < 11; j++) name[j] = ((char*)old_dir_buf)[i*16 + j];
        if(my_strcmp("WnkaSXS", name) == 0) {
            sxs_slot = i;
            sxs_sector = old_dir_buf[i*8 + 6];
            break;
        }
    }
    
    uint16_t sxs_data[256];
    if(sxs_slot != -1) {
        read_sector(sxs_sector, sxs_data);
        kprint("[DISK] Preserving WnkaSXS backup\n");
    }
    
    uint16_t dir_buf[256];
    for(int i = 0; i < 256; i++) dir_buf[i] = 0;
    
    if(sxs_slot != -1) {
        for(int j = 0; j < 7; j++) ((char*)dir_buf)[sxs_slot*16 + j] = "WnkaSXS"[j];
        ((char*)dir_buf)[sxs_slot*16 + 11] = 1;
        dir_buf[sxs_slot*8 + 6] = sxs_sector;
        dir_buf[sxs_slot*8 + 7] = 0;
        
        write_sector(sxs_sector, sxs_data);
        
        kprint("[DISK] WnkaSXS restored\n");
    }
    
    write_sector(100, dir_buf);
    
    uint16_t check_buf[256];
    read_sector(100, check_buf);
    
    int ok = 1;
    for(int i = 0; i < 256; i++) {
        if(check_buf[i] != dir_buf[i]) { ok = 0; break; }
    }
    
    if(ok) {
        kprint("[DISK] Format OK (WnkaSXS preserved)\n");
    } else {
        kprint("[DISK] Format FAILED! Check disk connection\n");
    }
}

extern "C" int identify_drive(uint16_t port) {
    ata_base_port = port;
    
    for(int i = 0; i < 40; i++) detected_model[i] = 0;
    
    outb(ata_base_port + 6, 0xA0);
    ata_wait_bsy();
    
    outb(ata_base_port + 7, 0xEC);
    
    if(inb(ata_base_port + 7) == 0) return 0;
    
    for(volatile int i = 0; i < 10000000; i++) {
        if(inb(ata_base_port + 7) & 0x08) break;
    }
    
    uint16_t identify_buf[256];
    for(int i = 0; i < 256; i++) {
        identify_buf[i] = inw(ata_base_port);
    }
    
    for(int i = 0; i < 20; i++) {
        uint16_t w = identify_buf[27 + i];
        detected_model[i*2] = (char)(w >> 8);
        detected_model[i*2 + 1] = (char)(w & 0xFF);
    }
    
    for(int i = 39; i >= 0; i--) {
        if(detected_model[i] != ' ') break;
        detected_model[i] = '\0';
    }
    
    for(int i = 0; i < 40; i++) {
        ide_driver.model[i] = detected_model[i];
        real_pc_driver.model[i] = detected_model[i];
    }
    ide_driver.present = 1;
    real_pc_driver.present = 1;
    
    return 1;
}

extern "C" void check_sata_mode() {
    kprint("\n-------------------------------------------");
    kprint("\n STORAGE CONTROLLER");
    kprint("\n-------------------------------------------");
    
    if(ide_driver.present) {
        kprint("\nDRIVE: ");
        kprint(ide_driver.model);
    } else {
        kprint("\nNO DRIVE DETECTED");
    }
    
    kprint("\nMODE: Standard ATA (0x1F0)");
    kprint("\n-------------------------------------------\n");
}

extern "C" void scan_all_disks() {
    uint16_t ports[] = {0x1F0, 0x170};
    const char* names[] = {"Primary", "Secondary"};
    
    kprint("\n=== SCANNING PORTS ===\n");
    
    for(int i = 0; i < 2; i++) {
        kprint(names[i]);
        kprint(" (0x");
        kprint_hex(ports[i]);
        kprint("): ");
        
        if(identify_drive(ports[i])) {
            kprint(detected_model);
        } else {
            kprint("[NO DISK]");
        }
        kprint("\n");
    }
    
    identify_drive(0x1F0);
}

extern "C" void disk_menu() {
    clear_screen();
    
    while(1) {
        kprint("\n=== DISK MANAGER ===\n");
        kprint("1. Scan ports\n");
        kprint("2. Show drive\n");
        kprint("3. Read sector 0\n");
        kprint("4. Exit\n");
        kprint("Choice: ");
        
        uint8_t choice = 0;
        while(!choice) {
            if(inb(0x64) & 1) {
                uint8_t key = inb(0x60);
                if(key >= 0x02 && key <= 0x05) choice = key - 0x01;
                if(key == 0x01) return;
            }
        }
        
        kprint("\n");
        switch(choice) {
            case 1: scan_all_disks(); break;
            case 2: 
                if(ide_driver.present) {
                    kprint("Drive: ");
                    kprint(ide_driver.model);
                    kprint("\n");
                } else {
                    kprint("No drive\n");
                }
                break;
            case 3: {
                uint16_t buf[256];
                read_sector(0, buf);
                kprint("Sector 0 read OK\n");
                kprint("First bytes: ");
                for(int i = 0; i < 4; i++) {
                    kprint_hex(buf[i] & 0xFF);
                    kprint(" ");
                }
                kprint("\n");
                break;
            }
            case 4: return;
        }
        
        kprint("\nPress any key...");
        while(!(inb(0x64) & 1));
        while(inb(0x64) & 1) inb(0x60);
        clear_screen();
    }
}

extern "C" void check_disk_health() { 
    uint8_t status = inb(ata_base_port + 7);
    kprint("Status: ");
    kprint_hex(status);
    kprint("\n");
}
extern "C" void restore_disk_brute() { disk_menu(); }
extern "C" void disk_death() { kprint("Use menu\n"); }
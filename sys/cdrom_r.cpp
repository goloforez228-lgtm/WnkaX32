#include "cdrom_r.h"
#include "kernel_stubs.h"
#include "video.h"
#include "ata.h"

#define NULL 0

CdromDeviceExtension cdrom_dev;

static int atapi_send_packet(uint8_t* cmd, int cmd_len, uint8_t* buffer, 
                              uint32_t buffer_len, int write_to_device,
                              uint8_t* sense, int* sense_len)
{
    uint16_t port = ata_base_port;
    
    outb(port + 6, 0xA0);
    for(volatile int i = 0; i < 1000; i++);
    
    int timeout = 1000000;
    while(timeout-- > 0) {
        if(!(inb(port + 7) & 0x80)) break;
    }
    if(timeout <= 0) return -1;
    
    uint8_t status = inb(port + 7);
    if(status & 0x01) {  
        kprint("[ATAPI] Error status\n");
        return -1;
    }
    
    outb(port + 1, 0x00); 
    outb(port + 2, 0x00); 
    outb(port + 3, 0x00); 
    outb(port + 4, 0x00);  
    outb(port + 5, 0x00);  
    outb(port + 7, 0xA0);  
    
    timeout = 1000000;
    while(timeout-- > 0) {
        status = inb(port + 7);
        if(status & 0x01) { 
            kprint("[ATAPI] No media or error\n");
            return -1;
        }
        if(status & 0x08) break;  
    }
    if(timeout <= 0) return -1;
    
    for(int i = 0; i < 12; i++) {
        uint16_t word;
        if(i < cmd_len) {
            word = cmd[i] | (cmd[i+1] << 8);
        } else {
            word = 0;
        }
        outw(port, word);
    }
    
    timeout = 1000000;
    while(timeout-- > 0) {
        status = inb(port + 7);
        if(status & 0x01) {  
            if(sense && sense_len) {
                for(int i = 0; i < 16; i++) {
                    sense[i] = inb(port + 7);
                }
                *sense_len = 16;
            }
            return -1;
        }
        if(status & 0x08) {  
            break;
        }
        if(status & 0x80) continue;  
    }
    
    if(buffer && buffer_len > 0) {
        uint32_t words = (buffer_len + 1) / 2;
        for(uint32_t i = 0; i < words && i < buffer_len/2; i++) {
            uint16_t word = inw(port);
            buffer[i*2] = word & 0xFF;
            buffer[i*2 + 1] = (word >> 8) & 0xFF;
        }
    }
    
    return 0;
}

int cdrom_send_command(uint8_t* cdb, int cdb_len, uint8_t* buffer,
                        uint32_t buffer_len, int write_to_device,
                        uint8_t* sense, int* sense_len)
{
    return atapi_send_packet(cdb, cdb_len, buffer, buffer_len, 
                             write_to_device, sense, sense_len);
}

int cdrom_test_unit_ready(void)
{
    uint8_t cdb[6] = {SCSIOP_TEST_UNIT_READY, 0, 0, 0, 0, 0};
    return cdrom_send_command(cdb, 6, NULL, 0, 0, NULL, NULL);
}

int cdrom_read_toc(uint8_t* buffer, uint32_t buffer_len, int msf)
{
    uint8_t cdb[10] = {0};
    cdb[0] = SCSIOP_READ_TOC;
    if(msf) cdb[1] = 0x02;
    cdb[7] = (buffer_len >> 8) & 0xFF;
    cdb[8] = buffer_len & 0xFF;
    return cdrom_send_command(cdb, 10, buffer, buffer_len, 0, NULL, NULL);
}

int cdrom_read_toc_ex(uint8_t* buffer, uint32_t buffer_len, int format, int track)
{
    uint8_t cdb[10] = {0};
    cdb[0] = SCSIOP_READ_TOC;
    cdb[1] = format & 0x0F;
    cdb[6] = track;
    cdb[7] = (buffer_len >> 8) & 0xFF;
    cdb[8] = buffer_len & 0xFF;
    return cdrom_send_command(cdb, 10, buffer, buffer_len, 0, NULL, NULL);
}

int cdrom_eject_media(void)
{
    uint8_t cdb[6] = {SCSIOP_START_STOP_UNIT, 0, 0, 0, 0x02, 0};
    return cdrom_send_command(cdb, 6, NULL, 0, 0, NULL, NULL);
}

int cdrom_load_media(void)
{
    uint8_t cdb[6] = {SCSIOP_START_STOP_UNIT, 0, 0, 0, 0x03, 0};
    return cdrom_send_command(cdb, 6, NULL, 0, 0, NULL, NULL);
}

int cdrom_prevent_removal(int prevent)
{
    uint8_t cdb[6] = {SCSIOP_MEDIUM_REMOVAL, 0, 0, 0, prevent, 0};
    return cdrom_send_command(cdb, 6, NULL, 0, 0, NULL, NULL);
}

int cdrom_play_audio_msf(uint8_t start_m, uint8_t start_s, uint8_t start_f,
                          uint8_t end_m, uint8_t end_s, uint8_t end_f)
{
    uint8_t cdb[10] = {SCSIOP_READ_TOC, 0, 0, start_m, start_s, start_f, end_m, end_s, end_f, 0};
    cdb[0] = 0x47;
    return cdrom_send_command(cdb, 10, NULL, 0, 0, NULL, NULL);
}

int cdrom_pause_audio(void)
{
    uint8_t cdb[6] = {0x4B, 0, 0, 0, 0, 0};
    return cdrom_send_command(cdb, 6, NULL, 0, 0, NULL, NULL);
}

int cdrom_resume_audio(void)
{
    uint8_t cdb[6] = {0x4B, 0, 0, 0, 1, 0};
    return cdrom_send_command(cdb, 6, NULL, 0, 0, NULL, NULL);
}

int cdrom_stop_audio(void)
{
    uint8_t cdb[6] = {0x4E, 0, 0, 0, 0, 0};
    return cdrom_send_command(cdb, 6, NULL, 0, 0, NULL, NULL);
}

int cdrom_get_volume(uint8_t* left, uint8_t* right)
{
    uint8_t cdb[10] = {0x44, 0, 0, 0, 0, 0, 0, 0, 8, 0};
    uint8_t buffer[8];
    int result = cdrom_send_command(cdb, 10, buffer, 8, 0, NULL, NULL);
    if(result == 0)
    {
        *left = buffer[1];
        *right = buffer[3];
    }
    return result;
}

int cdrom_set_volume(uint8_t left, uint8_t right)
{
    uint8_t cdb[10] = {0x46, 0, 0, 0, left, 0, right, 0, 0, 0};
    return cdrom_send_command(cdb, 10, NULL, 0, 0, NULL, NULL);
}

int cdrom_read_capacity(uint32_t* last_sector, uint32_t* sector_size)
{
    uint8_t cdb[10] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t buffer[8];
    int result = cdrom_send_command(cdb, 10, buffer, 8, 0, NULL, NULL);
    if(result == 0)
    {
        *last_sector = (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
        *sector_size = (buffer[4] << 24) | (buffer[5] << 16) | (buffer[6] << 8) | buffer[7];
    }
    return result;
}

int cdrom_read10(uint32_t lba, uint16_t sectors, uint8_t* buffer, uint32_t buffer_len)
{
    uint8_t cdb[10] = {SCSIOP_READ_CD, 0, 
                       (uint8_t)(lba >> 24), (uint8_t)(lba >> 16),
                       (uint8_t)(lba >> 8), (uint8_t)(lba),
                       (uint8_t)(sectors >> 8), (uint8_t)(sectors),
                       0};
    return cdrom_send_command(cdb, 10, buffer, buffer_len, 0, NULL, NULL);
}

int cdrom_get_configuration(uint8_t* buffer, uint32_t buffer_len)
{
    uint8_t cdb[10] = {SCSIOP_GET_CONFIGURATION, 0x02, 
                       0, 0, 0,
                       (uint8_t)(buffer_len >> 16), (uint8_t)(buffer_len >> 8),
                       (uint8_t)(buffer_len), 0};
    return cdrom_send_command(cdb, 10, buffer, buffer_len, 0, NULL, NULL);
}

int cdrom_media_changed(void)
{
    uint8_t cdb[6] = {SCSIOP_TEST_UNIT_READY, 0, 0, 0, 0, 0};
    int result = cdrom_send_command(cdb, 6, NULL, 0, 0, NULL, NULL);
    return (result != 0) ? 1 : 0;
}

void cdrom_init(void)
{
    kprint("[CDROM] Initializing CD-ROM driver (ReactOS port)...\n");
    
    cdrom_dev.DeviceType = 5;
    cdrom_dev.TimeOutValue = 10;
    cdrom_dev.SectorSize = 2048;
    cdrom_dev.SectorShift = 11;
    cdrom_dev.MediaState = MediaUnknown;
    
    int ready = cdrom_test_unit_ready();
    if(ready == 0)
    {
        cdrom_dev.MediaState = MediaPresent;
        kprint("[CDROM] Media present\n");
    }
    else
    {
        cdrom_dev.MediaState = MediaNotPresent;
        kprint("[CDROM] No media\n");
    }
    
    kprint("[CDROM] Ready!\n");
}

void cdrom_handle_error(uint8_t* sense, int sense_len)
{
    if(sense && sense_len >= 2)
    {
        uint8_t sense_key = sense[2] & 0x0F;
        uint8_t asc = sense[12];
        uint8_t ascq = sense[13];
        
        kprint("[CDROM] Sense key: 0x");
        kprint_hex8(sense_key);
        kprint(" ASC: 0x");
        kprint_hex8(asc);
        kprint(" ASCQ: 0x");
        kprint_hex8(ascq);
        kprint("\n");
        
        switch(sense_key)
        {
            case 0x02:
                kprint("[CDROM] Not ready\n");
                break;
            case 0x06:
                kprint("[CDROM] Unit attention (media changed?)\n");
                cdrom_dev.MediaState = MediaUnknown;
                break;
            case 0x03:
                kprint("[CDROM] Medium error\n");
                break;
            case 0x05:
                kprint("[CDROM] Illegal request\n");
                break;
        }
    }
}
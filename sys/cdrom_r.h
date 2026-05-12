#ifndef CDROM_R_H
#define CDROM_R_H

#include <stdint.h>
#include "ata.h"

#define SCSI_CDROM_TIMEOUT          10

#define SCSIOP_TEST_UNIT_READY      0x00
#define SCSIOP_INQUIRY              0x12
#define SCSIOP_READ_CD              0xBE
#define SCSIOP_READ_TOC             0x43
#define SCSIOP_READ_DVD_STRUCTURE   0xAD
#define SCSIOP_REPORT_KEY           0xA4
#define SCSIOP_SEND_KEY             0xA3
#define SCSIOP_GET_CONFIGURATION    0x46
#define SCSIOP_GET_EVENT_STATUS     0x4A
#define SCSIOP_MEDIUM_REMOVAL       0x1E
#define SCSIOP_START_STOP_UNIT      0x1B
#define SCSIOP_SYNCHRONIZE_CACHE    0x35
#define SCSIOP_READ_CAPACITY        0x25
#define SCSIOP_MODE_SENSE           0x5A
#define SCSIOP_MODE_SENSE10         0x5A

#define IOCTL_CDROM_READ_TOC        0x00024000
#define IOCTL_CDROM_READ_TOC_EX     0x00024001
#define IOCTL_CDROM_GET_DRIVE_GEOMETRY 0x00024028
#define IOCTL_CDROM_PLAY_AUDIO_MSF  0x00024012
#define IOCTL_CDROM_PAUSE_AUDIO     0x00024013
#define IOCTL_CDROM_RESUME_AUDIO    0x00024014
#define IOCTL_CDROM_STOP_AUDIO      0x00024015
#define IOCTL_CDROM_GET_VOLUME      0x00024016
#define IOCTL_CDROM_SET_VOLUME      0x00024017
#define IOCTL_CDROM_GET_CONFIGURATION 0x00024018
#define IOCTL_STORAGE_CHECK_VERIFY  0x002D4800
#define IOCTL_STORAGE_LOAD_MEDIA    0x002D480C
#define IOCTL_STORAGE_EJECT_MEDIA   0x002D4808
#define IOCTL_STORAGE_MEDIA_REMOVAL 0x002D4804

typedef struct {
    uint8_t  OperationCode;
    uint8_t  LunNate : 3;
    uint8_t  Reserved1 : 1;
    uint8_t  Format : 4;
    uint8_t  Reserved2;
    uint8_t  Reserved3;
    uint8_t  Reserved4;
    uint8_t  Reserved5;
    uint8_t  AllocationLength[2];
    uint8_t  Control;
} CdbReadToc;

typedef struct {
    uint8_t  OperationCode;
    uint8_t  LunNate : 3;
    uint8_t  Reserved1 : 1;
    uint8_t  Format : 4;
    uint8_t  Reserved2;
    uint8_t  Msf : 2;
    uint8_t  Reserved3 : 6;
    uint8_t  TrackSessionNumber;
    uint8_t  Reserved4;
    uint8_t  AllocationLength[2];
    uint8_t  Control;
} CdbReadTocEx;

typedef struct {
    uint8_t  OperationCode;
    uint8_t  Immediate : 1;
    uint8_t  Reserved1 : 7;
    uint8_t  Reserved2[2];
    uint8_t  Start : 1;
    uint8_t  LoadEject : 1;
    uint8_t  Reserved3 : 6;
    uint8_t  Control;
} CdbStartStop;

typedef struct {
    uint8_t  OperationCode;
    uint8_t  Reserved1;
    uint8_t  Reserved2;
    uint8_t  StartingM;
    uint8_t  StartingS;
    uint8_t  StartingF;
    uint8_t  EndingM;
    uint8_t  EndingS;
    uint8_t  EndingF;
    uint8_t  Control;
} CdbPlayAudioMsf;

typedef struct {
    uint8_t  OperationCode;
    uint8_t  Reserved1 : 4;
    uint8_t  Lun : 4;
    uint8_t  Reserved2;
    uint8_t  Reserved3 : 2;
    uint8_t  RelAdr : 1;
    uint8_t  Reserved4 : 5;
    uint8_t  StartingLba[4];
    uint8_t  Reserved5[2];
    uint8_t  TransferLength[4];
    uint8_t  Control;
} CdbRead10;

typedef struct {
    uint8_t  Length[2];
    uint8_t  SenseKey : 4;
    uint8_t  Reserved : 1;
    uint8_t  Ili : 1;
    uint8_t  Eom : 1;
    uint8_t  Filemark : 1;
    uint8_t  Information[4];
    uint8_t  AdditionalSenseLength;
    uint8_t  CmdSpecific[4];
    uint8_t  AdditionalSenseCode;
    uint8_t  AdditionalSenseCodeQualifier;
    uint8_t  FieldReplaceableUnitCode;
    uint8_t  SenseKeySpecific[3];
} SenseData;

typedef struct {
    uint16_t Length;
    uint8_t  FirstTrack;
    uint8_t  LastTrack;
} TocHeader;

typedef struct {
    uint8_t  Reserved;
    uint8_t  ControlAdr : 4;
    uint8_t  TrackNumber : 4;
    uint8_t  Reserved1;
    uint8_t  Address[4];
} TrackDescriptor;

typedef struct {
    uint16_t Length;
    uint8_t  FirstTrack;
    uint8_t  LastTrack;
    TrackDescriptor TrackData[1];
} ReadTocData;

typedef struct {
    uint8_t  PortNumber;
    uint8_t  PathId;
    uint8_t  TargetId;
    uint8_t  Lun;
} ScsiAddress;

typedef enum {
    MediaUnknown,
    MediaPresent,
    MediaNotPresent,
    MediaUnavailable
} MediaState;

typedef struct {
    uint16_t RequestedMode;
} VideoMode;

typedef struct {
    ScsiAddress     Address;
    uint8_t         DeviceType;
    int             IsMmc;
    int             IsDvd;
    int             IsWriter;
    int             MediaState;
    uint32_t        PartitionLength;
    uint32_t        SectorSize;
    uint8_t         SectorShift;
    uint16_t        TimeOutValue;
    uint8_t         InquiryData[36];
    uint8_t         SenseBuffer[32];
    void*           ScratchBuffer;
    uint32_t        ScratchBufferSize;
} CdromDeviceExtension;

void cdrom_init(void);
int  cdrom_send_command(uint8_t* cdb, int cdb_len, uint8_t* buffer, 
                        uint32_t buffer_len, int write_to_device, 
                        uint8_t* sense, int* sense_len);
int  cdrom_test_unit_ready(void);
int  cdrom_read_toc(uint8_t* buffer, uint32_t buffer_len, int msf);
int  cdrom_read_toc_ex(uint8_t* buffer, uint32_t buffer_len, 
                       int format, int track);
int  cdrom_eject_media(void);
int  cdrom_load_media(void);
int  cdrom_prevent_removal(int prevent);
int  cdrom_play_audio_msf(uint8_t start_m, uint8_t start_s, uint8_t start_f,
                          uint8_t end_m, uint8_t end_s, uint8_t end_f);
int  cdrom_pause_audio(void);
int  cdrom_resume_audio(void);
int  cdrom_stop_audio(void);
int  cdrom_get_volume(uint8_t* left, uint8_t* right);
int  cdrom_set_volume(uint8_t left, uint8_t right);
int  cdrom_read_capacity(uint32_t* last_sector, uint32_t* sector_size);
int  cdrom_read10(uint32_t lba, uint16_t sectors, uint8_t* buffer, uint32_t buffer_len);
int  cdrom_get_configuration(uint8_t* buffer, uint32_t buffer_len);
int  cdrom_media_changed(void);
void cdrom_handle_error(uint8_t* sense, int sense_len);


#endif
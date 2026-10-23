/*++
=============================================================================
 sec_ts_y79a.h - Samsung Y79A (sec_ts) touch driver for Windows ARM64
=============================================================================
 Hardware (verified on device y2q, SM-G986N):
   - I2C QUPv3 SE5 @ 0x00994000, slave 0x48, 400 kHz
   - IRQ: TLMM GPIO15, level-low  (NOT used: we poll)
   - No reset pin. Power: s2dos05-ldo2 (DVDD 1.8V) / ldo3 (AVDD 3.0V), boot-on
   - Protocol: 16-byte events, read via reg 0x60 (one) / 0x61 (all)
=============================================================================
--*/

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <spb.h>
#include <vhf.h>        // da keo theo hidclass.h (HID_XFER_PACKET)
#include <reshub.h>     // RESOURCE_HUB_PATH_SIZE / RESOURCE_HUB_DEVICE_NAME
#include <ntstrsafe.h>  // RtlStringCbPrintfW

#define SEC_TS_POOL_TAG 'TSYS'

//
// ---- Chip protocol constants (from sec_ts.h, y79a) -------------------------
//
#define SEC_TS_CMD_SENSE_ON            0x10
#define SEC_TS_CMD_SENSE_OFF           0x11
#define SEC_TS_CMD_SW_RESET            0x12
#define SEC_TS_CMD_SET_TOUCHFUNCTION   0x30
#define SEC_TS_READ_ONE_EVENT          0x60
#define SEC_TS_READ_ALL_EVENT          0x61
#define SEC_TS_CMD_CLEAR_EVENT_STACK   0x62
#define SEC_TS_CMD_SET_POWER_MODE      0xE4

#define SEC_TS_EVENT_BUFF_SIZE         16
#define SEC_TS_MAX_EVENTS              32
#define SEC_TS_MAX_FINGERS             10

// touch function bits written to 0x30
#define SEC_TS_BIT_TOUCH               0x0001
#define SEC_TS_BIT_PALM                0x0020
#define SEC_TS_BIT_WET                 0x0040
#define SEC_TS_TOUCHFUNCTION_VALUE     (SEC_TS_BIT_TOUCH | SEC_TS_BIT_PALM | SEC_TS_BIT_WET)

// eid (event id)
#define SEC_TS_EID_COORDINATE          0
#define SEC_TS_EID_STATUS              1

// tchsta (touch action)
#define SEC_TS_ACTION_NONE             0
#define SEC_TS_ACTION_PRESS            1
#define SEC_TS_ACTION_MOVE             2
#define SEC_TS_ACTION_RELEASE          3

//
// Decoded 16-byte coordinate event. Bitfields per struct sec_ts_event_coordinate.
//
typedef struct _SEC_TS_EVENT {
    UCHAR  Eid;          // byte0 bits[1:0]
    UCHAR  Tid;          // byte0 bits[5:2]  (finger id, 1-based from chip)
    UCHAR  Action;       // byte0 bits[7:6]
    USHORT X;            // 0..4095
    USHORT Y;            // 0..4095
    UCHAR  Major;
    UCHAR  Minor;
    UCHAR  Z;            // pressure 0..63
    UCHAR  LeftEvents;   // byte7 bits[4:0]
} SEC_TS_EVENT, *PSEC_TS_EVENT;

#pragma pack(push, 1)
typedef struct _SEC_TS_EVENT_RAW {
    UCHAR B0;
    UCHAR XHi;
    UCHAR YHi;
    UCHAR XyLo;
    UCHAR Major;
    UCHAR Minor;
    UCHAR ZType;
    UCHAR LeftFlag;
    UCHAR Noise;
    UCHAR MaxStrength;
    UCHAR Rsv10;
    UCHAR Rsv11;
    UCHAR Rsv12;
    UCHAR Rsv13;
    UCHAR Rsv14;
    UCHAR Rsv15;
} SEC_TS_EVENT_RAW, *PSEC_TS_EVENT_RAW;
#pragma pack(pop)

//
// ---- HID report (VHF) ------------------------------------------------------
//
#pragma pack(push, 1)
typedef struct _SEC_TS_HID_REPORT {
    UCHAR  ReportId;      // 1
    UCHAR  ContactCount;  // number of active contacts
    struct {
        UCHAR  TipSwitch; // 1 = finger down
        UCHAR  ContactId; // 0..9
        UCHAR  Confidence;
        USHORT X;
        USHORT Y;
    } Contact[SEC_TS_MAX_FINGERS];
} SEC_TS_HID_REPORT, *PSEC_TS_HID_REPORT;
#pragma pack(pop)

//
// ---- Per-finger tracking state ---------------------------------------------
//
typedef struct _SEC_TS_FINGER {
    BOOLEAN  Down;
    USHORT   X;
    USHORT   Y;
    UCHAR    Z;
} SEC_TS_FINGER, *PSEC_TS_FINGER;

//
// ---- Device context --------------------------------------------------------
//
typedef struct _SEC_TS_DEVICE {
    WDFDEVICE        Device;
    WDFIOTARGET      I2CTarget;
    WDFWAITLOCK      I2CLock;

    // polling
    WDFTIMER         PollTimer;
    volatile LONG    PollingActive;

    // HID / VHF
    VHFHANDLE        VhfHandle;
    BOOLEAN          VhfReady;          // EvtVhfReadyForNextReadReport fired
    BOOLEAN          VhfStarted;

    // report buffer (one per submit)
    PSEC_TS_HID_REPORT ReportBuffer;

    // fingers (index = ContactId)
    SEC_TS_FINGER    Fingers[SEC_TS_MAX_FINGERS];

    // stats
    ULONG            EventCount;
    ULONG            ErrorCount;
    ULONG            ReinitCount;

    BOOLEAN          HardwareReady;
} SEC_TS_DEVICE, *PSEC_TS_DEVICE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SEC_TS_DEVICE, SecTsGetDeviceContext)

//
// ---- I2C helpers (sec_ts_hw.c) ---------------------------------------------
//
NTSTATUS
SecTsI2cWrite(
    _In_ PSEC_TS_DEVICE Dev,
    _In_reads_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length
    );

NTSTATUS
SecTsI2cWriteRead(
    _In_ PSEC_TS_DEVICE Dev,
    _In_reads_bytes_(WriteLength) PUCHAR WriteBuffer,
    _In_ ULONG WriteLength,
    _Out_writes_bytes_(ReadLength) PUCHAR ReadBuffer,
    _In_ ULONG ReadLength
    );

NTSTATUS
SecTsWriteRegister(
    _In_ PSEC_TS_DEVICE Dev,
    _In_ UCHAR Reg,
    _In_reads_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length
    );

NTSTATUS
SecTsReadRegister(
    _In_ PSEC_TS_DEVICE Dev,
    _In_ UCHAR Reg,
    _Out_writes_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length
    );

//
// ---- Chip logic (sec_ts_chip.c) --------------------------------------------
//
NTSTATUS
SecTsChipInit(
    _In_ PSEC_TS_DEVICE Dev
    );

NTSTATUS
SecTsChipReadEvents(
    _In_ PSEC_TS_DEVICE Dev
    );

VOID
SecTsChipReleaseAll(
    _In_ PSEC_TS_DEVICE Dev
    );

//
// ---- HID / VHF (sec_ts_hid.c) ----------------------------------------------
//
NTSTATUS
SecTsHidCreate(
    _In_ PSEC_TS_DEVICE Dev
    );

VOID
SecTsHidDestroy(
    _In_ PSEC_TS_DEVICE Dev
    );

VOID
SecTsHidSubmitReport(
    _In_ PSEC_TS_DEVICE Dev
    );

//
// ---- Timer (sec_ts_poll.c) -------------------------------------------------
//
NTSTATUS
SecTsPollStart(
    _In_ PSEC_TS_DEVICE Dev
    );

VOID
SecTsPollStop(
    _In_ PSEC_TS_DEVICE Dev
    );
/*++
=============================================================================
 sec_ts_chip.c - protocol logic for Samsung Y79A touch controller
=============================================================================
 Init sequence (verified by I2C trace on Android + sec_ts source):
   1. clear event stack            : write 0x62
   2. wait for boot-complete event : read 0x60 x16 until status event
                                     with status_id == 0x00 and data == 0x20
                                     (trace showed: 09 00 10 ...)
   3. write 0x30 = 0x0061          : enable touch + palm + wet
   4. write 0xE4 = 0x01            : power mode normal
   5. write 0x10                   : SENSE_ON
   6. clear event stack again

 Event read loop:
   read 0x60 -> 16 bytes.  byte7 bits[4:0] = number of *additional* events
   still queued.  If > 0, read 0x61 for (left * 16) bytes in one go.

 Decode (struct sec_ts_event_coordinate):
   b0: eid:2 | tid:4 | tchsta:2
   b1: x_11_4        b2: y_11_4      b3: y_3_0:4 | x_3_0:4
   b4: major         b5: minor       b6: z:6 | ttype_3_2:2
   b7: left_event:5 | max_energy_flag:1 | ttype_1_0:2
=============================================================================
--*/

#include "sec_ts_y79a.h"

static VOID
SecTsDecodeEvent(
    _In_reads_bytes_(SEC_TS_EVENT_BUFF_SIZE) PUCHAR Raw,
    _Out_ PSEC_TS_EVENT Event
    )
{
    Event->Eid        = (UCHAR)(Raw[0] & 0x03);
    Event->Tid        = (UCHAR)((Raw[0] >> 2) & 0x0F);
    Event->Action     = (UCHAR)((Raw[0] >> 6) & 0x03);
    Event->X          = (USHORT)(((USHORT)Raw[1] << 4) | (Raw[3] >> 4));
    Event->Y          = (USHORT)(((USHORT)Raw[2] << 4) | (Raw[3] & 0x0F));
    Event->Major      = Raw[4];
    Event->Minor      = Raw[5];
    Event->Z          = (UCHAR)(Raw[6] & 0x3F);
    Event->LeftEvents = (UCHAR)(Raw[7] & 0x1F);
}

//
// Apply one coordinate event to the finger table.
//
static VOID
SecTsApplyCoordinate(
    _In_ PSEC_TS_DEVICE Dev,
    _In_ PSEC_TS_EVENT Event
    )
{
    ULONG index;

    // chip reports tid 1..10 (or 0..9 depending on fw); normalise to 0..9
    index = (Event->Tid > 0) ? (Event->Tid - 1) : 0;
    if (index >= SEC_TS_MAX_FINGERS) {
        return;
    }

    switch (Event->Action) {
    case SEC_TS_ACTION_PRESS:
    case SEC_TS_ACTION_MOVE:
        Dev->Fingers[index].Down = TRUE;
        Dev->Fingers[index].X    = Event->X;
        Dev->Fingers[index].Y    = Event->Y;
        Dev->Fingers[index].Z    = Event->Z;
        break;

    case SEC_TS_ACTION_RELEASE:
        Dev->Fingers[index].Down = FALSE;
        Dev->Fingers[index].X    = Event->X;
        Dev->Fingers[index].Y    = Event->Y;
        Dev->Fingers[index].Z    = 0;
        break;

    default:
        break;
    }
}

VOID
SecTsChipReleaseAll(
    _In_ PSEC_TS_DEVICE Dev
    )
{
    ULONG i;
    for (i = 0; i < SEC_TS_MAX_FINGERS; i++) {
        Dev->Fingers[i].Down = FALSE;
        Dev->Fingers[i].Z    = 0;
    }
}

NTSTATUS
SecTsChipInit(
    _In_ PSEC_TS_DEVICE Dev
    )
{
    NTSTATUS status;
    UCHAR    buf[SEC_TS_EVENT_BUFF_SIZE];
    UCHAR    cmd;
    UCHAR    val[2];
    ULONG    attempt;

    // 1. clear event stack
    cmd = SEC_TS_CMD_CLEAR_EVENT_STACK;
    (VOID)SecTsI2cWrite(Dev, &cmd, 1);

    // 2. wait for boot-complete event (status event, id 0x00)
    for (attempt = 0; attempt < 50; attempt++) {
        status = SecTsReadRegister(Dev, SEC_TS_READ_ONE_EVENT, buf, sizeof(buf));
        if (NT_SUCCESS(status)) {
            if (((buf[0] & 0x03) == SEC_TS_EID_STATUS) && (buf[1] == 0x00)) {
                break;
            }
        }
        LARGE_INTEGER delay;
        delay.QuadPart = -10000;    // 1 ms
        KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }

    if (attempt >= 50) {
        return STATUS_IO_TIMEOUT;
    }

    // 3. touch functions: TOUCH | PALM | WET = 0x0061
    val[0] = (UCHAR)(SEC_TS_TOUCHFUNCTION_VALUE & 0xFF);
    val[1] = (UCHAR)((SEC_TS_TOUCHFUNCTION_VALUE >> 8) & 0xFF);
    status = SecTsWriteRegister(Dev, SEC_TS_CMD_SET_TOUCHFUNCTION, val, 2);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // 4. power mode = normal
    val[0] = 0x01;
    status = SecTsWriteRegister(Dev, SEC_TS_CMD_SET_POWER_MODE, val, 1);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // 5. SENSE_ON
    cmd = SEC_TS_CMD_SENSE_ON;
    status = SecTsI2cWrite(Dev, &cmd, 1);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // 6. clear whatever piled up during init
    cmd = SEC_TS_CMD_CLEAR_EVENT_STACK;
    (VOID)SecTsI2cWrite(Dev, &cmd, 1);

    SecTsChipReleaseAll(Dev);
    return STATUS_SUCCESS;
}

//
// Read all queued events and update the finger table.
// Returns number of coordinate events applied.
//
NTSTATUS
SecTsChipReadEvents(
    _In_ PSEC_TS_DEVICE Dev
    )
{
    NTSTATUS        status;
    UCHAR           buf[SEC_TS_EVENT_BUFF_SIZE];
    SEC_TS_EVENT    event;
    UCHAR           left;
    ULONG           applied = 0;

    status = SecTsReadRegister(Dev, SEC_TS_READ_ONE_EVENT, buf, sizeof(buf));
    if (!NT_SUCCESS(status)) {
        return status;
    }

    SecTsDecodeEvent(buf, &event);

    if (event.Eid == SEC_TS_EID_COORDINATE) {
        SecTsApplyCoordinate(Dev, &event);
        applied++;
    } else if ((event.Eid == SEC_TS_EID_STATUS) && (buf[1] == 0x00) && (buf[2] == 0x20)) {
        // chip watchdog / ESD reset -> release everything and re-init
        SecTsChipReleaseAll(Dev);
        Dev->ReinitCount++;
        (VOID)SecTsChipInit(Dev);
        return STATUS_SUCCESS;
    }

    left = event.LeftEvents;
    if ((left > 0) && (left <= SEC_TS_MAX_EVENTS)) {
        ULONG total = left * SEC_TS_EVENT_BUFF_SIZE;
        PUCHAR bulk = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, total, SEC_TS_POOL_TAG);
        if (bulk != NULL) {
            status = SecTsReadRegister(Dev, SEC_TS_READ_ALL_EVENT, bulk, total);
            if (NT_SUCCESS(status)) {
                ULONG i;
                for (i = 0; i < left; i++) {
                    SecTsDecodeEvent(bulk + (i * SEC_TS_EVENT_BUFF_SIZE), &event);
                    if (event.Eid == SEC_TS_EID_COORDINATE) {
                        SecTsApplyCoordinate(Dev, &event);
                        applied++;
                    }
                }
            }
            ExFreePoolWithTag(bulk, SEC_TS_POOL_TAG);
        }
    }

    Dev->EventCount += applied;
    return STATUS_SUCCESS;
}
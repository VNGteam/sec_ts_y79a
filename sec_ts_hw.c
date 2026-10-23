/*++
=============================================================================
 sec_ts_hw.c - SPB (I2C) transport for the Samsung Y79A touch controller
=============================================================================
 The controller is reached through the parent SPB controller (qci2c) using
 IOCTL_SPB_EXECUTE_SEQUENCE, exactly like the WDK "SpbAccelerometer" sample.

 Register access model (verified from I2C trace on the device):
   write  : [reg] [data...]
   read   : [reg] then read N bytes (repeated start)
=============================================================================
--*/

#include "sec_ts_y79a.h"


//
// Build a one-entry (write) or two-entry (write-then-read) SPB transfer list
// in a single contiguous buffer and send it to the I2C target.
//
static NTSTATUS
SecTsSpbTransfer(
    _In_ PSEC_TS_DEVICE Dev,
    _In_reads_bytes_(WriteLength) PUCHAR WriteBuffer,
    _In_ ULONG WriteLength,
    _Out_writes_bytes_opt_(ReadLength) PUCHAR ReadBuffer,
    _In_ ULONG ReadLength
    )
{
    NTSTATUS            status;
    WDFREQUEST          request = NULL;
    WDFMEMORY           memory = NULL;
    SPB_TRANSFER_LIST*  list = NULL;
    PUCHAR              raw = NULL;
    ULONG               entries;
    ULONG               totalSize;
    ULONG               offset;
    WDF_OBJECT_ATTRIBUTES attrs;
    WDF_REQUEST_SEND_OPTIONS options;
    BOOLEAN             sendOk;

    entries = (ReadLength > 0) ? 2 : 1;

    // header + entries + payload (write bytes then read bytes)
    totalSize = (ULONG)(sizeof(SPB_TRANSFER_LIST) +
                        (entries * sizeof(SPB_TRANSFER_LIST_ENTRY)) +
                        WriteLength + ReadLength);

    status = WdfMemoryCreate(WDF_NO_OBJECT_ATTRIBUTES,
                             NonPagedPoolNx,
                             SEC_TS_POOL_TAG,
                             totalSize,
                             &memory,
                             (PVOID*)&raw);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlZeroMemory(raw, totalSize);

    list = (SPB_TRANSFER_LIST*)raw;
    SPB_TRANSFER_LIST_INIT(list, entries);

    offset = (ULONG)(sizeof(SPB_TRANSFER_LIST) +
                     (entries * sizeof(SPB_TRANSFER_LIST_ENTRY)));

    // entry 0: write [reg ...]
    // SPB_TRANSFER_BUFFER = { Format; union { Simple{Buffer,BufferCb} ... } }
    list->Transfers[0].Direction = SpbTransferDirectionToDevice;
    list->Transfers[0].DelayInUs = 0;
    list->Transfers[0].Buffer.Format        = SpbTransferBufferFormatSimple;
    list->Transfers[0].Buffer.Simple.Buffer = raw + offset;
    list->Transfers[0].Buffer.Simple.BufferCb = WriteLength;
    RtlCopyMemory(raw + offset, WriteBuffer, WriteLength);
    offset += WriteLength;

    if (entries == 2) {
        // entry 1: read N bytes (controller fills this area)
        list->Transfers[1].Direction = SpbTransferDirectionFromDevice;
        list->Transfers[1].DelayInUs = 0;
        list->Transfers[1].Buffer.Format          = SpbTransferBufferFormatSimple;
        list->Transfers[1].Buffer.Simple.Buffer   = raw + offset;
        list->Transfers[1].Buffer.Simple.BufferCb = ReadLength;
    }

    status = WdfRequestCreate(WDF_NO_OBJECT_ATTRIBUTES, Dev->I2CTarget, &request);
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(memory);
        return status;
    }

    status = WdfIoTargetFormatRequestForIoctl(Dev->I2CTarget,
                                              request,
                                              IOCTL_SPB_EXECUTE_SEQUENCE,
                                              memory,
                                              NULL,
                                              NULL,
                                              NULL);
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(request);
        WdfObjectDelete(memory);
        return status;
    }

    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    options.Timeout = WDF_REL_TIMEOUT_IN_MS(500);

    sendOk = WdfRequestSend(request, Dev->I2CTarget, &options);
    if (!sendOk) {
        status = WdfRequestGetStatus(request);
        WdfObjectDelete(request);
        WdfObjectDelete(memory);
        return status;
    }

    status = WdfRequestGetStatus(request);

    if (NT_SUCCESS(status) && (ReadLength > 0) && (ReadBuffer != NULL)) {
        // Simple.Buffer tro thang vao vung doc trong cung WDFMEMORY,
        // nen doc lai tu do (khong phai output memory cua request).
        PUCHAR readPtr = (PUCHAR)list->Transfers[1].Buffer.Simple.Buffer;
        if (readPtr != NULL) {
            RtlCopyMemory(ReadBuffer, readPtr, ReadLength);
        } else {
            status = STATUS_INVALID_BUFFER_SIZE;
        }
    }

    WdfObjectDelete(request);
    WdfObjectDelete(memory);
    return status;
}

NTSTATUS
SecTsI2cWrite(
    _In_ PSEC_TS_DEVICE Dev,
    _In_reads_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length
    )
{
    NTSTATUS status;

    WdfWaitLockAcquire(Dev->I2CLock, NULL);
    status = SecTsSpbTransfer(Dev, Buffer, Length, NULL, 0);
    WdfWaitLockRelease(Dev->I2CLock);

    if (!NT_SUCCESS(status)) {
        Dev->ErrorCount++;
    }
    return status;
}

NTSTATUS
SecTsI2cWriteRead(
    _In_ PSEC_TS_DEVICE Dev,
    _In_reads_bytes_(WriteLength) PUCHAR WriteBuffer,
    _In_ ULONG WriteLength,
    _Out_writes_bytes_(ReadLength) PUCHAR ReadBuffer,
    _In_ ULONG ReadLength
    )
{
    NTSTATUS status;

    WdfWaitLockAcquire(Dev->I2CLock, NULL);
    status = SecTsSpbTransfer(Dev, WriteBuffer, WriteLength, ReadBuffer, ReadLength);
    WdfWaitLockRelease(Dev->I2CLock);

    if (!NT_SUCCESS(status)) {
        Dev->ErrorCount++;
    }
    return status;
}

NTSTATUS
SecTsWriteRegister(
    _In_ PSEC_TS_DEVICE Dev,
    _In_ UCHAR Reg,
    _In_reads_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length
    )
{
    PUCHAR  buffer;
    NTSTATUS status;
    ULONG    i;

    if (Length > 32) {
        return STATUS_INVALID_PARAMETER;
    }

    buffer = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, Length + 1, SEC_TS_POOL_TAG);
    if (buffer == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    buffer[0] = Reg;
    for (i = 0; i < Length; i++) {
        buffer[i + 1] = Data[i];
    }

    status = SecTsI2cWrite(Dev, buffer, Length + 1);

    ExFreePoolWithTag(buffer, SEC_TS_POOL_TAG);
    return status;
}

NTSTATUS
SecTsReadRegister(
    _In_ PSEC_TS_DEVICE Dev,
    _In_ UCHAR Reg,
    _Out_writes_bytes_(Length) PUCHAR Data,
    _In_ ULONG Length
    )
{
    UCHAR reg = Reg;
    return SecTsI2cWriteRead(Dev, &reg, 1, Data, Length);
}
/*++
=============================================================================
 sec_ts_y79a.c - KMDF driver entry / device bring-up
=============================================================================
 Target device: ACPI\SEC0048  (child of ACPI\QCOM2511 I2C controller)
=============================================================================
--*/

#include "sec_ts_y79a.h"

//
// Prototypes (can thiet cho ALLOC_PRAGMA va thu tu bien dich)
//
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD SecTsEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE SecTsEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE SecTsEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY SecTsEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT SecTsEvtDeviceD0Exit;

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, SecTsEvtDeviceAdd)
#pragma alloc_text (PAGE, SecTsEvtPrepareHardware)
#endif

//
// Open the parent SPB target using the resource hub path derived from the
// I2C connection resource in our _CRS, and verify we got an I2C resource.
//
static NTSTATUS
SecTsOpenI2CTarget(
    _In_ PSEC_TS_DEVICE Dev,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    NTSTATUS                    status;
    ULONG                       count;
    ULONG                       i;
    ULONG                       i2cCount = 0;
    LARGE_INTEGER               connId = {0};
    DECLARE_UNICODE_STRING_SIZE(deviceName, RESOURCE_HUB_PATH_SIZE);
    WDF_IO_TARGET_OPEN_PARAMS   openParams;

    count = WdfCmResourceListGetCount(ResourcesTranslated);

    for (i = 0; i < count; i++) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR desc =
            WdfCmResourceListGetDescriptor(ResourcesTranslated, i);

        if (desc == NULL) {
            continue;
        }

        if (desc->Type == CmResourceTypeConnection) {
            if ((desc->u.Connection.Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL) &&
                (desc->u.Connection.Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)) {
                connId.LowPart  = desc->u.Connection.IdLowPart;
                connId.HighPart = desc->u.Connection.IdHighPart;
                i2cCount++;
            }
        }
    }

    if (i2cCount != 1) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    status = WdfIoTargetCreate(Dev->Device, WDF_NO_OBJECT_ATTRIBUTES, &Dev->I2CTarget);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    //
    // Khong dung RtlStringCbPrintfW: no keo __stdio_common_vswprintf (CRT user-mode)
    // vao driver kernel. Tu ghep chuoi: "\??\RESOURCE_HUB\<16 hex>".
    //
    {
        static const WCHAR prefix[] = L"\\??\\" RESOURCE_HUB_DEVICE_NAME L"\\";
        ULONG  i;
        ULONG  pos = 0;
        ULONG  nibbles = (ULONG)(sizeof(LARGE_INTEGER) * 2);   // 16
        ULONGLONG value = (ULONGLONG)connId.QuadPart;

        for (i = 0; prefix[i] != L'\0'; i++) {
            if (pos + 1 >= RESOURCE_HUB_PATH_SIZE / sizeof(WCHAR)) {
                return STATUS_BUFFER_TOO_SMALL;
            }
            deviceName.Buffer[pos++] = prefix[i];
        }

        // hex, khong zero-pad (giong %I64x)
        {
            WCHAR  tmp[17];
            ULONG  n = 0;
            BOOLEAN started = FALSE;

            for (i = 0; i < nibbles; i++) {
                ULONG shift = (nibbles - 1 - i) * 4;
                ULONG nib   = (ULONG)((value >> shift) & 0xF);

                if ((nib != 0) || started || (i == nibbles - 1)) {
                    started = TRUE;
                    tmp[n++] = (WCHAR)((nib < 10) ? (L'0' + nib) : (L'a' + (nib - 10)));
                }
            }
            for (i = 0; i < n; i++) {
                if (pos + 1 >= RESOURCE_HUB_PATH_SIZE / sizeof(WCHAR)) {
                    return STATUS_BUFFER_TOO_SMALL;
                }
                deviceName.Buffer[pos++] = tmp[i];
            }
        }

        deviceName.Buffer[pos] = L'\0';
        deviceName.Length = (USHORT)(pos * sizeof(WCHAR));
        deviceName.MaximumLength = RESOURCE_HUB_PATH_SIZE;
    }

    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&openParams, &deviceName, FILE_ALL_ACCESS);

    status = WdfIoTargetOpen(Dev->I2CTarget, &openParams);
    return status;
}

NTSTATUS
SecTsEvtPrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PSEC_TS_DEVICE dev = SecTsGetDeviceContext(Device);
    NTSTATUS       status;

    PAGED_CODE();

    status = SecTsOpenI2CTarget(dev, ResourcesRaw, ResourcesTranslated);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = SecTsChipInit(dev);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    dev->HardwareReady = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
SecTsEvtReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PSEC_TS_DEVICE dev = SecTsGetDeviceContext(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    dev->HardwareReady = FALSE;
    SecTsChipReleaseAll(dev);

    return STATUS_SUCCESS;
}

NTSTATUS
SecTsEvtDeviceD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PSEC_TS_DEVICE dev = SecTsGetDeviceContext(Device);

    UNREFERENCED_PARAMETER(PreviousState);

    // re-arm the chip every time we come back to D0
    (VOID)SecTsChipInit(dev);
    SecTsPollStart(dev);

    return STATUS_SUCCESS;
}

NTSTATUS
SecTsEvtDeviceD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
    )
{
    PSEC_TS_DEVICE dev = SecTsGetDeviceContext(Device);

    UNREFERENCED_PARAMETER(TargetState);

    SecTsPollStop(dev);
    SecTsChipReleaseAll(dev);

    return STATUS_SUCCESS;
}

NTSTATUS
SecTsEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS                status;
    WDFDEVICE               device;
    PSEC_TS_DEVICE          dev;
    WDF_OBJECT_ATTRIBUTES   attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
    UNREFERENCED_PARAMETER(Driver);
    PAGED_CODE();

    //
    // Cac buoc khoi tao BAT BUOC cho VHF (VhfCreate yeu cau FDO that su):
    //   - DeviceType = FILE_DEVICE_UNKNOWN
    //   - Exclusive  = FALSE (VHF/HID can mo nhieu handle)
    //   - IoType     = BufferedIO
    // Thieu cac buoc nay -> VhfCreate tra ve 0xc0000010 (STATUS_INVALID_DEVICE_REQUEST)
    //
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetExclusive(DeviceInit, FALSE);
    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoBuffered);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
    pnpCallbacks.EvtDevicePrepareHardware = SecTsEvtPrepareHardware;
    pnpCallbacks.EvtDeviceReleaseHardware = SecTsEvtReleaseHardware;
    pnpCallbacks.EvtDeviceD0Entry         = SecTsEvtDeviceD0Entry;
    pnpCallbacks.EvtDeviceD0Exit          = SecTsEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, SEC_TS_DEVICE);

    SecTsTrace("EvtDeviceAdd: bat dau");

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        SecTsTrace("WdfDeviceCreate FAIL status=%x", status);
        return status;
    }
    SecTsTrace("WdfDeviceCreate OK");

    dev = SecTsGetDeviceContext(device);
    RtlZeroMemory(dev, sizeof(SEC_TS_DEVICE));
    dev->Device = device;

    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &dev->I2CLock);
    if (!NT_SUCCESS(status)) {
        SecTsTrace("WdfWaitLockCreate FAIL status=%x", status);
        return status;
    }
    SecTsTrace("WdfWaitLockCreate OK");

    status = SecTsHidCreate(dev);
    if (!NT_SUCCESS(status)) {
        SecTsTrace("SecTsHidCreate FAIL status=%x", status);
        return status;
    }
    SecTsTrace("SecTsHidCreate OK -> EvtDeviceAdd thanh cong");

    return STATUS_SUCCESS;
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS        status;
    WDF_DRIVER_CONFIG config;

    WDF_DRIVER_CONFIG_INIT(&config, SecTsEvtDeviceAdd);
    config.EvtDriverUnload = NULL;

    status = WdfDriverCreate(DriverObject,
                             RegistryPath,
                             WDF_NO_OBJECT_ATTRIBUTES,
                             &config,
                             WDF_NO_HANDLE);
    return status;
}

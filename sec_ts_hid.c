/*++
=============================================================================
 sec_ts_hid.c - VHF (Virtual HID Framework) digitizer for 10 fingers
=============================================================================
 Report descriptor: HID Digitizer / Touch Screen, 10 contacts, X/Y 0..4095,
 report id 1, plus a Contact Count field.
=============================================================================
--*/

#include "sec_ts_y79a.h"

//
// HID report descriptor
//   Usage Page Digitizer (0x0D), Usage Touch Screen (0x04)
//   Report ID 1
//   Contact Count (0x0D/0x54)
//   10 x Logical Collection (0x0D/0x55):
//        Tip Switch (0x0D/0x42)
//        Contact Identifier (0x0D/0x51)
//        Confidence (0x0D/0x47)
//        X (0x01/0x30), Y (0x01/0x31) logical 0..4095
//
static const UCHAR g_SecTsReportDescriptor[] = {
    0x05, 0x0D,                     // Usage Page (Digitizer)
    0x09, 0x04,                     // Usage (Touch Screen)
    0xA1, 0x01,                     // Collection (Application)
    0x85, 0x01,                     //   Report ID (1)

    // Contact Count
    0x09, 0x54,                     //   Usage (Contact Count)
    0x15, 0x00,                     //   Logical Minimum (0)
    0x25, 0x0A,                     //   Logical Maximum (10)
    0x75, 0x08,                     //   Report Size (8)
    0x95, 0x01,                     //   Report Count (1)
    0x81, 0x02,                     //   Input (Data,Var,Abs)

    // 10 finger collections
    0x09, 0x55,                     //   Usage (Contact Count Maximum)? -> use Contact Identifier group
    0x09, 0x55,                     //   (kept for compatibility)
    0x05, 0x0D,                     //   Usage Page (Digitizer)
    0x09, 0x55,                     //   Usage (Contact Identifier group)
    0x15, 0x00,                     //   Logical Minimum (0)
    0x25, 0x0A,                     //   Logical Maximum (10)
    0x75, 0x08,                     //   Report Size (8)
    0x95, 0x01,                     //   Report Count (1)
    0xB1, 0x02,                     //   Feature (Data,Var,Abs)

    0x05, 0x0D,                     //   Usage Page (Digitizer)
    0x09, 0x22,                     //   Usage (Finger)
    0xA1, 0x02,                     //   Collection (Logical)
    0x09, 0x42,                     //     Usage (Tip Switch)
    0x15, 0x00,                     //     Logical Minimum (0)
    0x25, 0x01,                     //     Logical Maximum (1)
    0x75, 0x01,                     //     Report Size (1)
    0x95, 0x01,                     //     Report Count (1)
    0x81, 0x02,                     //     Input (Data,Var,Abs)
    0x09, 0x47,                     //     Usage (Confidence)
    0x81, 0x02,                     //     Input (Data,Var,Abs)
    0x09, 0x51,                     //     Usage (Contact Identifier)
    0x15, 0x00,                     //     Logical Minimum (0)
    0x25, 0x09,                     //     Logical Maximum (9)
    0x75, 0x08,                     //     Report Size (8)
    0x95, 0x01,                     //     Report Count (1)
    0x81, 0x02,                     //     Input (Data,Var,Abs)
    0x05, 0x01,                     //     Usage Page (Generic Desktop)
    0x09, 0x30,                     //     Usage (X)
    0x09, 0x31,                     //     Usage (Y)
    0x15, 0x00,                     //     Logical Minimum (0)
    0x26, 0xFF, 0x0F,               //     Logical Maximum (4095)
    0x75, 0x10,                     //     Report Size (16)
    0x95, 0x02,                     //     Report Count (2)
    0x81, 0x02,                     //     Input (Data,Var,Abs)
    0xC0,                           //   End Collection
    // repeat the finger collection 9 more times
#define SEC_TS_FINGER_COLLECTION \
    0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02, \
    0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02, \
    0x09, 0x47, 0x81, 0x02, \
    0x09, 0x51, 0x15, 0x00, 0x25, 0x09, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02, \
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, \
    0x15, 0x00, 0x26, 0xFF, 0x0F, 0x75, 0x10, 0x95, 0x02, 0x81, 0x02, \
    0xC0,
    SEC_TS_FINGER_COLLECTION
    SEC_TS_FINGER_COLLECTION
    SEC_TS_FINGER_COLLECTION
    SEC_TS_FINGER_COLLECTION
    SEC_TS_FINGER_COLLECTION
    SEC_TS_FINGER_COLLECTION
    SEC_TS_FINGER_COLLECTION
    SEC_TS_FINGER_COLLECTION
    SEC_TS_FINGER_COLLECTION

    0xC0                            // End Collection (Application)
};

static NTSTATUS
SecTsVhfReadyForNextReadReport(
    _In_ PVOID VhfClientContext
    )
{
    PSEC_TS_DEVICE dev = (PSEC_TS_DEVICE)VhfClientContext;
    dev->VhfReady = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
SecTsHidCreate(
    _In_ PSEC_TS_DEVICE Dev
    )
{
    NTSTATUS    status;
    VHF_CONFIG  vhfConfig;

    // don ban copy cu neu co (truong hop EvtDeviceAdd goi lai)
    if (Dev->DescCopy != NULL) {
        ExFreePoolWithTag(Dev->DescCopy, SEC_TS_POOL_TAG);
        Dev->DescCopy = NULL;
    }

    Dev->ReportBuffer = (PSEC_TS_HID_REPORT)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                                            sizeof(SEC_TS_HID_REPORT),
                                                            SEC_TS_POOL_TAG);
    if (Dev->ReportBuffer == NULL) {
        SecTsTrace("HidCreate: ExAllocatePool2 FAIL");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Dev->ReportBuffer, sizeof(SEC_TS_HID_REPORT));
    Dev->ReportBuffer->ReportId = 1;

    VHF_CONFIG_INIT(&vhfConfig,
                    WdfDeviceWdmGetDeviceObject(Dev->Device),
                    sizeof(g_SecTsReportDescriptor),
                    (PUCHAR)g_SecTsReportDescriptor);

    vhfConfig.VhfClientContext          = Dev;
    vhfConfig.EvtVhfReadyForNextReadReport = SecTsVhfReadyForNextReadReport;

    // Chan doan: in ra cac tham so truoc khi goi VhfCreate
    SecTsTrace("HidCreate: DeviceObject=%x  DescLen=%d  DescPtr=%x",
               (ULONG)(ULONG_PTR)vhfConfig.DeviceObject,
               (ULONG)vhfConfig.ReportDescriptorLength,
               (ULONG)(ULONG_PTR)vhfConfig.ReportDescriptor);
    SecTsTrace("HidCreate: ClientContext=%x  EvtReady=%x",
               (ULONG)(ULONG_PTR)vhfConfig.VhfClientContext,
               (ULONG)(ULONG_PTR)vhfConfig.EvtVhfReadyForNextReadReport);

    SecTsTrace("HidCreate: goi VhfCreate");
    status = VhfCreate(&vhfConfig, &Dev->VhfHandle);
    if (!NT_SUCCESS(status)) {
        SecTsTrace("VhfCreate FAIL status=%x", status);

        //
        // Thu lai voi report descriptor copy sang non-paged pool
        // (VHF co the yeu cau descriptor o vung non-paged)
        //
        {
            PUCHAR descCopy = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                                      sizeof(g_SecTsReportDescriptor),
                                                      SEC_TS_POOL_TAG);
            if (descCopy != NULL) {
                RtlCopyMemory(descCopy, g_SecTsReportDescriptor, sizeof(g_SecTsReportDescriptor));
                VHF_CONFIG_INIT(&vhfConfig,
                                WdfDeviceWdmGetDeviceObject(Dev->Device),
                                sizeof(g_SecTsReportDescriptor),
                                descCopy);
                vhfConfig.VhfClientContext            = Dev;
                vhfConfig.EvtVhfReadyForNextReadReport = SecTsVhfReadyForNextReadReport;
                status = VhfCreate(&vhfConfig, &Dev->VhfHandle);
                SecTsTrace("VhfCreate (non-paged desc) status=%x", status);
                if (!NT_SUCCESS(status)) {
                    ExFreePoolWithTag(descCopy, SEC_TS_POOL_TAG);
                    return status;
                }
                Dev->DescCopy = descCopy;
            } else {
                return status;
            }
        }
    }
    SecTsTrace("VhfCreate OK");

    status = VhfStart(Dev->VhfHandle);
    if (!NT_SUCCESS(status)) {
        SecTsTrace("VhfStart FAIL status=%x", status);
        VhfDelete(Dev->VhfHandle, TRUE);
        Dev->VhfHandle = NULL;
        return status;
    }
    SecTsTrace("VhfStart OK");

    Dev->VhfStarted = TRUE;
    Dev->VhfReady   = TRUE;     // first report may be submitted immediately
    return STATUS_SUCCESS;
}

VOID
SecTsHidDestroy(
    _In_ PSEC_TS_DEVICE Dev
    )
{
    if (Dev->VhfHandle != NULL) {
        VhfDelete(Dev->VhfHandle, TRUE);
        Dev->VhfHandle = NULL;
        Dev->VhfStarted = FALSE;
    }
    if (Dev->ReportBuffer != NULL) {
        ExFreePoolWithTag(Dev->ReportBuffer, SEC_TS_POOL_TAG);
        Dev->ReportBuffer = NULL;
    }
}

//
// Build a report from the finger table and submit it to VHF.
//
VOID
SecTsHidSubmitReport(
    _In_ PSEC_TS_DEVICE Dev
    )
{
    PSEC_TS_HID_REPORT report;
    UCHAR              count = 0;
    ULONG              i;

    if (!Dev->VhfStarted || (Dev->ReportBuffer == NULL)) {
        return;
    }

    // VHF requires the previous read report to be consumed by the host
    if (!Dev->VhfReady) {
        return;
    }

    report = Dev->ReportBuffer;
    RtlZeroMemory(report, sizeof(SEC_TS_HID_REPORT));
    report->ReportId = 1;

    for (i = 0; i < SEC_TS_MAX_FINGERS; i++) {
        if (Dev->Fingers[i].Down) {
            report->Contact[count].TipSwitch = 1;
            report->Contact[count].ContactId = (UCHAR)i;
            report->Contact[count].Confidence = 1;
            report->Contact[count].X = Dev->Fingers[i].X;
            report->Contact[count].Y = Dev->Fingers[i].Y;
            count++;
        }
    }
    report->ContactCount = count;

    // submit as a HID input report
    {
        HID_XFER_PACKET packet;
        NTSTATUS        status;

        packet.reportBuffer    = (PUCHAR)report;
        packet.reportBufferLen = sizeof(SEC_TS_HID_REPORT);
        packet.reportId        = 1;

        Dev->VhfReady = FALSE;
        status = VhfReadReportSubmit(Dev->VhfHandle, &packet);
        if (!NT_SUCCESS(status)) {
            Dev->VhfReady = TRUE;   // allow retry on next tick
        }
    }
}
/*++
=============================================================================
 sec_ts_poll.c - periodic polling of the touch controller
=============================================================================
 We deliberately do NOT use the GPIO interrupt:
   - the TLMM GPIO node (GIO0) has no working driver on this port
     (qcgpio fails to start with 0xc0000034), and the inbox GPIO driver
     (QCOMFFEB) causes WDF_VIOLATION.
 Polling every 4 ms gives ~250 Hz sampling, which is enough for smooth
 touch and matches the chip's own 120 Hz report rate with headroom.
=============================================================================
--*/

#include "sec_ts_y79a.h"

#define SEC_TS_POLL_INTERVAL_MS 4

static VOID
SecTsEvtPollTimer(
    _In_ WDFTIMER Timer
    )
{
    WDFDEVICE       device = WdfTimerGetParentObject(Timer);
    PSEC_TS_DEVICE  dev    = SecTsGetDeviceContext(device);

    if (dev->PollingActive != 1) {
        return;
    }

    if (!dev->HardwareReady) {
        return;
    }

    if (!NT_SUCCESS(SecTsChipReadEvents(dev))) {
        // chip may have reset; try one re-init, then keep going
        dev->ReinitCount++;
        if (dev->ReinitCount < 100) {
            (VOID)SecTsChipInit(dev);
        }
        return;
    }

    // always submit: reports with 0 contacts are needed to release fingers
    SecTsHidSubmitReport(dev);
}

NTSTATUS
SecTsPollStart(
    _In_ PSEC_TS_DEVICE Dev
    )
{
    NTSTATUS                status;
    WDF_TIMER_CONFIG        timerConfig;
    WDF_OBJECT_ATTRIBUTES   timerAttributes;

    WDF_TIMER_CONFIG_INIT_PERIODIC(&timerConfig,
                                   SecTsEvtPollTimer,
                                   SEC_TS_POLL_INTERVAL_MS);

    WDF_OBJECT_ATTRIBUTES_INIT(&timerAttributes);
    timerAttributes.ParentObject = Dev->Device;

    status = WdfTimerCreate(&timerConfig, &timerAttributes, &Dev->PollTimer);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    Dev->PollingActive = 1;
    WdfTimerStart(Dev->PollTimer, WDF_REL_TIMEOUT_IN_MS(SEC_TS_POLL_INTERVAL_MS));
    return STATUS_SUCCESS;
}

VOID
SecTsPollStop(
    _In_ PSEC_TS_DEVICE Dev
    )
{
    Dev->PollingActive = 0;
    if (Dev->PollTimer != NULL) {
        WdfTimerStop(Dev->PollTimer, TRUE);
    }
}
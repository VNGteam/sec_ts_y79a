/*++
=============================================================================
 sec_ts_trace.c - ghi trace ra file tu kernel driver

 Muc dich: khi chua co cam ung/chuot, khong the doc DbgPrint/DebugView.
 Ghi tung buoc cua driver ra C:\y2q_try\driver_trace.txt de chan doan.

 KHONG dung CRT (_vsnprintf) vi se keo __stdio_common_vswprintf vao kernel.
 Tu format: %s (chuoi), %x / %08x (hex), %d (thap phan).
=============================================================================
--*/

#include "sec_ts_y79a.h"

#define SEC_TS_TRACE_PATH   L"\\??\\C:\\y2q_try\\driver_trace.txt"
#define SEC_TS_TRACE_MAX    600

static
VOID
SecTsTraceAppendStr(
    _Inout_ PCHAR  Buf,
    _Inout_ PULONG Pos,
    _In_    ULONG  Max,
    _In_    PCSTR  Str
    )
{
    while ((*Str != '\0') && (*Pos < (Max - 1))) {
        Buf[(*Pos)++] = *Str++;
    }
}

static
VOID
SecTsTraceAppendHex(
    _Inout_ PCHAR   Buf,
    _Inout_ PULONG  Pos,
    _In_    ULONG   Max,
    _In_    ULONG   Value
    )
{
    static const CHAR hex[] = "0123456789abcdef";
    INT shift;
    BOOLEAN started = FALSE;

    for (shift = 28; shift >= 0; shift -= 4) {
        ULONG nib = (Value >> shift) & 0xF;
        if ((nib != 0) || started || (shift == 0)) {
            started = TRUE;
            if (*Pos < (Max - 1)) {
                Buf[(*Pos)++] = hex[nib];
            }
        }
    }
}

static
VOID
SecTsTraceAppendDec(
    _Inout_ PCHAR   Buf,
    _Inout_ PULONG  Pos,
    _In_    ULONG   Max,
    _In_    ULONG   Value
    )
{
    CHAR tmp[12];
    ULONG n = 0;

    if (Value == 0) {
        if (*Pos < (Max - 1)) { Buf[(*Pos)++] = '0'; }
        return;
    }
    while ((Value > 0) && (n < sizeof(tmp))) {
        tmp[n++] = (CHAR)('0' + (Value % 10));
        Value /= 10;
    }
    while (n > 0) {
        if (*Pos < (Max - 1)) { Buf[(*Pos)++] = tmp[--n]; }
        else { n--; }
    }
}

//
// Ghi mot dong trace. An toan: moi loi deu bi bo qua.
//
VOID
SecTsTrace(
    _In_ PCSTR Format,
    ...
    )
{
    NTSTATUS            status;
    OBJECT_ATTRIBUTES   oa;
    UNICODE_STRING      path;
    IO_STATUS_BLOCK     iosb;
    HANDLE              hFile = NULL;
    CHAR                line[SEC_TS_TRACE_MAX];
    ULONG               pos = 0;
    LARGE_INTEGER       offset;
    va_list             args;
    PCSTR               p;

    SecTsTraceAppendStr(line, &pos, sizeof(line), "[Y79A] ");

    va_start(args, Format);
    for (p = Format; (*p != '\0') && (pos < (sizeof(line) - 24)); p++) {
        if (*p != '%') {
            line[pos++] = *p;
            continue;
        }
        p++;
        switch (*p) {
        case 's': {
            PCSTR s = va_arg(args, PCSTR);
            SecTsTraceAppendStr(line, &pos, sizeof(line), (s != NULL) ? s : "(null)");
            break;
        }
        case 'x': {
            ULONG v = va_arg(args, ULONG);
            SecTsTraceAppendHex(line, &pos, sizeof(line), v);
            break;
        }
        case 'd': {
            ULONG v = va_arg(args, ULONG);
            SecTsTraceAppendDec(line, &pos, sizeof(line), v);
            break;
        }
        case '%': {
            line[pos++] = '%';
            break;
        }
        default: {
            line[pos++] = '%';
            if (*p != '\0') { line[pos++] = *p; }
            break;
        }
        }
    }
    va_end(args);

    line[pos++] = '\r';
    line[pos++] = '\n';

    RtlInitUnicodeString(&path, SEC_TS_TRACE_PATH);
    InitializeObjectAttributes(&oa, &path,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL, NULL);

    status = ZwCreateFile(&hFile,
                          FILE_APPEND_DATA | SYNCHRONIZE,
                          &oa,
                          &iosb,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          0,
                          FILE_OPEN_IF,
                          FILE_SYNCHRONOUS_IO_NONALERT,
                          NULL, 0);
    if (!NT_SUCCESS(status)) {
        return;
    }

    offset.QuadPart = 0;
    ZwWriteFile(hFile, NULL, NULL, NULL, &iosb,
                line, pos, &offset, NULL);

    ZwClose(hFile);
}
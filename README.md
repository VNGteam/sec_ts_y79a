# sec_ts_y79a — Samsung Y79A touch driver for Windows 11 ARM64

Driver KMDF cho chip cảm ứng **Samsung Y79A (`sec_ts`)** trên Samsung Galaxy S20+ 5G
(bo mạch `y2q`, Snapdragon 865), chạy Windows 11 ARM64 native.

## Phần cứng (đã xác minh trên máy thật)

| Mục | Giá trị |
|---|---|
| ACPI device | `ACPI\SEC0048` (con của `ACPI\QCOM2511`) |
| Bus | QUPv3 SE5 I2C `0x00994000`, slave `0x48`, 400 kHz |
| Ngắt | TLMM GPIO15 mức thấp — **không dùng**, driver poll mỗi 4 ms |
| Nguồn | `s2dos05-ldo2` (1.8 V) / `ldo3` (3.0 V), bootloader bật sẵn |
| Reset | không có chân reset |

## Giao thức (từ I2C trace + source `sec_ts.h`)

- Event 16 byte. Đọc `[0x60]` (1 event); nếu `byte7 & 0x1F = N > 0` thì `[0x61]` đọc `N×16`.
- `X = (b1<<4) | (b3>>4)`, `Y = (b2<<4) | (b3&0xF)`
- `id = (b0>>2)&0xF`, `action = b0>>6` (1 press, 2 move, 3 release)
- Khởi tạo: `[0x62]` clear → chờ event boot (`09 00 10…`) → `[0x30]=0x0061` → `[0xE4]=0x01` → `[0x10]` SENSE_ON → `[0x62]`

## Kiến trúc driver

| File | Nhiệm vụ |
|---|---|
| `sec_ts_y79a.c` | DriverEntry, EvtDeviceAdd, mở SPB target qua Resource Hub |
| `sec_ts_hw.c` | I2C transport: `IOCTL_SPB_EXECUTE_SEQUENCE` với `SPB_TRANSFER_LIST` |
| `sec_ts_chip.c` | Khởi tạo chip + giải mã event |
| `sec_ts_hid.c` | VHF: HID Digitizer / Touch Screen, 10 ngón, X/Y 0..4095 |
| `sec_ts_poll.c` | Timer poll 4 ms (~250 Hz) |

**Vì sao poll thay vì ngắt:** node `GIO0` (TLMM GPIO) trên port này không có driver hoạt động —
`qcgpio.sys` fail với `0xc0000034`, còn driver inbox `qcgpio_i.sys` gây `WDF_VIOLATION (0x10D)`.
Poll 4 ms đủ mượt và loại bỏ hoàn toàn phụ thuộc GPIO.

## Build

Dùng **GitHub Actions** (`.github/workflows/build.yml`) — runner `windows-2022` có VS2022 + SDK,
cài thêm WDK qua `winget`, rồi build bằng **MSBuild** với PlatformToolset
`WindowsKernelModeDriver10.0` (cách chuẩn, MSBuild tự thêm mọi runtime lib cần thiết).

Push lên `main` hoặc bấm **Run workflow** → tải artifact `sec_ts_y79a-driver`.

## Cài đặt

Yêu cầu: `testsigning Yes` + cert tin cậy trong `Root` và `TrustedPublisher`.

```
pnputil /add-driver sec_ts_y79a.inf /install
```

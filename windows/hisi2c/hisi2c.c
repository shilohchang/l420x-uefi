/*
 * hisi2c.c - HiSilicon I2C master ("i2c_v400", Kirin 9000C) for Windows, SpbCx controller driver.
 *
 * Register procedure = mainline Linux drivers/i2c/busses/i2c-hisi.c, verified on the L420X
 * from the UEFI shell (KirinI2cXfer / KirinI2cXfer2): both buses returned the HID descriptor.
 *
 * What this driver deliberately does NOT do:
 *   - it does not touch clocks, resets or pin functions (the UEFI hand-off stage leaves the
 *     bus clocks on and the pins in their I2C function);
 *   - standard-mode bus timing is left as found; the fast-mode SCL counters are set to
 *     the values the firmware uses on the keyboard bus, and each transfer selects standard
 *     or fast mode from the speed in the device's ACPI I2cSerialBus resource.
 *
 * Transfers are interrupt driven. A 20 ms timer runs alongside every transfer as a safety
 * net: if the interrupt line turns out to be wired differently the transfer still finishes
 * (slowly), and a stuck transfer is failed after 2 s instead of hanging the system.
 *
 * No debugger on this machine, so the driver writes a few counters to the registry:
 *   HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\KRN0002\<6|7>\Device Parameters
 */
#include <ntddk.h>
#include <wdf.h>
#define RESHUB_USE_HELPER_ROUTINES
#include <reshub.h>
#include <spb\1.1\spbcx.h>
 /* The stub library is added to the project as a file:
    <WDK>\Lib\<version>\km\arm64\spb\1.1\spbcxstubs.lib  (#pragma comment(lib) is ignored in this project) */

#define R_FRAME_CTRL   0x00
#define R_SLV_ADDR     0x04
#define R_CMD_TXDATA   0x08
#define R_RXDATA       0x0C
#define R_FS_SCL_HCNT  0x18
#define R_FS_SCL_LCNT  0x1C
#define R_FIFO_CTRL    0x28
#define R_FIFO_STATE   0x2C
#define R_INT_MSTAT    0x44
#define R_INT_CLR      0x48
#define R_INT_MASK     0x4C
#define R_TRANS_STATE  0x50
#define R_TRANS_ERR    0x54

#define CMD_RW         0x100
#define CMD_STOP       0x200
#define CMD_RESTART    0x400
#define FIFO_RX_EMPTY  0x008
#define FIFO_TX_FULL   0x800
#define INT_CPLT       0x01
#define INT_ERR        0x02
#define INT_FIFO_ERR   0x04
#define INT_RX_FULL    0x08
#define INT_TX_EMPTY   0x10
#define INT_ALL        0x1F
#define FIFO_CTRL_INIT 0xF3C           /* value used by the vendor Linux driver */

    /* Fast-mode SCL counters. These are the values the firmware itself programs on the
       keyboard bus (same 112 MHz controller clock, about 400 kHz). The touchpad bus is never
       used by the firmware and still has reset defaults; the touchpad returns stale data
       at the default ~116 kHz and correct data with these (KirinI2cXfer5). */
#define FS_SCL_HCNT_VALUE 0x50
#define FS_SCL_LCNT_VALUE 0xC0
#define FRAME_SPEED_MASK  0x3
#define FRAME_SPEED_FAST  0x1
#define FRAME_ADDR_10BIT  0x4

#define MAX_SEG        16
#define RX_WINDOW      48              /* read commands in flight; RX FIFO holds 64 */
#define POLL_MS        20
#define TIMEOUT_100NS  (2 * 10 * 1000 * 1000LL)

typedef struct _SEG {
    PUCHAR  Va;
    ULONG   Len;
    BOOLEAN Read;
    BOOLEAN First;                      /* first piece of a transfer */
} SEG;

#define TRACE_MAX 24                   /* the first requests after start are recorded */
#pragma pack(push, 1)
typedef struct _TRACE_ENTRY {          /* 28 bytes */
    UCHAR  Address;
    UCHAR  Segments;
    USHORT WriteLen;
    USHORT ReadLen;
    USHORT ReadGot;
    ULONG  Status;
    UCHAR  W[8];
    UCHAR  R[8];
} TRACE_ENTRY;
#pragma pack(pop)

typedef struct _DEV {
    WDFDEVICE    Device;
    PUCHAR       Base;
    ULONG        Length;
    WDFINTERRUPT Interrupt;
    WDFTIMER     Timer;
    WDFWORKITEM  StatWork;

    /* current transfer - protected by the interrupt lock */
    SPBREQUEST   Request;
    BOOLEAN      Active;
    BOOLEAN      Done;
    NTSTATUS     Status;
    ULONG        Total;
    SEG          Seg[MAX_SEG];
    ULONG        NSeg, TxSeg, TxOff, RxSeg, RxOff;
    ULONG        Outstanding, RxExpected, RxCount;
    ULONGLONG    Start;

    /* counters for the registry */
    ULONG        StReq, StOk, StErr, StTimeout, StIsr, StTimerWork;
    ULONG        StLastErr, StLastTstate, StLastMstat, StLastAddr, StD0Tstate, StD0Frame;
    ULONG        StFixups, StLastFrame;
    ULONG        TraceCount;
    TRACE_ENTRY  Trace[TRACE_MAX];
} DEV;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEV, GetDev)

typedef struct _TGT {
    USHORT Address;
    ULONG  Speed;                       /* Hz, from the ACPI I2cSerialBus resource */
} TGT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(TGT, GetTgt)

static __inline ULONG Rd(DEV* d, ULONG off) { return READ_REGISTER_ULONG((PULONG)(d->Base + off)); }
static __inline VOID  Wr(DEV* d, ULONG off, ULONG v) { WRITE_REGISTER_ULONG((PULONG)(d->Base + off), v); }

static VOID ClearFifos(DEV* d)
{
    ULONG v = Rd(d, R_FIFO_CTRL);
    Wr(d, R_FIFO_CTRL, v | 3);
    Wr(d, R_FIFO_CTRL, v & ~3UL);
}

/* ---- transfer engine: everything below runs with the interrupt lock held ---- */

static VOID Pump(DEV* d)
{
    ULONG guard;

    for (guard = 0; guard < 128; guard++) {
        UCHAR b;
        if (Rd(d, R_FIFO_STATE) & FIFO_RX_EMPTY) {
            break;
        }
        b = (UCHAR)Rd(d, R_RXDATA);
        while (d->RxSeg < d->NSeg && !d->Seg[d->RxSeg].Read) {
            d->RxSeg++;
        }
        if (d->RxSeg < d->NSeg) {
            d->Seg[d->RxSeg].Va[d->RxOff++] = b;
            d->RxCount++;
            if (d->RxOff == d->Seg[d->RxSeg].Len) {
                d->RxSeg++;
                d->RxOff = 0;
            }
        }
        if (d->Outstanding != 0) {
            d->Outstanding--;
        }
    }

    while (d->TxSeg < d->NSeg) {
        SEG* s = &d->Seg[d->TxSeg];
        ULONG cmd = 0;

        if (Rd(d, R_FIFO_STATE) & FIFO_TX_FULL) {
            break;
        }
        if (s->Read && d->Outstanding >= RX_WINDOW) {
            break;
        }
        if (d->TxOff == 0 && s->First && d->TxSeg != 0) {
            cmd |= CMD_RESTART;
        }
        if (d->TxSeg == d->NSeg - 1 && d->TxOff == s->Len - 1) {
            cmd |= CMD_STOP;
        }
        if (s->Read) {
            cmd |= CMD_RW;
            d->Outstanding++;
            d->RxExpected++;
        }
        else {
            cmd |= s->Va[d->TxOff];
        }
        Wr(d, R_CMD_TXDATA, cmd);
        if (++d->TxOff == s->Len) {
            d->TxSeg++;
            d->TxOff = 0;
        }
    }
}

/*
 * Device quirk: Hantick 0911:5288 touchpad (I2C address 0x2C on this machine).
 *
 * Measured on the L420X from the UEFI shell (KirinI2cXfer3/4): once the touchpad has
 * received ANY HID command (even SET_POWER ON), it returns its 501-byte report descriptor
 * with bytes 2..29 replaced by bytes 2..29 of its HID descriptor. Everything else stays
 * correct (byte sums differ by exactly that amount), and nothing short of a power cycle
 * restores it. Windows' hidi2c always sends SET_POWER and RESET before reading the report
 * descriptor, so it always receives the damaged copy and refuses to start the device.
 * (Linux carries workarounds for this same touchpad: a no-irq-after-reset quirk and, on
 * some laptops, a hard-coded report descriptor.)
 *
 * The fix-up below only acts on the exact signature of that damage - a read of register
 * 0x0021 from address 0x2C whose bytes 2..7 are "00 01 F5 01 21 00" (HID descriptor
 * fields) - and puts back the 28 bytes read from the device before any command was sent.
 */
static const UCHAR TpRdescGood2to29[28] = {
    0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01,
    0x29, 0x02, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x81, 0x02, 0x95, 0x06, 0x81, 0x01
};
static const UCHAR TpRdescBad2to7[6] = { 0x00, 0x01, 0xF5, 0x01, 0x21, 0x00 };

static VOID TouchpadDescriptorFixup(DEV* d)
{
    SEG* w, * r;

    if (d->StLastAddr != 0x2C || d->NSeg != 2) {
        return;
    }
    w = &d->Seg[0];
    r = &d->Seg[1];
    if (w->Read || w->Len != 2 || w->Va[0] != 0x21 || w->Va[1] != 0x00 ||
        !r->Read || r->Len < 30 || r->Va[0] != 0x05 || r->Va[1] != 0x01) {
        return;
    }
    if (RtlCompareMemory(r->Va + 2, TpRdescBad2to7, sizeof(TpRdescBad2to7)) != sizeof(TpRdescBad2to7)) {
        return;
    }
    RtlCopyMemory(r->Va + 2, TpRdescGood2to29, sizeof(TpRdescGood2to29));
    d->StFixups++;
}

static VOID Finish(DEV* d, NTSTATUS status)
{
    Wr(d, R_INT_MASK, 0);
    if (NT_SUCCESS(status)) {
        TouchpadDescriptorFixup(d);
    }
    if (d->TraceCount < TRACE_MAX) {
        TRACE_ENTRY* t = &d->Trace[d->TraceCount++];
        ULONG       i, k, w = 0, r = 0;

        RtlZeroMemory(t, sizeof(*t));
        t->Address = (UCHAR)d->StLastAddr;
        t->Segments = (UCHAR)d->NSeg;
        t->ReadGot = (USHORT)d->RxCount;
        t->Status = (ULONG)status;
        for (i = 0; i < d->NSeg; i++) {
            for (k = 0; k < d->Seg[i].Len; k++) {
                if (d->Seg[i].Read) {
                    if (r < 8) { t->R[r] = d->Seg[i].Va[k]; }
                    r++;
                }
                else {
                    if (w < 8) { t->W[w] = d->Seg[i].Va[k]; }
                    w++;
                }
            }
        }
        t->WriteLen = (USHORT)w;
        t->ReadLen = (USHORT)r;
    }
    d->Active = FALSE;
    d->Done = TRUE;
    d->Status = status;
    if (NT_SUCCESS(status)) {
        d->StOk++;
    }
    else {
        d->StErr++;
        d->StLastTstate = Rd(d, R_TRANS_STATE);
    }
}

static VOID Service(DEV* d, ULONG st)
{
    d->StLastMstat = st;
    if (st & (INT_ERR | INT_FIFO_ERR)) {
        d->StLastErr = Rd(d, R_TRANS_ERR);
        Finish(d, STATUS_IO_DEVICE_ERROR);
        ClearFifos(d);
        return;
    }
    Pump(d);
    if ((st & INT_CPLT) && d->TxSeg == d->NSeg) {
        Finish(d, d->RxCount == d->RxExpected ? STATUS_SUCCESS : STATUS_IO_DEVICE_ERROR);
        return;
    }
    Wr(d, R_INT_MASK, INT_CPLT | INT_ERR | INT_FIFO_ERR | INT_RX_FULL |
        (d->TxSeg < d->NSeg ? INT_TX_EMPTY : 0));
}

/* ---- completion (no lock held on entry) ---- */

static VOID CompleteIfDone(DEV* d)
{
    SPBREQUEST r = NULL;
    NTSTATUS   status = STATUS_SUCCESS;
    ULONG      total = 0, ok = 0;

    WdfInterruptAcquireLock(d->Interrupt);
    if (d->Done && d->Request != NULL) {
        r = d->Request;
        d->Request = NULL;
        d->Done = FALSE;
        status = d->Status;
        total = d->Total;
        ok = d->StOk;
    }
    WdfInterruptReleaseLock(d->Interrupt);

    if (r != NULL) {
        if (!NT_SUCCESS(status) || (ok & (ok - 1)) == 0 || d->StReq <= TRACE_MAX + 1) {
            WdfWorkItemEnqueue(d->StatWork);          /* errors, and successes 1,2,4,8,... */
        }
        WdfRequestSetInformation((WDFREQUEST)r, NT_SUCCESS(status) ? total : 0);
        SpbRequestComplete(r, status);
    }
}

static BOOLEAN EvtIsr(WDFINTERRUPT Interrupt, ULONG MessageId)
{
    DEV* d = GetDev(WdfInterruptGetDevice(Interrupt));
    ULONG st;

    UNREFERENCED_PARAMETER(MessageId);
    st = Rd(d, R_INT_MSTAT);
    if (st == 0) {
        return FALSE;
    }
    Wr(d, R_INT_CLR, st);
    d->StIsr++;
    if (!d->Active) {
        Wr(d, R_INT_MASK, 0);
        return TRUE;
    }
    Service(d, st);
    if (d->Done) {
        WdfInterruptQueueDpcForIsr(Interrupt);
    }
    return TRUE;
}

static VOID EvtDpc(WDFINTERRUPT Interrupt, WDFOBJECT Object)
{
    UNREFERENCED_PARAMETER(Object);
    CompleteIfDone(GetDev(WdfInterruptGetDevice(Interrupt)));
}

static VOID EvtTimer(WDFTIMER Timer)
{
    DEV* d = GetDev((WDFDEVICE)WdfTimerGetParentObject(Timer));
    BOOLEAN rearm;

    WdfInterruptAcquireLock(d->Interrupt);
    if (d->Active) {
        ULONG st = Rd(d, R_INT_MSTAT);
        if (st != 0) {                                 /* the interrupt did not get there first */
            Wr(d, R_INT_CLR, st);
            d->StTimerWork++;
            Service(d, st);
        }
        if (d->Active && (LONGLONG)(KeQueryInterruptTime() - d->Start) > TIMEOUT_100NS) {
            d->StTimeout++;
            d->StLastMstat = Rd(d, R_INT_MSTAT);
            Finish(d, STATUS_IO_TIMEOUT);
            ClearFifos(d);
        }
    }
    rearm = d->Active;
    WdfInterruptReleaseLock(d->Interrupt);

    CompleteIfDone(d);
    if (rearm) {
        WdfTimerStart(Timer, WDF_REL_TIMEOUT_IN_MS(POLL_MS));
    }
}

/* ---- registry counters (PASSIVE_LEVEL) ---- */

static VOID PutUlong(WDFKEY key, PCWSTR name, ULONG value)
{
    UNICODE_STRING u;
    RtlInitUnicodeString(&u, name);
    WdfRegistryAssignULong(key, &u, value);
}

static VOID WriteStats(DEV* d)
{
    WDFKEY key;

    if (!NT_SUCCESS(WdfDeviceOpenRegistryKey(d->Device, PLUGPLAY_REGKEY_DEVICE, KEY_WRITE,
        WDF_NO_OBJECT_ATTRIBUTES, &key))) {
        return;
    }
    PutUlong(key, L"D0TransState", d->StD0Tstate);
    PutUlong(key, L"D0FrameCtrl", d->StD0Frame);
    PutUlong(key, L"Requests", d->StReq);
    PutUlong(key, L"Ok", d->StOk);
    PutUlong(key, L"Errors", d->StErr);
    PutUlong(key, L"Timeouts", d->StTimeout);
    PutUlong(key, L"Interrupts", d->StIsr);
    PutUlong(key, L"TimerRescues", d->StTimerWork);
    PutUlong(key, L"LastTransErr", d->StLastErr);
    PutUlong(key, L"LastTransState", d->StLastTstate);
    PutUlong(key, L"LastIntStatus", d->StLastMstat);
    PutUlong(key, L"LastAddress", d->StLastAddr);
    PutUlong(key, L"DescriptorFixups", d->StFixups);
    PutUlong(key, L"LastFrameCtrl", d->StLastFrame);
    {
        UNICODE_STRING u;
        RtlInitUnicodeString(&u, L"Trace");
        WdfRegistryAssignValue(key, &u, REG_BINARY, d->TraceCount * sizeof(TRACE_ENTRY), d->Trace);
    }
    WdfRegistryClose(key);
}

static VOID EvtStatWork(WDFWORKITEM WorkItem)
{
    WriteStats(GetDev((WDFDEVICE)WdfWorkItemGetParentObject(WorkItem)));
}

/* ---- SpbCx callbacks ---- */

static NTSTATUS EvtTargetConnect(WDFDEVICE Controller, SPBTARGET Target)
{
    SPB_CONNECTION_PARAMETERS                       params;
    PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER   rh;
    PUCHAR                                          desc;

    UNREFERENCED_PARAMETER(Controller);
    SPB_CONNECTION_PARAMETERS_INIT(&params);
    SpbTargetGetConnectionParameters(Target, &params);
    rh = (PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER)params.ConnectionParameters;
    if (rh == NULL || rh->PropertiesLength < 18) {
        return STATUS_INVALID_PARAMETER;
    }
    /* ACPI I2cSerialBus descriptor: [0]=0x8E, [5]=bus type (1 = I2C),
       [12..15]=connection speed, [16..17]=slave address */
    desc = rh->ConnectionProperties;
    if (desc[0] != 0x8E || desc[5] != 1) {
        return STATUS_INVALID_PARAMETER;
    }
    GetTgt(Target)->Address = (USHORT)(desc[16] | (desc[17] << 8));
    GetTgt(Target)->Speed = (ULONG)desc[12] | ((ULONG)desc[13] << 8) |
        ((ULONG)desc[14] << 16) | ((ULONG)desc[15] << 24);
    return STATUS_SUCCESS;
}

static VOID StartTransfer(WDFDEVICE Controller, SPBTARGET Target, SPBREQUEST Request, ULONG Count)
{
    DEV* d = GetDev(Controller);
    SEG      seg[MAX_SEG];
    ULONG    n = 0, total = 0, i;
    NTSTATUS status = STATUS_SUCCESS;

    if (Count == 0) {
        status = STATUS_INVALID_PARAMETER;
    }
    for (i = 0; i < Count && NT_SUCCESS(status); i++) {
        SPB_TRANSFER_DESCRIPTOR td;
        PMDL                    mdl = NULL;
        ULONG                   left;
        BOOLEAN                 first = TRUE;

        SPB_TRANSFER_DESCRIPTOR_INIT(&td);
        SpbRequestGetTransferParameters(Request, i, &td, &mdl);
        left = (ULONG)td.TransferLength;
        if (left == 0) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        for (; mdl != NULL && left != 0; mdl = mdl->Next) {
            PUCHAR va = (PUCHAR)MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority | MdlMappingNoExecute);
            ULONG  len = MmGetMdlByteCount(mdl);

            if (va == NULL) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            if (len > left) {
                len = left;
            }
            if (len == 0) {
                continue;
            }
            if (n == MAX_SEG) {
                status = STATUS_NOT_SUPPORTED;
                break;
            }
            seg[n].Va = va;
            seg[n].Len = len;
            seg[n].Read = (BOOLEAN)(td.Direction == SpbTransferDirectionFromDevice);
            seg[n].First = first;
            first = FALSE;
            n++;
            left -= len;
            total += len;
        }
        if (NT_SUCCESS(status) && left != 0) {
            status = STATUS_INVALID_PARAMETER;
        }
    }
    if (!NT_SUCCESS(status)) {
        SpbRequestComplete(Request, status);
        return;
    }

    WdfInterruptAcquireLock(d->Interrupt);
    RtlCopyMemory(d->Seg, seg, n * sizeof(SEG));
    d->NSeg = n;
    d->TxSeg = d->TxOff = d->RxSeg = d->RxOff = 0;
    d->Outstanding = d->RxExpected = d->RxCount = 0;
    d->Total = total;
    d->Request = Request;
    d->Done = FALSE;
    d->Active = TRUE;
    d->Start = KeQueryInterruptTime();
    d->StReq++;
    d->StLastAddr = GetTgt(Target)->Address;

    Wr(d, R_INT_MASK, 0);
    ClearFifos(d);
    Wr(d, R_INT_CLR, INT_ALL);
    {
        /* 7-bit addressing; standard mode up to 100 kHz, fast mode above */
        ULONG frame = Rd(d, R_FRAME_CTRL) & ~(ULONG)(FRAME_SPEED_MASK | FRAME_ADDR_10BIT);
        if (GetTgt(Target)->Speed > 100000) {
            frame |= FRAME_SPEED_FAST;
        }
        Wr(d, R_FRAME_CTRL, frame);
        d->StLastFrame = frame;
    }
    Wr(d, R_SLV_ADDR, GetTgt(Target)->Address);
    Pump(d);                                                    /* first command starts the bus */
    Wr(d, R_INT_MASK, INT_CPLT | INT_ERR | INT_FIFO_ERR | INT_RX_FULL |
        (d->TxSeg < d->NSeg ? INT_TX_EMPTY : 0));
    WdfInterruptReleaseLock(d->Interrupt);

    WdfTimerStart(d->Timer, WDF_REL_TIMEOUT_IN_MS(POLL_MS));
}

static VOID EvtIoRead(WDFDEVICE Controller, SPBTARGET Target, SPBREQUEST Request, size_t Length)
{
    UNREFERENCED_PARAMETER(Length);
    StartTransfer(Controller, Target, Request, 1);
}

static VOID EvtIoWrite(WDFDEVICE Controller, SPBTARGET Target, SPBREQUEST Request, size_t Length)
{
    UNREFERENCED_PARAMETER(Length);
    StartTransfer(Controller, Target, Request, 1);
}

static VOID EvtIoSequence(WDFDEVICE Controller, SPBTARGET Target, SPBREQUEST Request, ULONG TransferCount)
{
    StartTransfer(Controller, Target, Request, TransferCount);
}

/* ---- PnP / power ---- */

static NTSTATUS EvtPrepareHardware(WDFDEVICE Device, WDFCMRESLIST Raw, WDFCMRESLIST Translated)
{
    DEV* d = GetDev(Device);
    ULONG i;

    UNREFERENCED_PARAMETER(Raw);
    for (i = 0; i < WdfCmResourceListGetCount(Translated); i++) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR r = WdfCmResourceListGetDescriptor(Translated, i);
        if (r->Type == CmResourceTypeMemory && d->Base == NULL) {
            d->Length = r->u.Memory.Length;
            d->Base = (PUCHAR)MmMapIoSpaceEx(r->u.Memory.Start, d->Length, PAGE_READWRITE | PAGE_NOCACHE);
        }
    }
    return d->Base != NULL ? STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
}

static NTSTATUS EvtReleaseHardware(WDFDEVICE Device, WDFCMRESLIST Translated)
{
    DEV* d = GetDev(Device);

    UNREFERENCED_PARAMETER(Translated);
    if (d->Base != NULL) {
        MmUnmapIoSpace(d->Base, d->Length);
        d->Base = NULL;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS EvtD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE Previous)
{
    DEV* d = GetDev(Device);

    UNREFERENCED_PARAMETER(Previous);
    Wr(d, R_INT_MASK, 0);
    Wr(d, R_INT_CLR, INT_ALL);
    Wr(d, R_FIFO_CTRL, FIFO_CTRL_INIT | 3);
    Wr(d, R_FIFO_CTRL, FIFO_CTRL_INIT);
    Wr(d, R_FS_SCL_HCNT, FS_SCL_HCNT_VALUE);
    Wr(d, R_FS_SCL_LCNT, FS_SCL_LCNT_VALUE);
    d->StD0Tstate = Rd(d, R_TRANS_STATE);
    d->StD0Frame = Rd(d, R_FRAME_CTRL);
    WriteStats(d);
    return STATUS_SUCCESS;
}

static NTSTATUS EvtD0Exit(WDFDEVICE Device, WDF_POWER_DEVICE_STATE Target)
{
    UNREFERENCED_PARAMETER(Target);
    Wr(GetDev(Device), R_INT_MASK, 0);
    return STATUS_SUCCESS;
}

static NTSTATUS EvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDF_OBJECT_ATTRIBUTES        attr;
    WDF_INTERRUPT_CONFIG         ic;
    WDF_TIMER_CONFIG             tc;
    WDF_WORKITEM_CONFIG          wc;
    SPB_CONTROLLER_CONFIG        spb;
    WDFDEVICE                    device;
    DEV* d;
    NTSTATUS                     status;

    UNREFERENCED_PARAMETER(Driver);

    status = SpbDeviceInitConfig(DeviceInit);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDevicePrepareHardware = EvtPrepareHardware;
    pnp.EvtDeviceReleaseHardware = EvtReleaseHardware;
    pnp.EvtDeviceD0Entry = EvtD0Entry;
    pnp.EvtDeviceD0Exit = EvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnp);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, DEV);
    status = WdfDeviceCreate(&DeviceInit, &attr, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    d = GetDev(device);
    d->Device = device;

    SPB_CONTROLLER_CONFIG_INIT(&spb);
    spb.EvtSpbTargetConnect = EvtTargetConnect;
    spb.EvtSpbIoRead = EvtIoRead;
    spb.EvtSpbIoWrite = EvtIoWrite;
    spb.EvtSpbIoSequence = EvtIoSequence;
    status = SpbDeviceInitialize(device, &spb);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, TGT);
    SpbControllerSetTargetAttributes(device, &attr);

    WDF_INTERRUPT_CONFIG_INIT(&ic, EvtIsr, EvtDpc);
    status = WdfInterruptCreate(device, &ic, WDF_NO_OBJECT_ATTRIBUTES, &d->Interrupt);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_TIMER_CONFIG_INIT(&tc, EvtTimer);
    tc.AutomaticSerialization = FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT(&attr);
    attr.ParentObject = device;
    status = WdfTimerCreate(&tc, &attr, &d->Timer);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_WORKITEM_CONFIG_INIT(&wc, EvtStatWork);
    wc.AutomaticSerialization = FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT(&attr);
    attr.ParentObject = device;
    return WdfWorkItemCreate(&wc, &attr, &d->StatWork);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;

    WDF_DRIVER_CONFIG_INIT(&config, EvtDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

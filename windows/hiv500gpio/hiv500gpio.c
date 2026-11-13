/*
 * hiv500gpio.sys - GPIO class-extension (GpioClx) client driver for the
 * HiSilicon "hiv500" GPIO block of the Kirin 9000C (Huawei L420X).
 *
 * Register layout recovered from the factory Linux kernel (gpio-hiv500) and
 * cross-checked against a UEFI register dump of the block at 0xFB232000:
 *
 *   0x00            output data, pins 16..31   [31:16] write mask, [15:0] value
 *   0x04            output data, pins  0..15   [31:16] write mask, [15:0] value
 *   0x08 + 4*pin    per-pin configuration      [31:16] write mask, [15:0] value
 *                      bit 4      1 = output enabled
 *                      bits 3..0  interrupt trigger:
 *                                   0x0 rising edge   0x2 falling edge   0x6 both edges
 *                                   0x1 level low     0x9 level high
 *   0x88            interrupt mask (1 = masked)
 *   0xA8            interrupt clear (write 1)
 *   0xB0            masked interrupt status
 *   0xDC            pin input level
 *
 * One bank of 32 pins. The block's bus clock is always on on this platform.
 */

#include <ntddk.h>
#include <wdf.h>
#include <gpioclx.h>

#define HIV500_PINS            32
#define HIV500_REG_DATA_HI     0x00
#define HIV500_REG_DATA_LO     0x04
#define HIV500_REG_PINCFG(p)   (0x08 + 4 * (p))
#define HIV500_REG_INT_MASK    0x88
#define HIV500_REG_INT_CLEAR   0xA8
#define HIV500_REG_INT_STATUS  0xB0
#define HIV500_REG_INPUT       0xDC

#define HIV500_CFG_OUTPUT      0x00100010u   /* mask bit 4, set bit 4   */
#define HIV500_CFG_INPUT       0x00100000u   /* mask bit 4, clear bit 4 */
#define HIV500_TRIG_LEVEL_HIGH 0x00090009u
#define HIV500_TRIG_LEVEL_LOW  0x00090001u
#define HIV500_TRIG_RISING     0x00070002u
#define HIV500_TRIG_FALLING    0x00070000u
#define HIV500_TRIG_BOTH       0x00070006u

typedef struct _HIV500_CONTEXT {
    PUCHAR Base;
    ULONG  Length;
} HIV500_CONTEXT, *PHIV500_CONTEXT;

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD Hiv500EvtDeviceAdd;
EVT_WDF_DRIVER_UNLOAD Hiv500EvtDriverUnload;

GPIO_CLIENT_PREPARE_CONTROLLER Hiv500PrepareController;
GPIO_CLIENT_RELEASE_CONTROLLER Hiv500ReleaseController;
GPIO_CLIENT_START_CONTROLLER Hiv500StartController;
GPIO_CLIENT_STOP_CONTROLLER Hiv500StopController;
GPIO_CLIENT_QUERY_CONTROLLER_BASIC_INFORMATION Hiv500QueryControllerBasicInformation;
GPIO_CLIENT_ENABLE_INTERRUPT Hiv500EnableInterrupt;
GPIO_CLIENT_DISABLE_INTERRUPT Hiv500DisableInterrupt;
GPIO_CLIENT_MASK_INTERRUPTS Hiv500MaskInterrupts;
GPIO_CLIENT_UNMASK_INTERRUPT Hiv500UnmaskInterrupt;
GPIO_CLIENT_QUERY_ACTIVE_INTERRUPTS Hiv500QueryActiveInterrupts;
GPIO_CLIENT_CLEAR_ACTIVE_INTERRUPTS Hiv500ClearActiveInterrupts;
GPIO_CLIENT_QUERY_ENABLED_INTERRUPTS Hiv500QueryEnabledInterrupts;
GPIO_CLIENT_RECONFIGURE_INTERRUPT Hiv500ReconfigureInterrupt;
GPIO_CLIENT_CONNECT_IO_PINS Hiv500ConnectIoPins;
GPIO_CLIENT_DISCONNECT_IO_PINS Hiv500DisconnectIoPins;
GPIO_CLIENT_READ_PINS_MASK Hiv500ReadPinsMask;
GPIO_CLIENT_WRITE_PINS_MASK Hiv500WritePinsMask;

static __inline ULONG Hiv500Read(PHIV500_CONTEXT Ctx, ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Ctx->Base + Offset));
}

static __inline VOID Hiv500Write(PHIV500_CONTEXT Ctx, ULONG Offset, ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Ctx->Base + Offset), Value);
}

/* Translate the Windows trigger description into the pin-configuration value. */
static NTSTATUS Hiv500TriggerValue(KINTERRUPT_MODE Mode, KINTERRUPT_POLARITY Polarity, PULONG Value)
{
    if (Mode == LevelSensitive) {
        if (Polarity == InterruptActiveHigh) { *Value = HIV500_TRIG_LEVEL_HIGH; return STATUS_SUCCESS; }
        if (Polarity == InterruptActiveLow)  { *Value = HIV500_TRIG_LEVEL_LOW;  return STATUS_SUCCESS; }
        return STATUS_NOT_SUPPORTED;
    }
    /* Latched (edge). For edge interrupts ActiveHigh = rising, ActiveLow = falling. */
    switch (Polarity) {
    case InterruptActiveHigh: *Value = HIV500_TRIG_RISING;  return STATUS_SUCCESS;
    case InterruptActiveLow:  *Value = HIV500_TRIG_FALLING; return STATUS_SUCCESS;
    case InterruptActiveBoth: *Value = HIV500_TRIG_BOTH;    return STATUS_SUCCESS;
    default:                  return STATUS_NOT_SUPPORTED;
    }
}

/* ------------------------------------------------------------------ driver */

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG Config;
    WDFDRIVER Driver;
    GPIO_CLIENT_REGISTRATION_PACKET Packet;
    NTSTATUS Status;

    WDF_DRIVER_CONFIG_INIT(&Config, Hiv500EvtDeviceAdd);
    Config.EvtDriverUnload = Hiv500EvtDriverUnload;
    Status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, &Driver);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    RtlZeroMemory(&Packet, sizeof(Packet));
    Packet.Version = GPIO_CLIENT_VERSION;
    Packet.Size = sizeof(GPIO_CLIENT_REGISTRATION_PACKET);
    Packet.ControllerContextSize = sizeof(HIV500_CONTEXT);
    Packet.CLIENT_PrepareController = Hiv500PrepareController;
    Packet.CLIENT_ReleaseController = Hiv500ReleaseController;
    Packet.CLIENT_StartController = Hiv500StartController;
    Packet.CLIENT_StopController = Hiv500StopController;
    Packet.CLIENT_QueryControllerBasicInformation = Hiv500QueryControllerBasicInformation;
    Packet.CLIENT_EnableInterrupt = Hiv500EnableInterrupt;
    Packet.CLIENT_DisableInterrupt = Hiv500DisableInterrupt;
    Packet.CLIENT_MaskInterrupts = Hiv500MaskInterrupts;
    Packet.CLIENT_UnmaskInterrupt = Hiv500UnmaskInterrupt;
    Packet.CLIENT_QueryActiveInterrupts = Hiv500QueryActiveInterrupts;
    Packet.CLIENT_ClearActiveInterrupts = Hiv500ClearActiveInterrupts;
    Packet.CLIENT_QueryEnabledInterrupts = Hiv500QueryEnabledInterrupts;
    Packet.CLIENT_ReconfigureInterrupt = Hiv500ReconfigureInterrupt;
    Packet.CLIENT_ConnectIoPins = Hiv500ConnectIoPins;
    Packet.CLIENT_DisconnectIoPins = Hiv500DisconnectIoPins;
    Packet.CLIENT_ReadGpioPinsUsingMask = Hiv500ReadPinsMask;
    Packet.CLIENT_WriteGpioPinsUsingMask = Hiv500WritePinsMask;

    return GPIO_CLX_RegisterClient(Driver, &Packet, RegistryPath);
}

VOID Hiv500EvtDriverUnload(_In_ WDFDRIVER Driver)
{
    GPIO_CLX_UnregisterClient(Driver);
}

NTSTATUS Hiv500EvtDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDFDEVICE Device;
    NTSTATUS Status;

    Status = GPIO_CLX_ProcessAddDevicePreDeviceCreate(Driver, DeviceInit, &Attributes);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Status = WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    return GPIO_CLX_ProcessAddDevicePostDeviceCreate(Driver, Device);
}

/* -------------------------------------------------------------- controller */

_Use_decl_annotations_
NTSTATUS Hiv500PrepareController(WDFDEVICE Device, PVOID Context, WDFCMRESLIST ResourcesRaw, WDFCMRESLIST ResourcesTranslated)
{
    PHIV500_CONTEXT Ctx = (PHIV500_CONTEXT)Context;
    ULONG Count, Index;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc;

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(ResourcesRaw);

    Ctx->Base = NULL;
    Count = WdfCmResourceListGetCount(ResourcesTranslated);
    for (Index = 0; Index < Count; Index++) {
        Desc = WdfCmResourceListGetDescriptor(ResourcesTranslated, Index);
        if (Desc->Type == CmResourceTypeMemory && Ctx->Base == NULL) {
            if (Desc->u.Memory.Length < 0x100) {
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
            Ctx->Length = Desc->u.Memory.Length;
            Ctx->Base = (PUCHAR)MmMapIoSpaceEx(Desc->u.Memory.Start, Ctx->Length, PAGE_READWRITE | PAGE_NOCACHE);
            if (Ctx->Base == NULL) {
                return STATUS_INSUFFICIENT_RESOURCES;
            }
        }
    }
    return (Ctx->Base != NULL) ? STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
}

_Use_decl_annotations_
NTSTATUS Hiv500ReleaseController(WDFDEVICE Device, PVOID Context)
{
    PHIV500_CONTEXT Ctx = (PHIV500_CONTEXT)Context;

    UNREFERENCED_PARAMETER(Device);
    if (Ctx->Base != NULL) {
        MmUnmapIoSpace(Ctx->Base, Ctx->Length);
        Ctx->Base = NULL;
    }
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS Hiv500QueryControllerBasicInformation(PVOID Context, PCLIENT_CONTROLLER_BASIC_INFORMATION Info)
{
    UNREFERENCED_PARAMETER(Context);

    Info->Version = GPIO_CONTROLLER_BASIC_INFORMATION_VERSION;
    Info->Size = sizeof(CLIENT_CONTROLLER_BASIC_INFORMATION);
    Info->TotalPins = HIV500_PINS;
    Info->NumberOfPinsPerBank = HIV500_PINS;
    Info->Flags.MemoryMappedController = TRUE;
    Info->Flags.ActiveInterruptsAutoClearOnRead = FALSE;
    Info->Flags.FormatIoRequestsAsMasks = TRUE;
    Info->Flags.DeviceIdlePowerMgmtSupported = FALSE;
    Info->Flags.EmulateDebouncing = FALSE;
    Info->Flags.EmulateActiveBoth = FALSE;
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS Hiv500StartController(PVOID Context, BOOLEAN RestoreContext, WDF_POWER_DEVICE_STATE PreviousPowerState)
{
    PHIV500_CONTEXT Ctx = (PHIV500_CONTEXT)Context;

    UNREFERENCED_PARAMETER(RestoreContext);
    UNREFERENCED_PARAMETER(PreviousPowerState);

    /* Start with every pin interrupt masked and nothing pending. Pin directions
       and output levels set by the firmware are deliberately left untouched. */
    Hiv500Write(Ctx, HIV500_REG_INT_MASK, 0xFFFFFFFFu);
    Hiv500Write(Ctx, HIV500_REG_INT_CLEAR, 0xFFFFFFFFu);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS Hiv500StopController(PVOID Context, BOOLEAN SaveContext, WDF_POWER_DEVICE_STATE TargetState)
{
    PHIV500_CONTEXT Ctx = (PHIV500_CONTEXT)Context;

    UNREFERENCED_PARAMETER(SaveContext);
    UNREFERENCED_PARAMETER(TargetState);
    Hiv500Write(Ctx, HIV500_REG_INT_MASK, 0xFFFFFFFFu);
    return STATUS_SUCCESS;
}

/* -------------------------------------------------------------- interrupts */

_Use_decl_annotations_
NTSTATUS Hiv500EnableInterrupt(PVOID Context, PGPIO_ENABLE_INTERRUPT_PARAMETERS Parameters)
{
    PHIV500_CONTEXT Ctx = (PHIV500_CONTEXT)Context;
    ULONG Pin = Parameters->PinNumber;
    ULONG Trigger, Config;
    NTSTATUS Status;

    if (Pin >= HIV500_PINS) {
        return STATUS_INVALID_PARAMETER;
    }
    Status = Hiv500TriggerValue(Parameters->InterruptMode, Parameters->Polarity, &Trigger);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Hiv500Write(Ctx, HIV500_REG_PINCFG(Pin), HIV500_CFG_INPUT);
    Config = Hiv500Read(Ctx, HIV500_REG_PINCFG(Pin)) & 0xFFFFFFF0u;
    Hiv500Write(Ctx, HIV500_REG_PINCFG(Pin), Config | Trigger);
    Hiv500Write(Ctx, HIV500_REG_INT_CLEAR, 1u << Pin);
    Hiv500Write(Ctx, HIV500_REG_INT_MASK, Hiv500Read(Ctx, HIV500_REG_INT_MASK) & ~(1u << Pin));
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS Hiv500DisableInterrupt(PVOID Context, PGPIO_DISABLE_INTERRUPT_PARAMETERS Parameters)
{
    PHIV500_CONTEXT Ctx = (PHIV500_CONTEXT)Context;
    ULONG Pin = Parameters->PinNumber;

    if (Pin >= HIV500_PINS) {
        return STATUS_INVALID_PARAMETER;
    }
    Hiv500Write(Ctx, HIV500_REG_INT_MASK, Hiv500Read(Ctx, HIV500_REG_INT_MASK) | (1u << Pin));
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS Hiv500MaskInterrupts(PVOID Context, PGPIO_MASK_INTERRUPT_PARAMETERS Parameters)
{
    PHIV500_CONTEXT Ctx = (PHIV500_CONTEXT)Context;

    Hiv500Write(Ctx, HIV500_REG_INT_MASK, Hiv500Read(Ctx, HIV500_REG_INT_MASK) | (ULONG)Parameters->PinMask);
    Parameters->FailedMask = 0;
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS Hiv500UnmaskInterrupt(PVOID Context, PGPIO_ENABLE_INTERRUPT_PARAMETERS Parameters)
{
    PHIV500_CONTEXT Ctx = (PHIV500_CONTEXT)Context;
    ULONG Pin = Parameters->PinNumber;

    if (Pin >= HIV500_PINS) {
        return STATUS_INVALID_PARAMETER;
    }
    Hiv500Write(Ctx, HIV500_REG_INT_MASK, Hiv500Read(Ctx, HIV500_REG_INT_MASK) & ~(1u << Pin));
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS Hiv500QueryActiveInterrupts(PVOID Context, PGPIO_QUERY_ACTIVE_INTERRUPTS_PARAMETERS Parameters)
{
    PHIV500_CONTEXT Ctx = (PHIV500_CONTEXT)Context;

    Parameters->ActiveMask = (ULONG64)Hiv500Read(Ctx, HIV500_REG_INT_STATUS) & Parameters->EnabledMask;
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS Hiv500ClearActiveInterrupts(PVOID Context, PGPIO_CLEAR_ACTIVE_INTERRUPTS_PARAMETERS Parameters)
{
    PHIV500_CONTEXT Ctx = (PHIV500_CONTEXT)Context;

    Hiv500Write(Ctx, HIV500_REG_INT_CLEAR, (ULONG)Parameters->ClearActiveMask);
    Parameters->FailedClearMask = 0;
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS Hiv500QueryEnabledInterrupts(PVOID Context, PGPIO_QUERY_ENABLED_INTERRUPTS_PARAMETERS Parameters)
{
    PHIV500_CONTEXT Ctx = (PHIV500_CONTEXT)Context;

    Parameters->EnabledMask = (ULONG64)(~Hiv500Read(Ctx, HIV500_REG_INT_MASK));
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS Hiv500ReconfigureInterrupt(PVOID Context, PGPIO_RECONFIGURE_INTERRUPTS_PARAMETERS Parameters)
{
    PHIV500_CONTEXT Ctx = (PHIV500_CONTEXT)Context;
    ULONG Pin = Parameters->PinNumber;
    ULONG Trigger, Config;
    NTSTATUS Status;

    if (Pin >= HIV500_PINS) {
        return STATUS_INVALID_PARAMETER;
    }
    Status = Hiv500TriggerValue(Parameters->InterruptMode, Parameters->Polarity, &Trigger);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }
    Config = Hiv500Read(Ctx, HIV500_REG_PINCFG(Pin)) & 0xFFFFFFF0u;
    Hiv500Write(Ctx, HIV500_REG_PINCFG(Pin), Config | Trigger);
    Hiv500Write(Ctx, HIV500_REG_INT_CLEAR, 1u << Pin);
    return STATUS_SUCCESS;
}

/* ---------------------------------------------------------------- pin I/O */

_Use_decl_annotations_
NTSTATUS Hiv500ConnectIoPins(PVOID Context, PGPIO_CONNECT_IO_PINS_PARAMETERS Parameters)
{
    PHIV500_CONTEXT Ctx = (PHIV500_CONTEXT)Context;
    ULONG Index, Pin;

    for (Index = 0; Index < Parameters->PinCount; Index++) {
        Pin = Parameters->PinNumberTable[Index];
        if (Pin >= HIV500_PINS) {
            return STATUS_INVALID_PARAMETER;
        }
        if (Parameters->ConnectMode == ConnectModeOutput) {
            Hiv500Write(Ctx, HIV500_REG_PINCFG(Pin), HIV500_CFG_OUTPUT);
        } else {
            Hiv500Write(Ctx, HIV500_REG_PINCFG(Pin), HIV500_CFG_INPUT);
        }
    }
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS Hiv500DisconnectIoPins(PVOID Context, PGPIO_DISCONNECT_IO_PINS_PARAMETERS Parameters)
{
    /* Leave the pins as they are: several are power enables set up by firmware. */
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Parameters);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS Hiv500ReadPinsMask(PVOID Context, PGPIO_READ_PINS_MASK_PARAMETERS Parameters)
{
    PHIV500_CONTEXT Ctx = (PHIV500_CONTEXT)Context;

    *Parameters->PinValues = (ULONG64)Hiv500Read(Ctx, HIV500_REG_INPUT);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS Hiv500WritePinsMask(PVOID Context, PGPIO_WRITE_PINS_MASK_PARAMETERS Parameters)
{
    PHIV500_CONTEXT Ctx = (PHIV500_CONTEXT)Context;
    ULONG Set = (ULONG)Parameters->SetMask;
    ULONG Clear = (ULONG)Parameters->ClearMask;
    ULONG Touch = Set | Clear;

    /* Each data register takes a write mask in its upper half, so only the
       requested pins change. Pins 0..15 live at 0x04, pins 16..31 at 0x00. */
    if ((Touch & 0x0000FFFFu) != 0) {
        Hiv500Write(Ctx, HIV500_REG_DATA_LO, ((Touch & 0xFFFFu) << 16) | (Set & 0xFFFFu));
    }
    if ((Touch & 0xFFFF0000u) != 0) {
        Hiv500Write(Ctx, HIV500_REG_DATA_HI, (Touch & 0xFFFF0000u) | ((Set >> 16) & 0xFFFFu));
    }
    return STATUS_SUCCESS;
}

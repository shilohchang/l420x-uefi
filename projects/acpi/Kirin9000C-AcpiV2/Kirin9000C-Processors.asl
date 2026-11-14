DefinitionBlock ("", "DSDT", 2, "KRNEXP", "KR9000C ", 0x00000002)
{
    Scope (\_SB)
    {
        Device (C000)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 0)
        }
        Device (C001)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 1)
        }
        Device (C002)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 2)
        }
        Device (C003)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 3)
        }
        Device (C004)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 4)
        }
        Device (C005)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 5)
        }
        Device (C006)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 6)
        }
        Device (C007)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 7)
        }
        Device (C008)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 8)
        }
        Device (C009)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 9)
        }
        Device (C010)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 10)
        }
        Device (C011)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, 11)
        }
        Device (USB0)
        {
            Name (_HID, "PNP0D10")
            Name (_UID, Zero)
            Name (_CCA, Zero)
            Method (_STA) { Return (0x0F) }
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0xEFC00000, 0x00100000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 172 }
            })
        }
        Device (GPI7)
        {
            Name (_HID, "KRN0001")
            Name (_UID, 0x7)
            Method (_STA) { Return (0x0F) }
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0xFB232000, 0x00001000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Shared) { 610 }
            })
        }
        Device (I2C7)
        {
            Name (_HID, "KRN0002")
            Name (_UID, 0x7)
            Method (_STA) { Return (0x0F) }
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0xFF88F000, 0x00001000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 392 }
            })
        }
        Device (I2C6)
        {
            Name (_HID, "KRN0002")
            Name (_UID, 0x6)
            Method (_STA) { Return (0x0F) }
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0xFF88E000, 0x00001000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 391 }
            })
        }
        Device (KBD0)
        {
            Name (_HID, "KRN0010")
            Name (_CID, "PNP0C50")
            Name (_UID, One)
            Method (_STA) { Return (0x0F) }
            Name (_CRS, ResourceTemplate ()
            {
                I2cSerialBus (0x003A, ControllerInitiated, 400000, AddressingMode7Bit, "\\_SB.I2C7", 0x00, ResourceConsumer, , )
                GpioInt (Edge, ActiveLow, Exclusive, PullUp, 0x0000, "\\_SB.GPI7", 0x00, ResourceConsumer, , ) { 8 }
            })
            Method (_DSM, 4, Serialized)
            {
                If ((Arg0 == ToUUID ("3cdff6f7-4267-4555-ad05-b30a3d8938de")))
                {
                    If ((Arg2 == Zero)) { Return (Buffer (One) { 0x03 }) }
                    If ((Arg2 == One)) { Return (0x0001) }
                }
                Return (Buffer (One) { 0x00 })
            }
        }
        Device (TPD0)
        {
            Name (_HID, "KRN0011")
            Name (_CID, "PNP0C50")
            Name (_UID, One)
            Method (_STA) { Return (0x0F) }
            Name (_CRS, ResourceTemplate ()
            {
                I2cSerialBus (0x002C, ControllerInitiated, 400000, AddressingMode7Bit, "\\_SB.I2C6", 0x00, ResourceConsumer, , )
                GpioInt (Level, ActiveLow, Exclusive, PullUp, 0x0000, "\\_SB.GPI7", 0x00, ResourceConsumer, , ) { 4 }
            })
            Method (_DSM, 4, Serialized)
            {
                If ((Arg0 == ToUUID ("3cdff6f7-4267-4555-ad05-b30a3d8938de")))
                {
                    If ((Arg2 == Zero)) { Return (Buffer (One) { 0x03 }) }
                    If ((Arg2 == One)) { Return (0x0020) }
                }
                Return (Buffer (One) { 0x00 })
            }
        }
    }
}

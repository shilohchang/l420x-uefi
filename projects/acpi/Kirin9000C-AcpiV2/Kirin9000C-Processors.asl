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
    }
}

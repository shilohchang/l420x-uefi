// Experimental diagnostic UFS resource declaration for *custom* device ID.
// NOT a storufs-compatible driver ID, NOT a functional HUFS driver.
// Disabled by default; keep disabled until baseline and IRQ mapping verified.
DefinitionBlock ("", "SSDT", 2, "L42DBG", "UFSPROBE", 0x00000001)
{
    Scope (\_SB)
    {
        Device (UFP0)
        {
            Name (_HID, "L42F000")
            Name (_UID, Zero)
            Name (_DDN, "Kirin9000C HUFS diagnostic only")
            Method (_STA, 0, NotSerialized) { Return (Zero) }
            Name (_CCA, Zero) // Provisional; verify DMA coherency before any real driver!
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0xFA500000, 0x00080000)
                Memory32Fixed (ReadWrite, 0xFA3FD000, 0x00001000)
                Memory32Fixed (ReadWrite, 0xFA400000, 0x00100000)
                Memory32Fixed (ReadWrite, 0xFA58A000, 0x00001000)
                Memory32Fixed (ReadWrite, 0xFB8C3000, 0x00001000)
                // DT GIC SPI n -> candidate ACPI GSI n+32 for GICv3 platform
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {
                    166, 152, 153, 154, 155, 156, 157, 158, 159
                }
            })
        }
    }
}

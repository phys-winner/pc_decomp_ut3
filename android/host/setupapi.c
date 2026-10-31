/* The virtual Windows machine has no AGEIA PPU. Expose a real, owned empty
 * device-information set for PhysX's two hardware interface classes, so its
 * normal enumeration selects software physics. Other PnP queries are outside
 * this host's device model and fail explicitly. */
#include "host.h"

#define SETUPAPI_ "setupapi.dll"
enum { SETUP_DEVICE_SET_ = 0x535055 }; /* Private object kind, never a registry key. */
static const uint8_t ppu_interface_guids[][16] = {
    {0x8C,0xDA,0xCF,0x85,0x31,0x14,0x6F,0x4C,0xAC,0x70,0x12,0x5E,0x6E,0xE8,0x6B,0xC6},
    {0x84,0xA3,0xD2,0x29,0x47,0x2E,0xB5,0x49,0xAE,0xBF,0x69,0x62,0xC2,0x2B,0xD7,0xC2},
};

WINAPI_EXPORT(SETUPAPI_, SetupDiGetClassDevsA, 4)
{
    uint32_t guid = ARG(0), enumerator = ARG(1), flags = ARG(3);
    uint32_t error = 0;
    if ((flags & 1u) && !(flags & 0x10u)) error = 1004; /* ERROR_INVALID_FLAGS */
    else if (!guid && !(flags & 4u)) error = ERROR_INVALID_PARAMETER_;
    else if (flags & 4u) error = ERROR_NOT_SUPPORTED_; /* All device classes. */
    else if (!void_vm_readable(guid, 16)) error = ERROR_INVALID_PARAMETER_;
    else {
        unsigned i;
        for (i = 0; i < sizeof ppu_interface_guids / sizeof *ppu_interface_guids; ++i)
            if (!memcmp(GPTR(guid), ppu_interface_guids[i], 16)) break;
        if (i == sizeof ppu_interface_guids / sizeof *ppu_interface_guids)
            error = ERROR_NOT_SUPPORTED_;
        else if (enumerator) {
            if (!void_vm_readable(enumerator, 1)) error = ERROR_INVALID_PARAMETER_;
            else if (MEM8(enumerator)) error = ERROR_NOT_SUPPORTED_; /* PnP instance filtering. */
        }
    }
    if (error) { void_set_last_error(error); return 0xFFFFFFFFu; }
    uint32_t set = void_handle_new(void_object_new(SETUP_DEVICE_SET_, 0));
    void_set_last_error(0);
    return set;
}

WINAPI_EXPORT(SETUPAPI_, SetupDiEnumDeviceInterfaces, 5)
{
    uint32_t error = ERROR_NO_MORE_ITEMS_;
    if (!void_handle_get(ARG(0), SETUP_DEVICE_SET_)) error = ERROR_INVALID_HANDLE_;
    else if (ARG(1)) error = ERROR_INVALID_PARAMETER_; /* No device can belong to an empty set. */
    /* Windows returns NO_MORE_ITEMS before inspecting the output cbSize or
     * GUID when the set is empty. Do not touch the caller's 28-byte x86
     * SP_DEVICE_INTERFACE_DATA, even if its pointer is NULL or size is wrong. */
    void_set_last_error(error);
    return 0;
}

WINAPI_EXPORT(SETUPAPI_, SetupDiGetDeviceInterfaceDetailA, 6)
{
    /* An empty set cannot have issued a valid interface descriptor. Windows
     * rejects fabricated descriptors without writing RequiredSize or paths. */
    void_set_last_error(void_handle_get(ARG(0), SETUP_DEVICE_SET_) ?
        ERROR_INVALID_PARAMETER_ : ERROR_INVALID_HANDLE_);
    return 0;
}

WINAPI_EXPORT(SETUPAPI_, SetupDiDestroyDeviceInfoList, 1)
{
    if (!void_handle_get(ARG(0), SETUP_DEVICE_SET_) || !void_handle_close(ARG(0))) {
        void_set_last_error(ERROR_INVALID_HANDLE_);
        return 0;
    }
    void_set_last_error(0);
    return 1;
}

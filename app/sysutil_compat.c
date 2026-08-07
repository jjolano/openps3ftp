/*
 * sysutil name bridge for the PS3 app.
 *
 * The portlibs libNoRSX.a is built against PSL1GHT-style headers and
 * references the plain sysUtil* names. ps3dk's libsysutil_stub.a
 * exports the reference-SDK cellSysutil* names instead. Provide the
 * plain names as thin forwards so NoRSX links against ps3dk.
 */
#ifdef OPFTP_PS3

#include <cell/sysutil.h>

/* PSL1GHT sysutilCallback == ps3dk CellSysutilCallback (same ABI). */
typedef void (*psl1ght_sysutil_cb)(uint64_t status, uint64_t param,
                                   void* userdata);

int sysUtilCheckCallback(void)
{
    return cellSysutilCheckCallback();
}

int sysUtilRegisterCallback(int slot, psl1ght_sysutil_cb cb, void* userdata)
{
    return cellSysutilRegisterCallback(slot, (CellSysutilCallback) cb,
                                       userdata);
}

int sysUtilUnregisterCallback(int slot)
{
    return cellSysutilUnregisterCallback(slot);
}

#endif /* OPFTP_PS3 */

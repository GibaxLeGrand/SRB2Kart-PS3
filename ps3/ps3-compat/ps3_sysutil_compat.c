/* PSL1GHT-style sysUtil callback forwarders, missing from PS3DK's
 * compat layer (its cell/sysutil.h explicitly notes "No PSL1GHT
 * forwarder" for these). SDL_PSL1GHTevents.c calls the old lowercase
 * names; forward them to PS3DK's cellSysutil* implementations. */

#include <stdint.h>

typedef void (*CellSysutilCallback)(uint64_t status, uint64_t param, void *userdata);

extern int cellSysutilCheckCallback(void);
extern int cellSysutilRegisterCallback(int slot, CellSysutilCallback func, void *userdata);
extern int cellSysutilUnregisterCallback(int slot);

int sysUtilCheckCallback(void)
{
	return cellSysutilCheckCallback();
}

int sysUtilRegisterCallback(int slot, CellSysutilCallback cb, void *usrdata)
{
	return cellSysutilRegisterCallback(slot, cb, usrdata);
}

int sysUtilUnregisterCallback(int slot)
{
	return cellSysutilUnregisterCallback(slot);
}

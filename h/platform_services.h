#pragma once

#include <types.h>

typedef struct sPlatformDisplayMode
{
   int width;
   int height;
   int bit_depth;
} sPlatformDisplayMode;

typedef enum ePlatformDisplayScope
{
   kPlatformCurrentDisplay,
   kPlatformAllDisplays
} ePlatformDisplayScope;

typedef struct sPlatformMemoryStatus
{
   uint32 load_percent;
   uint32 total_physical_bytes;
   uint32 available_physical_bytes;
   uint32 total_page_file_bytes;
   uint32 available_page_file_bytes;
   uint32 total_virtual_bytes;
   uint32 available_virtual_bytes;
} sPlatformMemoryStatus;

EXTERN int PlatformGetDisplayModes(ePlatformDisplayScope scope,
                                   sPlatformDisplayMode *modes,
                                   int capacity,
                                   int *native_width,
                                   int *native_height);
EXTERN BOOL PlatformGetMemoryStatus(sPlatformMemoryStatus *status);
EXTERN BOOL PlatformReadUserUInt(const char *store, const char *name,
                                 uint32 *value);
EXTERN BOOL PlatformWriteUserUInt(const char *store, const char *name,
                                  uint32 value);
EXTERN const char *PlatformGetLanguageCode(void);
EXTERN void PlatformShowError(const char *title, const char *message);
EXTERN int PlatformPopupMenu(const char *const *items, int count,
                             int client_x, int client_y);

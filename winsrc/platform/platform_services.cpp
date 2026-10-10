#include "../win32_platform.h"

#include <platform_services.h>

#include <stdio.h>
#include <string.h>

struct sDisplayModeCollector
{
   sPlatformDisplayMode *modes;
   int capacity;
   int count;
};

static void CollectMonitorModes(HMONITOR monitor, sDisplayModeCollector *collector,
                                int *native_width, int *native_height)
{
   MONITORINFOEXA info = {};
   DEVMODEA mode = {};
   DWORD index;

   info.cbSize = sizeof(info);
   if (!GetMonitorInfoA(monitor, &info))
      return;

   if (native_width)
      *native_width = info.rcMonitor.right - info.rcMonitor.left;
   if (native_height)
      *native_height = info.rcMonitor.bottom - info.rcMonitor.top;

   for (index = 0; collector->count < collector->capacity; ++index)
   {
      int i;
      memset(&mode, 0, sizeof(mode));
      mode.dmSize = sizeof(mode);
      if (!EnumDisplaySettingsExA(info.szDevice, index, &mode, 0))
         break;

      for (i = 0; i < collector->count; ++i)
         if (collector->modes[i].width == (int)mode.dmPelsWidth &&
             collector->modes[i].height == (int)mode.dmPelsHeight &&
             collector->modes[i].bit_depth == (int)mode.dmBitsPerPel)
            break;
      if (i != collector->count)
         continue;

      sPlatformDisplayMode *output = &collector->modes[collector->count++];
      output->width = (int)mode.dmPelsWidth;
      output->height = (int)mode.dmPelsHeight;
      output->bit_depth = (int)mode.dmBitsPerPel;
   }
}

static BOOL CALLBACK CollectAllMonitorModes(HMONITOR monitor, HDC, LPRECT,
                                            LPARAM context)
{
   CollectMonitorModes(monitor, (sDisplayModeCollector *)context, NULL, NULL);
   return TRUE;
}

int PlatformGetDisplayModes(ePlatformDisplayScope scope,
                            sPlatformDisplayMode *modes, int capacity,
                            int *native_width, int *native_height)
{
   sDisplayModeCollector collector = { modes, capacity, 0 };

   if (!modes || capacity < 0)
      collector.capacity = 0;
   if (native_width)
      *native_width = 0;
   if (native_height)
      *native_height = 0;

   if (scope == kPlatformAllDisplays)
   {
      EnumDisplayMonitors(NULL, NULL, CollectAllMonitorModes,
                          (LPARAM)&collector);
   }
   else
   {
      HMONITOR monitor;
      HWND window = GetActiveWindow();
      if (window)
         monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
      else
      {
         POINT cursor = {};
         GetCursorPos(&cursor);
         monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
      }
      CollectMonitorModes(monitor, &collector, native_width, native_height);
   }

   return collector.count;
}

static uint32 ClampMemorySize(DWORDLONG size)
{
   return size > UINT32_MAX ? UINT32_MAX : (uint32)size;
}

BOOL PlatformGetMemoryStatus(sPlatformMemoryStatus *status)
{
   MEMORYSTATUSEX native = {};

   if (!status)
      return FALSE;
   memset(status, 0, sizeof(*status));
   native.dwLength = sizeof(native);
   if (!GlobalMemoryStatusEx(&native))
      return FALSE;

   status->load_percent = native.dwMemoryLoad;
   status->total_physical_bytes = ClampMemorySize(native.ullTotalPhys);
   status->available_physical_bytes = ClampMemorySize(native.ullAvailPhys);
   status->total_page_file_bytes = ClampMemorySize(native.ullTotalPageFile);
   status->available_page_file_bytes = ClampMemorySize(native.ullAvailPageFile);
   status->total_virtual_bytes = ClampMemorySize(native.ullTotalVirtual);
   status->available_virtual_bytes = ClampMemorySize(native.ullAvailVirtual);
   return TRUE;
}

static BOOL MakeUserKey(char *key, size_t size, const char *store)
{
   int count;
   if (!key || !size || !store || !store[0])
      return FALSE;
   count = snprintf(key, size, "Software\\OpenDarkEngine\\%s", store);
   return count > 0 && (size_t)count < size;
}

BOOL PlatformReadUserUInt(const char *store, const char *name, uint32 *value)
{
   char key_name[256];
   HKEY key;
   DWORD type = REG_DWORD;
   DWORD size = sizeof(*value);
   BOOL result;

   if (!name || !value || !MakeUserKey(key_name, sizeof(key_name), store) ||
       RegOpenKeyExA(HKEY_CURRENT_USER, key_name, 0, KEY_QUERY_VALUE, &key) !=
          ERROR_SUCCESS)
      return FALSE;

   result = RegQueryValueExA(key, name, NULL, &type, (BYTE *)value, &size) ==
               ERROR_SUCCESS &&
            type == REG_DWORD && size == sizeof(*value);
   RegCloseKey(key);
   return result;
}

BOOL PlatformWriteUserUInt(const char *store, const char *name, uint32 value)
{
   char key_name[256];
   HKEY key;
   DWORD disposition;
   LONG result;

   if (!name || !MakeUserKey(key_name, sizeof(key_name), store) ||
       RegCreateKeyExA(HKEY_CURRENT_USER, key_name, 0, NULL, 0, KEY_SET_VALUE,
                       NULL, &key, &disposition) != ERROR_SUCCESS)
      return FALSE;

   result = RegSetValueExA(key, name, 0, REG_DWORD, (const BYTE *)&value,
                           sizeof(value));
   RegCloseKey(key);
   return result == ERROR_SUCCESS;
}

const char *PlatformGetLanguageCode(void)
{
   switch (PRIMARYLANGID(GetSystemDefaultLangID()))
   {
      case LANG_FRENCH:  return "fr";
      case LANG_GERMAN:  return "de";
      case LANG_ITALIAN: return "it";
      case LANG_SPANISH: return "es";
      default:           return "en";
   }
}

void PlatformShowError(const char *title, const char *message)
{
   MessageBoxA(NULL, message ? message : "", title ? title : "Dark Engine",
               MB_OK | MB_ICONERROR);
}

int PlatformPopupMenu(const char *const *items, int count,
                      int client_x, int client_y)
{
   HWND window = GetActiveWindow();
   HMENU menu = CreatePopupMenu();
   POINT screen_pos = { client_x, client_y };
   UINT selection;

   if (!window || !menu)
   {
      if (menu)
         DestroyMenu(menu);
      return 0;
   }

   for (int i = 0; i < count; ++i)
      AppendMenuA(menu, MF_STRING, (UINT_PTR)(i + 1), items[i]);

   ClientToScreen(window, &screen_pos);
   selection = TrackPopupMenu(menu,
                              TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                              screen_pos.x, screen_pos.y, 0, window, NULL);
   DestroyMenu(menu);
   return (int)selection;
}

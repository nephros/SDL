/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "SDL_internal.h"

#include "../SDL_sysurl.h"

#ifdef SDL_PLATFORM_SAILFISHOS

#ifdef SDL_USE_LIBDBUS
#define LIPSTICK_URLOPEN_SERVICE "com.jolla.lipstick"
#define LIPSTICK_URLOPEN_PATH    "/"
#define LIPSTICK_URLOPEN_IFACE   "org.sailfishos.fileservice"
#endif

bool SDL_SYS_OpenURL(const char *url)
{
    bool result = false;

#ifdef SDL_USE_LIBDBUS
    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    if (!dbus) {
        return false;
    }

    result = SDL_DBus_CallVoidMethodOnConnection(dbus->session_conn,
                                            LIPSTICK_URLOPEN_SERVICE, LIPSTICK_URLOPEN_PATH, LIPSTICK_URLOPEN_IFACE,
                                            "openUrl", DBUS_TYPE_STRING, url,
                                            DBUS_TYPE_INVALID);
#ifdef // SDL_USE_LIBDBUS

    return result;
}

#endif // SDL_PLATFORM_SAILFISHOS

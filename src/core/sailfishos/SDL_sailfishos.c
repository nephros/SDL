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

#ifdef SDL_PLATFORM_SAILFISHOS

#include "SDL_sailfishos.h"

// new location starting from SFOS 5.1
#define HW_RELEASE_FILENAME "/usr/lib/hw-release"
#define HW_RELEASE_FILENAME_LEGACY "/etc/hw-release"

bool SDL_IsSailfishOSTablet(void)
{
    bool ret = false;

    FILE *file = fopen(HW_RELEASE_FILENAME, "r");
    if (file == NULL) {
        file = fopen(HW_RELEASE_FILENAME_LEGACY, "r");
        if (file == NULL) {
            SDL_LogWarn(SDL_LOG_CATEGORY_SYSTEM, "Could not find hardware release file!");
            return false;
        }
    }

    char line[128];
    while (fgets(line, sizeof(line), file) != NULL)
    {
        char dev[64];
        SDL_sscanf(line, "MER_HA_DEVICE=%s", dev);
        if (( SDL_strcmp(dev, "tbj") == 0 )  // Original Jolla Tablet
           || SDL_strcmp(dev, "pinetab")
           || SDL_strcmp(dev, "pinetab2")
           || SDL_strcmp(dev, "pipa") ) // VerandiTeams's port of Xiaomi Pad 6
            { ret = true; break; }
    }

    return ret;
}

#endif // SDL_PLATFORM_SAILFISHOS


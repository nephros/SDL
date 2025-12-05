/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>
  Copyright (C) 2024 Peter G. <sailfish@nephros.org>

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

#ifndef SDL_maliit_h_
#define SDL_maliit_h_

#include "../../SDL_internal.h"

#include "SDL_stdinc.h"
#include "SDL_rect.h"

extern SDL_bool SDL_Maliit_Init(void);
extern void SDL_Maliit_Quit(void);
extern void SDL_Maliit_SetFocus(SDL_bool focused);
extern void SDL_Maliit_Reset(void);
extern SDL_bool SDL_Maliit_ProcessKeyEvent(Uint32 keysym, Uint32 keycode, Uint8 state);
extern void SDL_Maliit_UpdateTextRect(const SDL_Rect *rect);
extern void SDL_Maliit_PumpEvents(void);

#endif /* SDL_maliit_h_ */

/* vi: set ts=4 sw=4 expandtab: */

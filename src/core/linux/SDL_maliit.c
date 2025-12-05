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
#include "../../SDL_internal.h"

#include <unistd.h>

#include "SDL_maliit.h"
#include "SDL_keycode.h"
#include "SDL_keyboard.h"
#include "../../events/SDL_keyboard_c.h"
#include "SDL_dbus.h"
#include "SDL_syswm.h"
// #ifdef SDL_VIDEO_DRIVER_X11
// #  include "../../video/x11/SDL_x11video.h"
// #endif
#include "SDL_hints.h"

#define MALIIT_ADDRESS_SERVICE "org.maliit.server"
#define MALIIT_ADDRESS_INTERFACE "org.maliit.Server.Address"
#define MALIIT_ADDRESS_PATH "/org/maliit/server/address"

#define MALIIT_IMS_INTERFACE "com.meego.inputmethod.uiserver1"
#define MALIIT_IMS_PATH "/com/meego/inputmethod/uiserver1"

#define MALIIT_IMC_INTERFACE "com.meego.inputmethod.inputcontext1"
#define MALIIT_IMC_PATH "/com/meego/inputmethod/inputcontext"

#define DBUS_LOCAL_PATH "/org/freedesktop/DBus/Local" ;
#define DBUS_LOCAL_INTERFACE "org.freedesktop.DBus.Local" ;

#define DBUS_TIMEOUT 500

typedef struct _MaliitServer {

} MaliitServer;
typedef struct  _MImSettingsInfo {
    char *info;
} MImSettingsInfo;

typedef struct _MaliitIMContext {
   MImSettingsInfo *info;
} MaliitIMContext;

typedef struct _MaliitClient
{
    DBusConnection *conn;

    MaliitIMContext *context;
    MaliitServer *server;

    char* id;

    SDL_Rect cursor_rect;
} MaliitClient;

static MaliitClient maliit_client;

static size_t Maliit_GetPreeditString(SDL_DBusContext *dbus,
                       DBusMessage *msg,
                       char **ret,
                       Sint32 *start_pos,
                       Sint32 *end_pos)
{
    char *text = NULL, *subtext;
    size_t text_bytes = 0;
    DBusMessageIter iter, array, sub;
    Sint32 p_start_pos = -1;
    Sint32 p_end_pos = -1;

    dbus->message_iter_init(msg, &iter);
    /* Message type is a(si)i, we only need string part */
    if (dbus->message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
        size_t pos = 0;
        /* First pass: calculate string length */
        dbus->message_iter_recurse(&iter, &array);
        while (dbus->message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT) {
            dbus->message_iter_recurse(&array, &sub);
            subtext = NULL;
            if (dbus->message_iter_get_arg_type(&sub) == DBUS_TYPE_STRING) {
                dbus->message_iter_get_basic(&sub, &subtext);
                if (subtext && *subtext) {
                    text_bytes += SDL_strlen(subtext);
                }
            }
            dbus->message_iter_next(&sub);
            if (dbus->message_iter_get_arg_type(&sub) == DBUS_TYPE_INT32 && p_end_pos == -1) {
                /* Type is a bit field defined as follows:                */
                /* bit 3: Underline, bit 4: HighLight, bit 5: DontCommit, */
                /* bit 6: Bold,      bit 7: Strike,    bit 8: Italic      */
                Sint32 type;
                dbus->message_iter_get_basic(&sub, &type);
                /* We only consider highlight */
                if (type & (1 << 4)) {
                    if (p_start_pos == -1) {
                        p_start_pos = pos;
                    }
                } else if (p_start_pos != -1 && p_end_pos == -1) {
                    p_end_pos = pos;
                }
            }
            dbus->message_iter_next(&array);
            if (subtext && *subtext) {
                pos += SDL_utf8strlen(subtext);
            }
        }
        if (p_start_pos != -1 && p_end_pos == -1) {
            p_end_pos = pos;
        }
        if (text_bytes) {
            text = SDL_malloc(text_bytes + 1);
        }

        if (text) {
            char *pivot = text;
            /* Second pass: join all the sub string */
            dbus->message_iter_recurse(&iter, &array);
            while (dbus->message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT) {
                dbus->message_iter_recurse(&array, &sub);
                if (dbus->message_iter_get_arg_type(&sub) == DBUS_TYPE_STRING) {
                    dbus->message_iter_get_basic(&sub, &subtext);
                    if (subtext && *subtext) {
                        size_t length = SDL_strlen(subtext);
                        SDL_strlcpy(pivot, subtext, length + 1);
                        pivot += length;
                    }
                }
                dbus->message_iter_next(&array);
            }
        } else {
            text_bytes = 0;
        }
    }

    *ret = text;
    *start_pos = p_start_pos;
    *end_pos = p_end_pos;
    return text_bytes;
}

/*
static void Maliit_updateWidgetInfo(DBusConnection *conn, SDL_bool focus)
{
    DBusMessage *msg;
    SDL_Window *focused_win = NULL;
    SDL_SysWMinfo info;

    focused_win = SDL_GetKeyboardFocus();
    if (!focused_win) {
        return;
    }

    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(focused_win, &info)) {
        return;
    }

    msg = dbus_message_new_method(MALIIT_IMC_PATH, MALIIT_IMC_INTERFACE, NULL);
    msg->append_args(conn, 1, "focusState", SDL_TRUE);
    //msg->append_args(conn, 1, "winId", "SDL_App");
    //msg->append_args(conn, 1, "winId", info.wl_display);
    //dbus->connection_send(conn, msg, NULL);
}
*/


static Sint32 Maliit_GetPreeditCursorByte(SDL_DBusContext *dbus, DBusMessage *msg)
{
    Sint32 byte = -1;
    DBusMessageIter iter;

    dbus->message_iter_init(msg, &iter);

    dbus->message_iter_next(&iter);

    if (dbus->message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32) {
        return -1;
    }

    dbus->message_iter_get_basic(&iter, &byte);

    return byte;
}

static DBusHandlerResult DBus_MessageFilter(DBusConnection *conn, DBusMessage *msg, void *data)
{
    SDL_DBusContext *dbus = (SDL_DBusContext *)data;

    if (dbus->message_is_signal(msg, MALIIT_IMC_INTERFACE, "commitString")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: got a DBus message: %s", "commitString");
        DBusMessageIter iter;
        const char *text = NULL;

        dbus->message_iter_init(msg, &iter);
        dbus->message_iter_get_basic(&iter, &text);

        if (text && *text) {
            char buf[SDL_TEXTINPUTEVENT_TEXT_SIZE];
            size_t text_bytes = SDL_strlen(text), i = 0;

            while (i < text_bytes) {
                size_t sz = SDL_utf8strlcpy(buf, text + i, sizeof(buf));
                SDL_SendKeyboardText(buf);

                i += sz;
            }
        }

        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus->message_is_signal(msg, MALIIT_IMC_INTERFACE, "updatePreedit")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: got a DBus message: %s", "updatePreedit");
        char *text = NULL;
        Sint32 start_pos, end_pos;
        size_t text_bytes = Maliit_GetPreeditString(dbus, msg, &text, &start_pos, &end_pos);
        if (text_bytes) {
            if (SDL_GetHintBoolean(SDL_HINT_IME_SUPPORT_EXTENDED_TEXT, SDL_FALSE)) {
                if (start_pos == -1) {
                    Sint32 byte_pos = Maliit_GetPreeditCursorByte(dbus, msg);
                    start_pos = byte_pos >= 0 ? SDL_utf8strnlen(text, byte_pos) : -1;
                }
                SDL_SendEditingText(text, start_pos, end_pos >= 0 ? end_pos - start_pos : -1);
            } else {
                char buf[SDL_TEXTEDITINGEVENT_TEXT_SIZE];
                size_t i = 0;
                size_t cursor = 0;
                while (i < text_bytes) {
                    const size_t sz = SDL_utf8strlcpy(buf, text + i, sizeof(buf));
                    const size_t chars = SDL_utf8strlen(buf);

                    SDL_SendEditingText(buf, cursor, chars);

                    i += sz;
                    cursor += chars;
                }
            }
            SDL_free(text);
        } else {
            SDL_SendEditingText("", 0, 0);
        }

        SDL_Maliit_UpdateTextRect(NULL);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus->message_is_signal(msg, MALIIT_IMC_INTERFACE, "keyEvent")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Event not yet handled: %s", "keyEvent");
    }
    if (dbus->message_is_signal(msg, MALIIT_IMC_INTERFACE, "imInitiatedHide")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Event not yet handled: %s", "imInitiatedHide");
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void MaliitClientICCallMethod(MaliitClient *client, const char *method)
{
    if (!client->conn) {
        return;
    }
    SDL_DBus_CallVoidMethodOnConnection(client->conn, MALIIT_IMC_PATH, MALIIT_IMC_INTERFACE, method, DBUS_TYPE_INVALID);
}

static char* MaliitClientGetAddress(void)
{
    char *addr = NULL;
    SDL_DBusContext *dbus;

    addr = SDL_getenv("MALIIT_SERVER_ADDRESS");
    if (addr != NULL) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit server address set from environment");
        return SDL_strdup(addr);
    }

    dbus = SDL_DBus_GetContext();
    if (!dbus) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Could not connect to bus.");
        return NULL;
    }

    SDL_DBus_QueryProperty(MALIIT_ADDRESS_SERVICE, MALIIT_ADDRESS_PATH, MALIIT_ADDRESS_INTERFACE,
                           "address", DBUS_TYPE_STRING, &addr);
    if (!addr) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "ERROR: Could not get Maliit server address!");
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit server address determined from bus.");
    return SDL_strdup(addr);
}

static Uint32 Maliit_ModState(void)
{
    Uint32 maliit_mods = 0;
    SDL_Keymod sdl_mods = SDL_GetModState();

    if (sdl_mods & KMOD_SHIFT) {
        maliit_mods |= (1 << 0);
    }
    if (sdl_mods & KMOD_CAPS) {
        maliit_mods |= (1 << 1);
    }
    if (sdl_mods & KMOD_CTRL) {
        maliit_mods |= (1 << 2);
    }
    if (sdl_mods & KMOD_ALT) {
        maliit_mods |= (1 << 3);
    }
    if (sdl_mods & KMOD_NUM) {
        maliit_mods |= (1 << 4);
    }
    if (sdl_mods & KMOD_MODE) {
        maliit_mods |= (1 << 7);
    }
    if (sdl_mods & KMOD_LGUI) {
        maliit_mods |= (1 << 6);
    }
    if (sdl_mods & KMOD_RGUI) {
        maliit_mods |= (1 << 28);
    }

    return maliit_mods;
}

SDL_bool SDL_Maliit_Init(void)
{
    const char* addr = NULL;
    SDL_DBusContext* dbus = SDL_DBus_GetContext();

    maliit_client.cursor_rect.x = -1;
    maliit_client.cursor_rect.y = -1;
    maliit_client.cursor_rect.w = 0;
    maliit_client.cursor_rect.h = 0;

    addr = MaliitClientGetAddress();

    if(!addr) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Could not get Server address.");
        return SDL_FALSE;
    }

    DBusConnection *conn = dbus->connection_open_private(addr, NULL);
    if (!conn) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Could not open connection");
        return SDL_FALSE;
    }

    dbus->bus_add_match(conn,
                        "type='signal', interface='com.meego.inputmethod.inputcontext1'",
                        NULL);
    dbus->connection_add_filter(conn, &DBus_MessageFilter, NULL, NULL);

    maliit_client.conn = conn;
    MaliitClientICCallMethod(&maliit_client, "activateContext");
    return SDL_TRUE;
}

void SDL_Maliit_Quit(void)
{
    MaliitClientICCallMethod(&maliit_client, "hideInputMethod");
    if (maliit_client.conn) {
        SDL_DBusContext *dbus;
        dbus = SDL_DBus_GetContext();
        dbus->connection_close(maliit_client.conn);
    }
}

void SDL_Maliit_SetFocus(SDL_bool focused)
{
    if (focused) {
        MaliitClientICCallMethod(&maliit_client, "activateContext");
        //MaliitClientICCallMethod(&maliit_client, "updateWidgetInformation");
        MaliitClientICCallMethod(&maliit_client, "showInputMethod");
    } else {
        MaliitClientICCallMethod(&maliit_client, "hideInputMethod");
    }
}

void SDL_Maliit_Reset(void)
{
    MaliitClientICCallMethod(&maliit_client, "reset");
}

SDL_bool SDL_Maliit_ProcessKeyEvent(Uint32 keysym, Uint32 keycode, Uint8 state)
{
    Uint32 mod_state = Maliit_ModState();
    Uint32 handled = SDL_FALSE;
    Uint32 is_release = (state == SDL_RELEASED);
    Uint32 event_time = 0;

    if (!maliit_client.conn) {
        return SDL_FALSE;
    }

    /*
    if (SDL_DBus_CallMethodOnConnection(maliit_client.conn, MALIIT_IMC_PATH, MALIIT_IMC_INTERFACE, "processKeyEvent",
                            DBUS_TYPE_UINT32, &keysym, DBUS_TYPE_UINT32, &keycode, DBUS_TYPE_UINT32, &mod_state, DBUS_TYPE_BOOLEAN, &is_release, DBUS_TYPE_UINT32, &event_time, DBUS_TYPE_INVALID,
                            DBUS_TYPE_BOOLEAN, &handled, DBUS_TYPE_INVALID)) {
        if (handled) {
            SDL_Maliit_UpdateTextRect(NULL);
            return SDL_TRUE;
        }
    }
    */
    return SDL_FALSE;
}

void SDL_Maliit_UpdateTextRect(const SDL_Rect *rect)
{
    SDL_Window *focused_win = NULL;
    SDL_SysWMinfo info;
    int x = 0, y = 0;
    SDL_Rect *cursor = &maliit_client.cursor_rect;

    if (rect) {
        SDL_copyp(cursor, rect);
    }

    focused_win = SDL_GetKeyboardFocus();
    if (!focused_win) {
        return;
    }

    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(focused_win, &info)) {
        return;
    }

    SDL_GetWindowPosition(focused_win, &x, &y);

    if (!maliit_client.conn) {
        return;
    }

    //SDL_DBus_CallVoidMethodOnConnection(maliit_client.conn, MALIIT_IMC_PATH, MALIIT_IMC_INTERFACE, "updateInputMethodArea",
    //                        DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32, &y, DBUS_TYPE_INT32, &cursor->w, DBUS_TYPE_INT32, &cursor->h, DBUS_TYPE_INVALID);
}

void SDL_Maliit_PumpEvents(void)
{
    SDL_DBusContext *dbus;
    DBusConnection *conn;
    dbus = SDL_DBus_GetContext();
    conn = maliit_client.conn;

    dbus->connection_read_write(conn, 0);

    while (dbus->connection_dispatch(conn) == DBUS_DISPATCH_DATA_REMAINS) {
        /* Do nothing, actual work happens in DBus_MessageFilter */
    }
}

/* vi: set ts=4 sw=4 expandtab: */

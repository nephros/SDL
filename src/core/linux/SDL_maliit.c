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
#include "SDL_hints.h"

#define MALIIT_ADDRESS_SERVICE "org.maliit.server"
#define MALIIT_ADDRESS_INTERFACE "org.maliit.Server.Address"
#define MALIIT_ADDRESS_PATH "/org/maliit/server/address"

#define MALIIT_IMSERVER_INTERFACE "com.meego.inputmethod.uiserver1"
#define MALIIT_IMSERVER_PATH "/com/meego/inputmethod/uiserver1"

#define MALIIT_IMCONTEXT_INTERFACE "com.meego.inputmethod.inputcontext1"
#define MALIIT_IMCONTEXT_PATH "/com/meego/inputmethod/inputcontext"

#define DBUS_LOCAL_PATH "/org/freedesktop/DBus/Local" ;
#define DBUS_LOCAL_INTERFACE "org.freedesktop.DBus.Local" ;

#define DBUS_TIMEOUT 500

typedef struct _MaliitClient
{
    SDL_DBusContext *dbus;
    DBusConnection *conn;

    char* id;

    SDL_bool active;
    SDL_bool hidden;
    SDL_bool focus;

    SDL_Rect cursor_rect;
} MaliitClient;

static MaliitClient maliit_client;

static char *GetAppName(void);

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

static void Maliit_updateOrientation()
{
    int orientation = 0;
    SDL_DisplayOrientation o = SDL_GetDisplayOrientation(0);
    if (o == SDL_ORIENTATION_UNKNOWN) {
        orientation = 180;
    }
    if (o == SDL_ORIENTATION_PORTRAIT) {
        orientation = 0;
    }
    if (o == SDL_ORIENTATION_PORTRAIT_FLIPPED) {
        orientation = 180;
    }
    if (o == SDL_ORIENTATION_LANDSCAPE) {
        orientation = 90;
    }
    if (o == SDL_ORIENTATION_LANDSCAPE_FLIPPED) {
        orientation = 270;
    }
    SDL_DBus_CallVoidMethodOnConnection(maliit_client.conn, NULL, MALIIT_IMSERVER_PATH, MALIIT_IMSERVER_INTERFACE, "appOrientationChanged",
                            DBUS_TYPE_INT32, &orientation, DBUS_TYPE_INVALID);
}

static void Maliit_updateWidgetInfo(SDL_bool focus)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: updateWidgetInfo, focus: %s", focus ? "true" : "false");
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

    char *appname = GetAppName();
    const char *key;

    DBusMessage *msg = maliit_client.dbus->message_new_method_call(NULL,
                                                    MALIIT_IMSERVER_PATH,
                                                    MALIIT_IMSERVER_INTERFACE,
                                                    "updateWidgetInformation");
    DBusMessageIter args, dict, entry, variant;
    
    /*
        focusState                bool
        surroundingText           string
        cursorPosition            int
        anchorPosition            int
        autocapitalizationEnabled hint
        hiddenText                hint
        predictionEnabled         hint
        maliit-inputmethod-hints  hint
        enterKeyType              ???
        hasSelection              bool
        winId                     uintXX
        cursorRectangle           rect ( (ii) ???)
        toolbarId                 Global extension id.
    */

    maliit_client.dbus->message_iter_init_append(msg, &args);
    // Append a{sv}
    maliit_client.dbus->message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict);      // a{
    
    key = "winId";
    maliit_client.dbus->message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);  // BEG entry
    maliit_client.dbus->message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);               // "winId"
    maliit_client.dbus->message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);   // s
    maliit_client.dbus->message_iter_append_basic(&variant, DBUS_TYPE_STRING, &appname);         // "foo"
    maliit_client.dbus->message_iter_close_container(&entry, &variant);                          // ,
    maliit_client.dbus->message_iter_close_container(&dict, &entry);                              // END entry
    key = "focusState";
    Uint32 value = maliit_client.focus ? 1 : 0;
    maliit_client.dbus->message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);  // BEG entry
    maliit_client.dbus->message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);               // "focusState"
    maliit_client.dbus->message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "u", &variant);   // u
    maliit_client.dbus->message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &value);           // 1/0
    maliit_client.dbus->message_iter_close_container(&entry, &variant);                          // ,
    maliit_client.dbus->message_iter_close_container(&dict, &entry);                             // END entry
    key = "toolbarId";
    value = 0;
    maliit_client.dbus->message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);  // BEG entry
    maliit_client.dbus->message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);               // "toolbarId"
    maliit_client.dbus->message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "u", &variant);   // u
    maliit_client.dbus->message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &value);           // 0
    maliit_client.dbus->message_iter_close_container(&entry, &variant);                          // ,
    maliit_client.dbus->message_iter_close_container(&dict, &entry);                             // END entry

    maliit_client.dbus->message_iter_close_container(&args, &dict);                              // }

    // Append boolean
    dbus_bool_t focusChanged = TRUE;
    maliit_client.dbus->message_iter_append_basic(&args, DBUS_TYPE_BOOLEAN, &focusChanged);

    maliit_client.dbus->connection_send(maliit_client.conn, msg, DBUS_TYPE_INVALID);
    maliit_client.dbus->connection_flush(maliit_client.conn);
    maliit_client.dbus->message_unref(msg);

/*
    // FIXME: is the correct signature <arg type="a{sv}" name="stateInformation"/>??
    SDL_DBus_CallVoidMethodOnConnection(maliit_client.conn, NULL, MALIIT_IMSERVER_PATH, MALIIT_IMSERVER_INTERFACE, "updateWidgetInformation",
                            DBUS_TYPE_STRING, &appname,
                            DBUS_TYPE_BOOLEAN, &focus,
                            DBUS_TYPE_INVALID);
*/
}


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

    /*
     * ***** Context Messages *****
     */
    if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "activationLostEvent")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: got a DBus message: %s", "activationLostEvent");
        maliit_client.active = SDL_FALSE;
        maliit_client.hidden = SDL_TRUE;
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "imInitiatedHide")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: got a DBus message: %s", "imInitiatedHide");
        SDL_SendEditingText("", 0, 0);
        maliit_client.hidden = SDL_TRUE;
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "commitString")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: got a DBus message: %s", "commitString");
        DBusMessageIter iter;
        const char *text = NULL;

        dbus->message_iter_init(msg, &iter);
        if (dbus->message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
            dbus->message_iter_get_basic(&iter, &text);
        }

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
    } else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "updatePreedit")) {
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
    } else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "keyEvent")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Event not yet handled: %s", "keyEvent");
    } else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "updateInputMethodArea")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Event not yet handled: %s", "updateInputMethodArea");
    } else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "selection")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Event not yet handled: %s", "selection");
    /* 
     * ***** Context Messages *****
     */
    } else if (dbus->message_is_signal(msg, MALIIT_IMSERVER_INTERFACE, "mouseClickedOnPreedit")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Event not yet handled: %s", "mouseClickedOnPreedit");
    } else if (dbus->message_is_signal(msg, MALIIT_IMSERVER_INTERFACE, "imInitiatedHide")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Event not yet handled: %s", "imInitiatedHide");
    } else if (dbus->message_is_signal(msg, MALIIT_IMSERVER_INTERFACE, "invokeAction")) {
            const char* action; const char* sequence;
            DBusMessageIter iter;
            dbus->message_iter_init(msg, &iter);
            dbus->message_iter_get_basic(&iter, &action);
            dbus->message_iter_get_basic(&iter, &sequence);
            SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Event not yet handled: %s: %s, %s", "invokeAction", action, sequence);
     } else {
        DBusMessageIter iter;

        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Unhandled Event details:\n");

        if (dbus->message_iter_init(msg, &iter)) {
            switch (dbus->message_iter_get_arg_type(&iter)) {
                case DBUS_TYPE_STRING: {
                    char* value;
                    dbus->message_iter_get_basic(&iter, &value);
                    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Event argument: %s", value);
                    break;
                }
                case DBUS_TYPE_BOOLEAN: {
                    SDL_bool value;
                    dbus->message_iter_get_basic(&iter, &value);
                    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Event argument: %s", value ? "[TRUE]" : "[FALSE]");
                    break;
                }
                case DBUS_TYPE_INT16:
                case DBUS_TYPE_UINT16:
                case DBUS_TYPE_INT32:
                case DBUS_TYPE_UINT32:
                case DBUS_TYPE_INT64:
                case DBUS_TYPE_UINT64: {
                    int value = 0;
                    dbus->message_iter_get_basic(&iter, &value);
                    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Event argument: %d", (int)value);
                    break;
                }
                case DBUS_TYPE_ARRAY:
                    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Event argument: %s", "{ARRAY}");
                    break;
                case DBUS_TYPE_STRUCT:
                    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Event argument: %s", "(STRUCT)");
                    break;
            }
        }
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void MaliitClientCallServerMethod(MaliitClient *client, const char *method)
{
    if (!client->conn) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Maliit: calling IMS method without a connection!");
        return;
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: calling IMS method: %s", method);
    if(SDL_DBus_CallVoidMethodOnConnection(client->conn, NULL, MALIIT_IMSERVER_PATH, MALIIT_IMSERVER_INTERFACE, method, DBUS_TYPE_INVALID) == SDL_FALSE) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Maliit: calling IMS method FAILED");
    }
}

/* currently unused
static void MaliitClientCallContextMethod(MaliitClient *client, const char *method)
{
    if (!client->conn) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Maliit: calling IMC method without a connection!");
        return;
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: calling IMC method: %s", method);
    if(SDL_DBus_CallVoidMethodOnConnection(client->conn, MALIIT_IMCONTEXT_PATH, MALIIT_IMCONTEXT_INTERFACE, method, DBUS_TYPE_INVALID) == SDL_FALSE) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Maliit: calling IMC method FAILED");
    }
}
*/

static char* MaliitClientGetAddress(void)
{
    char *addr = NULL;
    SDL_DBusContext *dbus;

    addr = SDL_getenv("MALIIT_SERVER_ADDRESS");
    if (addr != NULL) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Server address set from environment");
        return SDL_strdup(addr);
    }

    dbus = maliit_client.dbus;
    if (!dbus) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Maliit: Could not connect to bus");
        return NULL;
    }

    SDL_DBus_QueryProperty(MALIIT_ADDRESS_SERVICE, MALIIT_ADDRESS_PATH, MALIIT_ADDRESS_INTERFACE,
                           "address", DBUS_TYPE_STRING, &addr);
    if (!addr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Maliit: Could not get Maliit server address!");
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Server address determined from bus");
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
    SDL_LogVerbose(SDL_LOG_CATEGORY_INPUT, "Maliit: Init");
    maliit_client.dbus = SDL_DBus_GetContext();

    maliit_client.cursor_rect.x = -1;
    maliit_client.cursor_rect.y = -1;
    maliit_client.cursor_rect.w = 0;
    maliit_client.cursor_rect.h = 0;

    const char* addr = MaliitClientGetAddress();
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: connecting via address %s", addr);

    if(!addr) {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Maliit: Could not get Server address.");
        return SDL_FALSE;
    }

    DBusConnection *conn = maliit_client.dbus->connection_open_private(addr, NULL);
    if (!conn) {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Maliit: Could not open connection");
        return SDL_FALSE;
    }
    if (maliit_client.dbus->connection_get_is_connected(conn)) {
        SDL_LogVerbose(SDL_LOG_CATEGORY_INPUT, "Maliit: connection established.");
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: setting up message filter");
    maliit_client.dbus->bus_add_match(conn,
                        "type='signal', interface='com.meego.inputmethod.uiserver1'",
                        NULL);
    maliit_client.dbus->bus_add_match(conn,
                        "type='signal', interface='com.meego.inputmethod.inputcontext1'",
                        NULL);
    maliit_client.dbus->connection_add_filter(conn, &DBus_MessageFilter, maliit_client.dbus, NULL);

    maliit_client.conn = conn;

    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Init done");
    return SDL_TRUE;
}

void SDL_Maliit_Quit(void)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Quit");
    MaliitClientCallServerMethod(&maliit_client, "hideInputMethod");
    if (maliit_client.conn) {
        maliit_client.dbus->connection_close(maliit_client.conn);
        maliit_client.dbus->connection_unref(maliit_client.conn);
    }
    maliit_client.dbus = NULL;
    maliit_client.conn = NULL;
}

void SDL_Maliit_SetFocus(SDL_bool focused)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: SetFocus");
    SDL_Window *focused_win = NULL;
    focused_win = SDL_GetKeyboardFocus();

    Maliit_updateOrientation();

    if (focused) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: activating");
        MaliitClientCallServerMethod(&maliit_client, "activateContext");
        Maliit_updateWidgetInfo(focused);
        //MaliitClientCallServerMethod(&maliit_client, "appOrientationChanged"); // orientation, i 270
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: showing");
        MaliitClientCallServerMethod(&maliit_client, "showInputMethod");
        maliit_client.active = SDL_TRUE;
        maliit_client.hidden = SDL_FALSE;
    } else {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: de-activating");
        MaliitClientCallServerMethod(&maliit_client, "hideInputMethod");
        maliit_client.hidden = SDL_TRUE;
    }
}

void SDL_Maliit_Reset(void)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Reset");
    MaliitClientCallServerMethod(&maliit_client, "reset");
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

    if (SDL_DBus_CallMethodOnConnection(maliit_client.conn, NULL, MALIIT_IMCONTEXT_PATH, MALIIT_IMCONTEXT_INTERFACE, "processKeyEvent",
                            DBUS_TYPE_UINT32, &keysym, DBUS_TYPE_UINT32, &keycode, DBUS_TYPE_UINT32, &mod_state, DBUS_TYPE_BOOLEAN, &is_release, DBUS_TYPE_UINT32, &event_time, DBUS_TYPE_INVALID,
                            DBUS_TYPE_BOOLEAN, &handled, DBUS_TYPE_INVALID)) {
        if (handled) {
            SDL_Maliit_UpdateTextRect(NULL);
            return SDL_TRUE;
        }
    }
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

    SDL_DBus_CallVoidMethodOnConnection(maliit_client.conn, NULL, MALIIT_IMCONTEXT_PATH, MALIIT_IMCONTEXT_INTERFACE, "updateInputMethodArea",
                            DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32, &y, DBUS_TYPE_INT32, &cursor->w, DBUS_TYPE_INT32, &cursor->h, DBUS_TYPE_INVALID);
}

void SDL_Maliit_PumpEvents(void)
{
    SDL_DBusContext *dbus = maliit_client.dbus;
    DBusConnection *conn  = maliit_client.conn;

    dbus->connection_read_write(conn, 0);

    while (dbus->connection_dispatch(conn) == DBUS_DISPATCH_DATA_REMAINS) {
        /* Do nothing, actual work happens in DBus_MessageFilter */
    }
}

static char *GetAppName(void)
{
    char *spot;
    char procfile[1024];
    char linkfile[1024];
    int linksize;

    (void)SDL_snprintf(procfile, sizeof(procfile), "/proc/%d/exe", getpid());
    linksize = readlink(procfile, linkfile, sizeof(linkfile) - 1);
    if (linksize > 0) {
        linkfile[linksize] = '\0';
        spot = SDL_strrchr(linkfile, '/');
        if (spot) {
            return SDL_strdup(spot + 1);
        } else {
            return SDL_strdup(linkfile);
        }
    }
    return SDL_strdup("SDL_App");
}


/* vi: set ts=4 sw=4 expandtab: */

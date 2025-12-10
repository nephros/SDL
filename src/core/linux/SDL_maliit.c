/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>
  Copyright (C) 2025 Peter G. <sailfish@nephros.org>

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

#define DBUS_LOCAL_PATH "/org/freedesktop/DBus/Local"
#define DBUS_LOCAL_INTERFACE "org.freedesktop.DBus.Local"

#define DBUS_TIMEOUT 500

typedef struct _MaliitClient
{
    DBusConnection *conn;
    char* id;
    SDL_Rect cursor_rect;
    SDL_bool active;

} MaliitClient;

static MaliitClient maliit_client;

static char *GetAppName(void);

static SDL_bool Maliit_CheckConnection(void);

static void Maliit_updateOrientation(SDL_bool doit)
{
    int orientation = 0;
    const char* os;
    SDL_DisplayOrientation o = SDL_GetDisplayOrientation(0);
    if (o == SDL_ORIENTATION_UNKNOWN) {
        orientation = 180;
        os = "unknown";
    }
    if (o == SDL_ORIENTATION_PORTRAIT) {
        orientation = 0;
        os = "Portrait";
    }
    if (o == SDL_ORIENTATION_PORTRAIT_FLIPPED) {
        orientation = 180;
        os = "Portrait Flipped";
    }
    if (o == SDL_ORIENTATION_LANDSCAPE) {
        orientation = 270;
        os = "Landscape";
    }
    if (o == SDL_ORIENTATION_LANDSCAPE_FLIPPED) {
        orientation = 90;
        os = "Landscape Flipped";
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: updateOrientation, SDL: %s, IMS: %d", os, orientation);
    if (Maliit_CheckConnection()) {
        if (doit) {
            if(!SDL_DBus_CallVoidMethodOnConnection(maliit_client.conn, NULL, MALIIT_IMSERVER_PATH, MALIIT_IMSERVER_INTERFACE, "appOrientationAboutToChange",
                                    DBUS_TYPE_INT32, &orientation, DBUS_TYPE_INVALID)) {
                SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Maliit: Call FAILED");
            }
        } else {
            if(!SDL_DBus_CallVoidMethodOnConnection(maliit_client.conn, NULL, MALIIT_IMSERVER_PATH, MALIIT_IMSERVER_INTERFACE, "appOrientationChanged",
                                    DBUS_TYPE_INT32, &orientation, DBUS_TYPE_INVALID)) {
                SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Maliit: Call FAILED");
            }
        }
    }
}

static void Maliit_updateWidgetInfo(SDL_bool focus)
{
    SDL_Window *focused_win = NULL;
    SDL_SysWMinfo info;

    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: updateWidgetInfo, focus: %s", focus ? "true" : "false");


    focused_win = SDL_GetKeyboardFocus();
    if (!focused_win) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: no window focus");
        return;
    }

    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(focused_win, &info)) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: no window info");
        return;
    }
    // FIXME: Which app name/window id to use?
    // maybe:
    //const char* appname = info.info.wl.shell_surface.wl;
    // or:
    // const char* appname = SDL_GetWindowID(focused_win);
    // meanwhile:
    char* appname = GetAppName();

    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: using app name %s", appname);

    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    DBusMessage *msg = dbus->message_new_method_call(NULL,
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

    dbus->message_iter_init_append(msg, &args);
    // Append a{sv}
    dbus->message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict);      // a{

    // There is SDL_DBus_AppendDictWithKeyValue(args, key, value); but we can not use it.

    char* key;
    key = SDL_strdup("winId");
    dbus->message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);  // BEG entry
    dbus->message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);               // "winId"
    dbus->message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);   // s
    dbus->message_iter_append_basic(&variant, DBUS_TYPE_STRING, &appname);         // "foo"
    dbus->message_iter_close_container(&entry, &variant);                          // ,
    dbus->message_iter_close_container(&dict, &entry);                              // END entry
    key = SDL_strdup("focusState");
    Uint32 value = focus ? 1 : 0;
    dbus->message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);  // BEG entry
    dbus->message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);               // "focusState"
    dbus->message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "u", &variant);   // u
    dbus->message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &value);           // 1/0
    dbus->message_iter_close_container(&entry, &variant);                          // ,
    dbus->message_iter_close_container(&dict, &entry);                             // END entry
    key = SDL_strdup("toolbarId");
    value = 0;
    dbus->message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);  // BEG entry
    dbus->message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);               // "toolbarId"
    dbus->message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "u", &variant);   // u
    dbus->message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &value);           // 0
    dbus->message_iter_close_container(&entry, &variant);                          // ,
    dbus->message_iter_close_container(&dict, &entry);                             // END entry

    dbus->message_iter_close_container(&args, &dict);                              // }

    SDL_free(key);
    SDL_free(appname);

    // Append boolean
    dbus_bool_t focusChanged = TRUE;
    dbus->message_iter_append_basic(&args, DBUS_TYPE_BOOLEAN, &focusChanged);

    dbus->connection_send(maliit_client.conn, msg, DBUS_TYPE_INVALID);
    dbus->connection_flush(maliit_client.conn);
    dbus->message_unref(msg);
}

static DBusHandlerResult DBus_Disconnect(DBusConnection *conn, DBusMessage *msg, void *data)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Handling disconnection signal.");

    // TODO: We should detect whether we are active, and reconnect if yes.
    if (maliit_client.active) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Maliit: Server connection vanished while active!");
        // reconnect()
    }
    SDL_DBusContext *dbus = (SDL_DBusContext *)data;
    if (conn) {
        dbus->connection_close(maliit_client.conn);
        dbus->connection_unref(maliit_client.conn);
    }
    maliit_client.conn = NULL;
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult DBus_MessageFilter(DBusConnection *conn, DBusMessage *msg, void *data)
{
    SDL_DBusContext *dbus = (SDL_DBusContext *)data;

    const char* iface  = dbus->message_get_interface(msg);
    const char* member = dbus->message_get_member(msg);
    const char* sig    = dbus->message_get_signature(msg);
    const char* path   = dbus->message_get_path(msg);
    SDL_bool for_us = (iface) && (strcmp(iface, MALIIT_IMCONTEXT_INTERFACE) == 0)
                  && (dbus->message_get_type(msg) != DBUS_MESSAGE_TYPE_INVALID)
                  && (dbus->message_get_type(msg) != DBUS_MESSAGE_TYPE_ERROR)
                  && (dbus->message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_RETURN) ;

    if (dbus->message_get_type(msg) == DBUS_MESSAGE_TYPE_SIGNAL)
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: IT IS A SIGNAL!");

    if (for_us == SDL_FALSE) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT,
            "Maliit: ignoring DBus message not intended for us:\n\tpath:%s\n\tiface:%s\n\tmember:%s\n",
            path,
            iface,
            member
            );
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    /*
     * ***** Context Messages *****
     */
    //if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "activationLostEvent")) {
    if ( (member) && (sig)
        && (strcmp(member, "activationLostEvent") == 0)) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: got a DBus message: %s", "activationLostEvent");
        return DBUS_HANDLER_RESULT_HANDLED;
    //} else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "imInitiatedHide")) {
    } else if ( (member) && (sig)
        && (strcmp(member, "imInitiatedHide") == 0) ) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: got a DBus message: %s", "imInitiatedHide");
        SDL_SendEditingText("", 0, 0);
        return DBUS_HANDLER_RESULT_HANDLED;
    //} else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "commitString")) {
    } else if ( (member) && (sig)
        && (strcmp(member, "commitString") == 0)
        && (strcmp(sig, "siii") == 0)  ) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: got a DBus message: %s", "commitString");

        // siii
        //commitString [dbus.String('asdffg'), dbus.Int32(0), dbus.Int32(0), dbus.Int32(-1)]
        DBusMessageIter iter;
        const char *text = NULL;

        dbus->message_iter_init(msg, &iter);
        if (dbus->message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
            dbus->message_iter_get_basic(&iter, &text);
        }
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: commitString: %s", text);

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
    //} else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "updatePreedit")) {
    } else if ( (member) && (sig)
        && (strcmp(member, "updatePreedit") == 0)
        && (strcmp(sig, "sa(iii)iii") == 0) ) {
         // s The new string
         // a preeditFormats The formats for each part of preedit.
         // i replacementStart The position at which characters are to be replaced relative from the start of the preedit string.
         // i replacementLength The number of characters to be replaced in the preedit string.
         // i cursorPos Cursor position. If it is less than 0, then the cursor will be hidden.
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: got a DBus message: %s", "updatePreedit");
        //return DBUS_HANDLER_RESULT_HANDLED;
        char *text = NULL;
        //Sint32 start_pos, end_pos;
        Sint32 cursor, chars;
        DBusMessageIter iter;
        dbus->message_iter_init(msg, &iter);
        if (dbus->message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
            dbus->message_iter_get_basic(&iter, &text);
        }
        // skip the array of struct of ints "QList<Maliit::PreeditTextFormat>"
        dbus->message_iter_next(&iter);
        // skip replacementStart
        dbus->message_iter_next(&iter);
        if (dbus->message_iter_get_arg_type(&iter) == DBUS_TYPE_INT32) {
            dbus->message_iter_get_basic(&iter, &chars);
        }
        if (dbus->message_iter_get_arg_type(&iter) == DBUS_TYPE_INT32) {
            dbus->message_iter_get_basic(&iter, &cursor);
        }

        // FIXME: (SDL_GetHintBoolean(SDL_HINT_IME_SUPPORT_EXTENDED_TEXT, SDL_FALSE)) {
        if (text) {
            SDL_SendEditingText(text, cursor, chars);
            SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Sent preedit text: %s", text);
        } else {
            SDL_SendEditingText("", 0, 0);
        }
        SDL_Maliit_UpdateTextRect(NULL);
        return DBUS_HANDLER_RESULT_HANDLED;
/*
    } else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "setRedirectKeys")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Ignoring event: %s", "RedirectKeys");
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "setDetectableAutoRepeat")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Ignoring event: %s", "setDetectableAutoRepeat");
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "setGlobalCorrectionEnabled")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Ignoring event: %s", "setGlobalCorrectionEnabled");
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "setLanguage")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Ignoring event: %s", "setLanguage");
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "selection")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: event not yet handled: %s", "selection");
    } else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "pluginSettingsLoaded")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: event not yet handled: %s", "pluginSettingsLoaded");
*/
     } else {
         /*
        DBusMessageIter iter;

        if (!dbus->message_iter_init(msg, &iter)) {
            SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------: no arguments");
        } else {
            SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------: arguments:\n");
            switch (dbus->message_iter_get_arg_type(&iter)) {
                case DBUS_TYPE_STRING: {
                    char* value;
                    dbus->message_iter_get_basic(&iter, &value);
                    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------- argument: %s", value);
                    break;
                }
                case DBUS_TYPE_BOOLEAN: {
                    SDL_bool value;
                    dbus->message_iter_get_basic(&iter, &value);
                    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------ argument: %s", value ? "[TRUE]" : "[FALSE]");
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
                    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------ argument: %d", (int)value);
                    break;
                }
                case DBUS_TYPE_ARRAY:
                    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------ argument: %s", "{ARRAY}");
                    break;
                case DBUS_TYPE_STRUCT:
                    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------ argument: %s", "(STRUCT)");
                    break;
            }
        while (dbus->message_iter_next(&iter)) {
            switch (dbus->message_iter_get_arg_type(&iter)) {
                case DBUS_TYPE_STRING: {
                    char* value;
                    dbus->message_iter_get_basic(&iter, &value);
                    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------ argument: %s", value);
                    break;
                }
                case DBUS_TYPE_BOOLEAN: {
                    SDL_bool value;
                    dbus->message_iter_get_basic(&iter, &value);
                    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------ argument: %s", value ? "[TRUE]" : "[FALSE]");
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
                    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------ argument: %d", (int)value);
                    break;
                }
                case DBUS_TYPE_ARRAY:
                    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------ argument: %s", "{ARRAY}");
                    break;
                case DBUS_TYPE_STRUCT:
                    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------ argument: %s", "(STRUCT)");
                    break;
                default:
                    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------ argument: %s", "OTHER");
            }
            }
            SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------: End of arguments");
        }
        */
    }

    int mtype = dbus->message_get_type(msg);
    const char* mtype_s;
    switch (mtype) {
        case DBUS_MESSAGE_TYPE_INVALID:
            mtype_s = "------- INVALID";
            break;
        case DBUS_MESSAGE_TYPE_METHOD_CALL:
            mtype_s = "------- CALL";
            break;
        case DBUS_MESSAGE_TYPE_METHOD_RETURN:
             mtype_s ="------- RETURN";
            break;
        case DBUS_MESSAGE_TYPE_ERROR:
            mtype_s ="------- ERROR";
            break;
        case DBUS_MESSAGE_TYPE_SIGNAL:
             mtype_s ="------- SIGNAL";
            break;
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Unhandled message of type: %s", mtype_s);
    if (mtype == DBUS_MESSAGE_TYPE_METHOD_CALL) {
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------- member: %s", dbus->message_get_member(msg));
    }
    if (mtype == DBUS_MESSAGE_TYPE_ERROR) {
        const char* err =  dbus->message_get_error_name(msg);
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------- error: %s", err);
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------- sender: %s", dbus->message_get_sender(msg));
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------- path: %s",   dbus->message_get_path(msg));
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------- iface: %s",  dbus->message_get_interface(msg));
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------- dst: %s",    dbus->message_get_destination(msg));
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------- sig: %s",    dbus->message_get_signature(msg));

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void MaliitClientCallServerMethod(MaliitClient *client, const char *method)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: calling IMServer method: %s", method);

    if (Maliit_CheckConnection()) {
        if(SDL_DBus_CallVoidMethodOnConnection(client->conn, NULL, MALIIT_IMSERVER_PATH, MALIIT_IMSERVER_INTERFACE, method, DBUS_TYPE_INVALID) == SDL_FALSE) {
            SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Maliit: calling IMServer method FAILED");
        }
    }
}

static char* MaliitClientGetAddress(void)
{
    char *addr = NULL;

    addr = SDL_getenv("MALIIT_SERVER_ADDRESS");
    if (addr != NULL) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Server address set from environment");
        return SDL_strdup(addr);
    }

    SDL_DBus_QueryProperty(MALIIT_ADDRESS_SERVICE, MALIIT_ADDRESS_PATH, MALIIT_ADDRESS_INTERFACE,
                           "address", DBUS_TYPE_STRING, &addr);
    if (!addr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Maliit: Could not get Maliit server address!");
        return NULL;
    }

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

static SDL_bool Maliit_CheckConnection(void)
{
    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    if (!dbus) {
        return SDL_FALSE;
    }

    if (maliit_client.conn && dbus->connection_get_is_connected(maliit_client.conn)) {
        return SDL_TRUE;
    }

    return SDL_FALSE;
}

static void SDLCALL Maliit_SetCapabilities(void *data, const char *name, const char *old_val,
                                         const char *internal_editing)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Maliit_SetCapabilities: name %s, old: %s, internal: %s",
                        name, old_val, internal_editing);
    // FIXME: we probably should call updateWidgetInfo here.
    // Maliit_updateWidgetInfo(SDL_bool focus)

    //SDL_DBusContext *dbus = SDL_DBus_GetContext();

    //if (IBus_CheckConnection(dbus)) {
    //    Uint32 caps = IBUS_CAP_FOCUS;
    //    if (!(internal_editing && *internal_editing == '1')) {
    //        caps |= IBUS_CAP_PREEDIT_TEXT;
    //    }

    //    SDL_DBus_CallVoidMethodOnConnection(ibus_conn, ibus_service, input_ctx_path, ibus_input_interface, "SetCapabilities",
    //                                        DBUS_TYPE_UINT32, &caps, DBUS_TYPE_INVALID);
    //}
}



SDL_bool SDL_Maliit_Init(void)
{
    SDL_DBusContext *dbus;
    DBusConnection *conn;
    char* addr;

    DBusObjectPathVTable vtable;
    SDL_zero(vtable);
    vtable.message_function = &DBus_MessageFilter;
    DBusObjectPathVTable vtable_local;
    SDL_zero(vtable_local);
    vtable_local.message_function = &DBus_Disconnect;


    SDL_LogVerbose(SDL_LOG_CATEGORY_INPUT, "Maliit IME: Init");

    maliit_client.active = SDL_FALSE;

    maliit_client.cursor_rect.x = -1;
    maliit_client.cursor_rect.y = -1;
    maliit_client.cursor_rect.w = 0;
    maliit_client.cursor_rect.h = 0;

    addr = MaliitClientGetAddress();

    if(!addr) {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Maliit: Init: Could not get Server address.");
        return SDL_FALSE;
    }

    dbus = SDL_DBus_GetContext();
    if (!dbus) {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Maliit: Init: Could not connect to DBus");
        return SDL_FALSE;
    }

    conn = dbus->connection_open_private(addr, NULL);
    if (!conn) {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Maliit: Init: Could not open connection");
        return SDL_FALSE;
    }
    SDL_free(addr);

    if (dbus->connection_get_is_connected(conn)) {
        SDL_LogVerbose(SDL_LOG_CATEGORY_INPUT, "Maliit: Init: connection established.");
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Maliit: Init: connection could not be established.");
        return SDL_FALSE;
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Init: setting up message filter");
    dbus->connection_flush(conn);

    SDL_AddHintCallback(SDL_HINT_IME_INTERNAL_EDITING, Maliit_SetCapabilities, NULL);

    char matchstr[128];
    (void)SDL_snprintf(matchstr, sizeof(matchstr), "type='signal',interface='%s'", MALIIT_IMCONTEXT_INTERFACE);
    dbus->bus_add_match(conn, matchstr, NULL);
    dbus->connection_try_register_object_path(conn, MALIIT_IMCONTEXT_PATH, &vtable, dbus, NULL);
    dbus->connection_flush(conn);

    // Listen on DBUS_LOCAL_PATH DBUS_LOCAL_INTERFACE for Signal  "Disconnected"
    (void)SDL_snprintf(matchstr, sizeof(matchstr), "type='signal', member='%s', path='%s', interface='%s'",
                                "Disconnected", DBUS_LOCAL_PATH, DBUS_LOCAL_INTERFACE);
    dbus->bus_add_match(conn, matchstr, NULL);
    dbus->connection_try_register_object_path(conn, DBUS_LOCAL_PATH, &vtable_local, dbus, NULL);
    dbus->connection_flush(conn);
    maliit_client.conn = conn;

    SDL_Maliit_SetFocus(SDL_GetKeyboardFocus() != NULL);
    SDL_Maliit_UpdateTextRect(NULL);

    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Init: done");
    return SDL_TRUE;
}

void SDL_Maliit_Quit(void)
{
    SDL_DBusContext *dbus;
    SDL_LogVerbose(SDL_LOG_CATEGORY_INPUT, "Maliit IME: Quit");
    dbus = SDL_DBus_GetContext();
    if (maliit_client.conn) {
        dbus->connection_close(maliit_client.conn);
        dbus->connection_unref(maliit_client.conn);
    }
    dbus = NULL;
    maliit_client.conn = NULL;
    SDL_memset(&maliit_client.cursor_rect, 0, sizeof(maliit_client.cursor_rect));
    SDL_DelHintCallback(SDL_HINT_IME_INTERNAL_EDITING, Maliit_SetCapabilities, NULL);
}

void SDL_Maliit_SetFocus(SDL_bool focused)
{
    SDL_LogVerbose(SDL_LOG_CATEGORY_INPUT, "Maliit IME: SetFocus");

    if (Maliit_CheckConnection()) {

        if (focused) {
            if (!maliit_client.active) {
                SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: activating");
                MaliitClientCallServerMethod(&maliit_client, "activateContext");
                // lets assume we succeeded:
                maliit_client.active = SDL_TRUE;
                Maliit_updateOrientation(SDL_FALSE); // send the orientation change warning
            }
            SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: showing");
            MaliitClientCallServerMethod(&maliit_client, "showInputMethod");
            Maliit_updateOrientation(SDL_TRUE); // send the actual orientation change
        } else {
            SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: hiding");
            MaliitClientCallServerMethod(&maliit_client, "hideInputMethod");
        }
    }
}

void SDL_Maliit_Reset(void)
{
    SDL_LogVerbose(SDL_LOG_CATEGORY_INPUT, "Maliit IME: Reset");
    MaliitClientCallServerMethod(&maliit_client, "reset");
}

SDL_bool SDL_Maliit_ProcessKeyEvent(Uint32 keysym, Uint32 keycode, Uint8 state)
{
    // TODO: See video/wayland/SDL_waylandevents.c:1257
    return SDL_FALSE;
}

void SDL_Maliit_UpdateTextRect(const SDL_Rect *rect)
{
    SDL_LogVerbose(SDL_LOG_CATEGORY_INPUT, "Maliit IME: UpdateTextRect");
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

    if (Maliit_CheckConnection()) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: updateInputMethodArea: %d %d %d %d",
                                 x, y, cursor->w,cursor->h);
        if(!SDL_DBus_CallMethodOnConnection(maliit_client.conn, NULL, MALIIT_IMCONTEXT_PATH, MALIIT_IMCONTEXT_INTERFACE, "updateInputMethodArea",
                                DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32, &y, DBUS_TYPE_INT32, &cursor->w, DBUS_TYPE_INT32, &cursor->h, DBUS_TYPE_INVALID)) {
            SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Maliit: Call FAILED");
        }
    }
}

void SDL_Maliit_PumpEvents(void)
{
    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    if (Maliit_CheckConnection()) {
        dbus->connection_read_write(maliit_client.conn, 0);

        while (dbus->connection_dispatch(maliit_client.conn) == DBUS_DISPATCH_DATA_REMAINS) {
            /* Do nothing, actual work happens in DBus_MessageFilter */
        }
    }
}

static char *GetAppName(void)
{
/* FIXME: actually, if using wayland, video/wayland/SDL_waylandvideo.c::get_classname
 * has already done pretty much what happens below.
       char *wmclass;
       SDL_WaylandOutputData *wl_output;
       SDL_VideoData *wl_video;
       wl_output = SDL_GetDisplayForWindow(0)->driverdata;
       if (wl_output) {
           wl_video = wl_output->videodata;
           if(wl_video) {
               wmclass = wl_video->classname;
               if (wmclass) {
                   return SDL_strdup(wmclass);
               }
           }
       }
*/
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

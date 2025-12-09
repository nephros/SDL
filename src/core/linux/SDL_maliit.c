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

#define DBUS_LOCAL_PATH "/org/freedesktop/DBus/Local" ;
#define DBUS_LOCAL_INTERFACE "org.freedesktop.DBus.Local" ;

#define DBUS_TIMEOUT 500

typedef struct _MaliitClient
{
    DBusConnection *conn;
    char* id;
    SDL_Rect cursor_rect;
} MaliitClient;

static MaliitClient maliit_client;

static char *GetAppName(void);

static SDL_bool Maliit_CheckConnection(void);

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

// FIXME: Actually handle the whole message for maliit!
    dbus->message_iter_init(msg, &iter);
    if (dbus->message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
        dbus->message_iter_get_basic(&sub, &subtext);
        if (subtext && *subtext) {
            text_bytes += SDL_strlen(subtext);
            return text_bytes;
        }
    }

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
        orientation = 270;
    }
    if (o == SDL_ORIENTATION_LANDSCAPE_FLIPPED) {
        orientation = 90;
    }
    if (Maliit_CheckConnection()) {
        if(!SDL_DBus_CallVoidMethodOnConnection(maliit_client.conn, NULL, MALIIT_IMSERVER_PATH, MALIIT_IMSERVER_INTERFACE, "appOrientationChanged",
                                DBUS_TYPE_INT32, &orientation, DBUS_TYPE_INVALID)) {
            SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Maliit: Call FAILED");
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

    const char* iface  = dbus->message_get_interface(msg);
    const char* member = dbus->message_get_member(msg);
    const char* sig    = dbus->message_get_signature(msg);
    SDL_bool for_us = (iface) && strcmp(iface, MALIIT_IMCONTEXT_INTERFACE)
                  && (dbus->message_get_type(msg) != DBUS_MESSAGE_TYPE_INVALID)
                  && (dbus->message_get_type(msg) != DBUS_MESSAGE_TYPE_ERROR)
                  && (dbus->message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_RETURN) ;

    if (!for_us) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT,
            "Maliit: ignoring DBus message not intended for us:\n\tpath:%s\n\tiface:%s\n\tmember:%s\n",
            dbus->message_get_path(msg),
            dbus->message_get_interface(msg),
            dbus->message_get_member(msg)
            );
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    /*
     * ***** Context Messages *****
     */
    if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "activationLostEvent")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: got a DBus message: %s", "activationLostEvent");
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "imInitiatedHide")) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: got a DBus message: %s", "imInitiatedHide");
        SDL_SendEditingText("", 0, 0);
        return DBUS_HANDLER_RESULT_HANDLED;
    //} else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "commitString")) {
    } else if ( (member) && (sig)
        && strcmp(member, "commitString") == 0
        && strcmp(sig, "siii") == 0) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: got a DBus message: %s", "commitString");

        // siii
        //commitString [dbus.String('asdffg'), dbus.Int32(0), dbus.Int32(0), dbus.Int32(-1)]
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
    //} else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "updatePreedit")) {
    } else if ( (member) && (sig)
        && strcmp(member, "updatePreedit") == 0
        && strcmp(sig, "sa(iii)iii") == 0 ) {
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
    } else if (dbus->message_is_signal(msg, MALIIT_IMCONTEXT_INTERFACE, "updateInputMethodArea")) {
         // updateInputMethodArea [dbus.Int32(372), dbus.Int32(0), dbus.Int32(348), dbus.Int32(1600)]
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: got a DBus message: %s", "updateInoutMethodArea");
        DBusMessageIter iter;
        SDL_Rect input_rect;
        dbus->message_iter_init(msg, &iter);
        if (dbus->message_iter_get_arg_type(&iter) == DBUS_TYPE_INT32) {
            dbus->message_iter_get_basic(&iter, &input_rect.x);
        }
        dbus->message_iter_next(&iter);
        if (dbus->message_iter_get_arg_type(&iter) == DBUS_TYPE_INT32) {
            dbus->message_iter_get_basic(&iter, &input_rect.y);
        }
        dbus->message_iter_next(&iter);
        if (dbus->message_iter_get_arg_type(&iter) == DBUS_TYPE_INT32) {
            dbus->message_iter_get_basic(&iter, &input_rect.h);
        }
        dbus->message_iter_next(&iter);
        if (dbus->message_iter_get_arg_type(&iter) == DBUS_TYPE_INT32) {
            dbus->message_iter_get_basic(&iter, &input_rect.w);
        }

        SDL_Maliit_UpdateTextRect(&input_rect);
        return DBUS_HANDLER_RESULT_HANDLED;
/*
setLanguage [dbus.String('')]
setGlobalCorrectionEnabled [dbus.Boolean(False)]
setRedirectKeys [dbus.Boolean(False)]
setDetectableAutoRepeat [dbus.Boolean(False)]
updateInputMethodArea [dbus.Int32(372), dbus.Int32(0), dbus.Int32(348), dbus.Int32(1600)]
updatePreedit [dbus.String('asdffg'), dbus.Array([dbus.Struct((dbus.Int32(0), dbus.Int32(6), dbus.Int32(0)), signature=None)], signature=dbus.Signature('(iii)')), dbus.Int32(0), dbus.Int32(0), dbus.Int32(-1)]
commitString [dbus.String('asdffg'), dbus.Int32(0), dbus.Int32(0), dbus.Int32(-1)]
keyEvent [dbus.Int32(6), dbus.Int32(16777220), dbus.Int32(0), dbus.String('\r'), dbus.Boolean(False), dbus.Int32(1), dbus.Byte(0)]
updateInputMethodArea [dbus.Int32(0), dbus.Int32(0), dbus.Int32(0), dbus.Int32(0)]
imInitiatedHide []
*/

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
     } else {
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
    if (mtype == DBUS_MESSAGE_TYPE_ERROR) {
        const char* err =  dbus->message_get_error_name(msg);
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------- error: %s", err);
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------- sender: %s", dbus->message_get_sender(msg));
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------- path: %s",   dbus->message_get_path(msg));
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------- iface: %s",  dbus->message_get_interface(msg));
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------- dst: %s",    dbus->message_get_destination(msg));
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------- member: %s", dbus->message_get_member(msg));
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "------- sig: %s",    dbus->message_get_signature(msg));


    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void MaliitClientCallServerMethod(MaliitClient *client, const char *method)
{
    if (!Maliit_CheckConnection()) {
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

    addr = SDL_getenv("MALIIT_SERVER_ADDRESS");
    if (addr != NULL) {
        SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Server address set from environment");
        return SDL_strdup(addr);
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


SDL_bool SDL_Maliit_Init(void)
{
    SDL_DBusContext *dbus;
    DBusConnection *conn;
    char* addr;

    DBusObjectPathVTable vtable;

    SDL_zero(vtable);
    vtable.message_function = &DBus_MessageFilter;

    SDL_LogVerbose(SDL_LOG_CATEGORY_INPUT, "Maliit: Init");

    maliit_client.cursor_rect.x = -1;
    maliit_client.cursor_rect.y = -1;
    maliit_client.cursor_rect.w = 0;
    maliit_client.cursor_rect.h = 0;

    addr = MaliitClientGetAddress();
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: connecting via address %s", addr);

    if(!addr) {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Maliit: Could not get Server address.");
        return SDL_FALSE;
    }

    dbus = SDL_DBus_GetContext();
    if (!dbus) {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Maliit: Could not connect to DBus");
        return SDL_FALSE;
    }

    conn = dbus->connection_open_private(addr, NULL);
    if (!conn) {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Maliit: Could not open connection");
        return SDL_FALSE;
    }
    SDL_free(addr);

    if (dbus->connection_get_is_connected(conn)) {
        SDL_LogVerbose(SDL_LOG_CATEGORY_INPUT, "Maliit: connection established.");
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Maliit: connection could not be established.");
        return SDL_FALSE;
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: setting up message filter");
    dbus->connection_flush(conn);

    //dbus->connection_ref(conn);

    char matchstr[128];
    (void)SDL_snprintf(matchstr, sizeof(matchstr), "type='signal',interface='%s'", MALIIT_IMCONTEXT_INTERFACE);
    dbus->bus_add_match(conn, matchstr, NULL);
    dbus->connection_try_register_object_path(conn, MALIIT_IMCONTEXT_PATH, &vtable, dbus, NULL);
    dbus->connection_flush(conn);



    maliit_client.conn = conn;

    SDL_Maliit_SetFocus(SDL_GetKeyboardFocus() != NULL);
    SDL_Maliit_UpdateTextRect(NULL);

    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Init done");
    return SDL_TRUE;
}

void SDL_Maliit_Quit(void)
{
    SDL_DBusContext *dbus;
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: Quit");
    dbus = SDL_DBus_GetContext();
    MaliitClientCallServerMethod(&maliit_client, "hideInputMethod");
    if (maliit_client.conn) {
        dbus->connection_close(maliit_client.conn);
        dbus->connection_unref(maliit_client.conn);
    }
    dbus = NULL;
    maliit_client.conn = NULL;
}

void SDL_Maliit_SetFocus(SDL_bool focused)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: SetFocus");

    if (Maliit_CheckConnection()) {
        Maliit_updateOrientation();

        if (focused) {
            SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: activating");
            MaliitClientCallServerMethod(&maliit_client, "activateContext");
            Maliit_updateWidgetInfo(focused);
            //MaliitClientCallServerMethod(&maliit_client, "appOrientationChanged"); // orientation, i 270
            SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: showing");
            MaliitClientCallServerMethod(&maliit_client, "showInputMethod");
        } else {
            SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Maliit: de-activating");
            MaliitClientCallServerMethod(&maliit_client, "hideInputMethod");
        }
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
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Maliit: No connection!");
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
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Maliit: No connection!");
        return;
    }

    if(!SDL_DBus_CallMethodOnConnection(maliit_client.conn, NULL, MALIIT_IMCONTEXT_PATH, MALIIT_IMCONTEXT_INTERFACE, "updateInputMethodArea",
                            DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32, &y, DBUS_TYPE_INT32, &cursor->w, DBUS_TYPE_INT32, &cursor->h, DBUS_TYPE_INVALID)) {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "Maliit: Call FAILED");
    };
}

void SDL_Maliit_PumpEvents(void)
{
    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    if (Maliit_CheckConnection()) {
        dbus->connection_read_write(maliit_client.conn, 0);

        while (dbus->connection_dispatch(maliit_client.conn) == DBUS_DISPATCH_DATA_REMAINS) {
            /* Do nothing, actual work happens in IBus_MessageHandler */
        }
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

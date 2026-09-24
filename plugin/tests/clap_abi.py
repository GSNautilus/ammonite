"""The CLAP ABI (the parts the plugin tests use), as ctypes structures."""
import ctypes as C


# ------------------------------------------------------------- CLAP ABI
class Version(C.Structure):
    _fields_ = [("major", C.c_uint32), ("minor", C.c_uint32), ("revision", C.c_uint32)]


class Descriptor(C.Structure):
    _fields_ = [("clap_version", Version), ("id", C.c_char_p), ("name", C.c_char_p),
                ("vendor", C.c_char_p), ("url", C.c_char_p), ("manual_url", C.c_char_p),
                ("support_url", C.c_char_p), ("version", C.c_char_p),
                ("description", C.c_char_p), ("features", C.POINTER(C.c_char_p))]


class Host(C.Structure):
    pass


HOST_GET_EXT = C.CFUNCTYPE(C.c_void_p, C.POINTER(Host), C.c_char_p)
HOST_REQ = C.CFUNCTYPE(None, C.POINTER(Host))
Host._fields_ = [("clap_version", Version), ("host_data", C.c_void_p), ("name", C.c_char_p),
                 ("vendor", C.c_char_p), ("url", C.c_char_p), ("version", C.c_char_p),
                 ("get_extension", HOST_GET_EXT), ("request_restart", HOST_REQ),
                 ("request_process", HOST_REQ), ("request_callback", HOST_REQ)]


class AudioBuffer(C.Structure):
    _fields_ = [("data32", C.POINTER(C.POINTER(C.c_float))), ("data64", C.c_void_p),
                ("channel_count", C.c_uint32), ("latency", C.c_uint32), ("constant_mask", C.c_uint64)]


class EventHeader(C.Structure):
    _fields_ = [("size", C.c_uint32), ("time", C.c_int32), ("space_id", C.c_uint16),
                ("type", C.c_uint16), ("flags", C.c_uint32)]


class ParamValueEvent(C.Structure):
    _fields_ = [("header", EventHeader), ("param_id", C.c_uint32), ("cookie", C.c_void_p),
                ("note_id", C.c_int32), ("port_index", C.c_int16), ("channel", C.c_int16),
                ("key", C.c_int16), ("value", C.c_double)]


class Transport(C.Structure):
    _fields_ = [("header", EventHeader), ("flags", C.c_uint32), ("song_pos_beats", C.c_int64),
                ("song_pos_seconds", C.c_int64), ("tempo", C.c_double), ("tempo_inc", C.c_double),
                ("loop_start_beats", C.c_int64), ("loop_end_beats", C.c_int64),
                ("loop_start_seconds", C.c_int64), ("loop_end_seconds", C.c_int64),
                ("bar_start", C.c_int64), ("bar_number", C.c_int32), ("tsig_num", C.c_uint16),
                ("tsig_denom", C.c_uint16)]


CLAP_EVENT_TRANSPORT = 9
HAS_TEMPO, HAS_BEATS, HAS_TSIG, IS_PLAYING = 1, 2, 8, 16
BEATTIME = 1 << 31


class InEvents(C.Structure):
    pass


IN_SIZE = C.CFUNCTYPE(C.c_uint32, C.POINTER(InEvents))
IN_GET = C.CFUNCTYPE(C.c_void_p, C.POINTER(InEvents), C.c_uint32)
InEvents._fields_ = [("ctx", C.c_void_p), ("size", IN_SIZE), ("get", IN_GET)]


class OutEvents(C.Structure):
    pass


OUT_PUSH = C.CFUNCTYPE(C.c_bool, C.POINTER(OutEvents), C.c_void_p)
OutEvents._fields_ = [("ctx", C.c_void_p), ("try_push", OUT_PUSH)]


class Process(C.Structure):
    _fields_ = [("steady_time", C.c_int64), ("frames_count", C.c_uint32), ("transport", C.c_void_p),
                ("audio_inputs", C.c_void_p), ("audio_outputs", C.POINTER(AudioBuffer)),
                ("audio_inputs_count", C.c_uint32), ("audio_outputs_count", C.c_uint32),
                ("in_events", C.POINTER(InEvents)), ("out_events", C.POINTER(OutEvents))]


class Plugin(C.Structure):
    pass


PP = C.POINTER(Plugin)
Plugin._fields_ = [("desc", C.POINTER(Descriptor)), ("plugin_data", C.c_void_p),
                   ("init", C.CFUNCTYPE(C.c_bool, PP)), ("destroy", C.CFUNCTYPE(None, PP)),
                   ("activate", C.CFUNCTYPE(C.c_bool, PP, C.c_double, C.c_uint32, C.c_uint32)),
                   ("deactivate", C.CFUNCTYPE(None, PP)),
                   ("start_processing", C.CFUNCTYPE(C.c_bool, PP)),
                   ("stop_processing", C.CFUNCTYPE(None, PP)), ("reset", C.CFUNCTYPE(None, PP)),
                   ("process", C.CFUNCTYPE(C.c_int32, PP, C.POINTER(Process))),
                   ("get_extension", C.CFUNCTYPE(C.c_void_p, PP, C.c_char_p)),
                   ("on_main_thread", C.CFUNCTYPE(None, PP))]


class Factory(C.Structure):
    pass


PF = C.POINTER(Factory)
Factory._fields_ = [("get_plugin_count", C.CFUNCTYPE(C.c_uint32, PF)),
                    ("get_plugin_descriptor", C.CFUNCTYPE(C.POINTER(Descriptor), PF, C.c_uint32)),
                    ("create_plugin", C.CFUNCTYPE(PP, PF, C.POINTER(Host), C.c_char_p))]


class Entry(C.Structure):
    _fields_ = [("clap_version", Version), ("init", C.CFUNCTYPE(C.c_bool, C.c_char_p)),
                ("deinit", C.CFUNCTYPE(None)), ("get_factory", C.CFUNCTYPE(C.c_void_p, C.c_char_p))]


class ParamInfo(C.Structure):
    _fields_ = [("id", C.c_uint32), ("flags", C.c_uint32), ("cookie", C.c_void_p),
                ("name", C.c_char * 256), ("module", C.c_char * 1024), ("min_value", C.c_double),
                ("max_value", C.c_double), ("default_value", C.c_double)]


class Params(C.Structure):
    _fields_ = [("count", C.CFUNCTYPE(C.c_uint32, PP)),
                ("get_info", C.CFUNCTYPE(C.c_bool, PP, C.c_uint32, C.POINTER(ParamInfo))),
                ("get_value", C.CFUNCTYPE(C.c_bool, PP, C.c_uint32, C.POINTER(C.c_double))),
                ("value_to_text", C.CFUNCTYPE(C.c_bool, PP, C.c_uint32, C.c_double, C.c_char_p, C.c_uint32)),
                ("text_to_value", C.c_void_p),
                ("flush", C.CFUNCTYPE(None, PP, C.POINTER(InEvents), C.POINTER(OutEvents)))]


class OStream(C.Structure):
    pass


class IStream(C.Structure):
    pass


OS_WRITE = C.CFUNCTYPE(C.c_int64, C.POINTER(OStream), C.c_void_p, C.c_uint64)
IS_READ = C.CFUNCTYPE(C.c_int64, C.POINTER(IStream), C.c_void_p, C.c_uint64)
OStream._fields_ = [("ctx", C.c_void_p), ("write", OS_WRITE)]
IStream._fields_ = [("ctx", C.c_void_p), ("read", IS_READ)]


class State(C.Structure):
    _fields_ = [("save", C.CFUNCTYPE(C.c_bool, PP, C.POINTER(OStream))),
                ("load", C.CFUNCTYPE(C.c_bool, PP, C.POINTER(IStream)))]


CLAP_EVENT_PARAM_VALUE = 5


# ---- GUI (clap.gui), for the UI check
class Window(C.Structure):
    _fields_ = [("api", C.c_char_p), ("handle", C.c_void_p)]


class HostGui(C.Structure):
    pass


HG_VOID = C.CFUNCTYPE(None, C.POINTER(Host))
HG_RESIZE = C.CFUNCTYPE(C.c_bool, C.POINTER(Host), C.c_uint32, C.c_uint32)
HG_BOOL = C.CFUNCTYPE(C.c_bool, C.POINTER(Host))
HG_CLOSED = C.CFUNCTYPE(None, C.POINTER(Host), C.c_bool)
HostGui._fields_ = [("resize_hints_changed", HG_VOID), ("request_resize", HG_RESIZE),
                    ("request_show", HG_BOOL), ("request_hide", HG_BOOL), ("closed", HG_CLOSED)]


class Gui(C.Structure):
    _fields_ = [("is_api_supported", C.CFUNCTYPE(C.c_bool, PP, C.c_char_p, C.c_bool)),
                ("get_preferred_api", C.c_void_p),
                ("create", C.CFUNCTYPE(C.c_bool, PP, C.c_char_p, C.c_bool)),
                ("destroy", C.CFUNCTYPE(None, PP)),
                ("set_scale", C.CFUNCTYPE(C.c_bool, PP, C.c_double)),
                ("get_size", C.CFUNCTYPE(C.c_bool, PP, C.POINTER(C.c_uint32), C.POINTER(C.c_uint32))),
                ("can_resize", C.CFUNCTYPE(C.c_bool, PP)),
                ("get_resize_hints", C.c_void_p),
                ("adjust_size", C.CFUNCTYPE(C.c_bool, PP, C.POINTER(C.c_uint32), C.POINTER(C.c_uint32))),
                ("set_size", C.CFUNCTYPE(C.c_bool, PP, C.c_uint32, C.c_uint32)),
                ("set_parent", C.CFUNCTYPE(C.c_bool, PP, C.POINTER(Window))),
                ("set_transient", C.c_void_p),
                ("suggest_title", C.c_void_p),
                ("show", C.CFUNCTYPE(C.c_bool, PP)),
                ("hide", C.CFUNCTYPE(C.c_bool, PP))]

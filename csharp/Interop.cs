// generated code do not edit

using System.Diagnostics.CodeAnalysis;
using System.Runtime.InteropServices;

namespace WebTransportFast
{
    public partial struct wtf_context
    {
    }

    public partial struct wtf_server
    {
    }

    public partial struct wtf_session
    {
    }

    public partial struct wtf_stream
    {
    }

    [NativeTypeName("unsigned int")]
    public enum wtf_execution_profile_t : uint
    {
        WTF_EXECUTION_PROFILE_LOW_LATENCY = 0,
        WTF_EXECUTION_PROFILE_MAX_THROUGHPUT = 1,
        WTF_EXECUTION_PROFILE_REAL_TIME = 2,
        WTF_EXECUTION_PROFILE_SCAVENGER = 3,
    }

    [NativeTypeName("unsigned int")]
    public enum wtf_result_t : uint
    {
        WTF_SUCCESS = 0,
        WTF_ERROR_INVALID_PARAMETER,
        WTF_ERROR_OUT_OF_MEMORY,
        WTF_ERROR_INTERNAL,
        WTF_ERROR_CONNECTION_ABORTED,
        WTF_ERROR_STREAM_ABORTED,
        WTF_ERROR_INVALID_STATE,
        WTF_ERROR_BUFFER_TOO_SMALL,
        WTF_ERROR_NOT_FOUND,
        WTF_ERROR_REJECTED,
        WTF_ERROR_TIMEOUT,
        WTF_ERROR_TLS_HANDSHAKE_FAILED,
        WTF_ERROR_PROTOCOL_VIOLATION,
        WTF_ERROR_FLOW_CONTROL,
    }

    [NativeTypeName("unsigned int")]
    public enum wtf_log_level_t : uint
    {
        WTF_LOG_LEVEL_TRACE = 0,
        WTF_LOG_LEVEL_DEBUG = 1,
        WTF_LOG_LEVEL_INFO = 2,
        WTF_LOG_LEVEL_WARN = 3,
        WTF_LOG_LEVEL_ERROR = 4,
        WTF_LOG_LEVEL_CRITICAL = 5,
        WTF_LOG_LEVEL_NONE = 6,
    }

    [NativeTypeName("unsigned int")]
    public enum wtf_server_state_t : uint
    {
        WTF_SERVER_STOPPED,
        WTF_SERVER_STARTING,
        WTF_SERVER_LISTENING,
        WTF_SERVER_STOPPING,
    }

    [NativeTypeName("unsigned int")]
    public enum wtf_session_state_t : uint
    {
        WTF_SESSION_HANDSHAKING,
        WTF_SESSION_CONNECTED,
        WTF_SESSION_DRAINING,
        WTF_SESSION_CLOSED,
    }

    [NativeTypeName("unsigned int")]
    public enum wtf_stream_state_t : uint
    {
        WTF_STREAM_OPEN,
        WTF_STREAM_CLOSING,
        WTF_STREAM_CLOSED,
    }

    [NativeTypeName("unsigned int")]
    public enum wtf_stream_type_t : uint
    {
        WTF_STREAM_BIDIRECTIONAL = 0,
        WTF_STREAM_UNIDIRECTIONAL = 1,
    }

    [NativeTypeName("unsigned int")]
    public enum wtf_connection_decision_t : uint
    {
        WTF_CONNECTION_ACCEPT,
        WTF_CONNECTION_REJECT,
    }

    [NativeTypeName("unsigned int")]
    public enum wtf_h3_error_t : uint
    {
        WTF_H3_NO_ERROR = 0x0100,
        WTF_H3_GENERAL_PROTOCOL_ERROR = 0x0101,
        WTF_H3_INTERNAL_ERROR = 0x0102,
        WTF_H3_STREAM_CREATION_ERROR = 0x0103,
        WTF_H3_CLOSED_CRITICAL_STREAM = 0x0104,
        WTF_H3_FRAME_UNEXPECTED = 0x0105,
        WTF_H3_FRAME_ERROR = 0x0106,
        WTF_H3_EXCESSIVE_LOAD = 0x0107,
        WTF_H3_ID_ERROR = 0x0108,
        WTF_H3_SETTINGS_ERROR = 0x0109,
        WTF_H3_MISSING_SETTINGS = 0x010a,
        WTF_H3_REQUEST_REJECTED = 0x010b,
        WTF_H3_REQUEST_CANCELLED = 0x010c,
        WTF_H3_REQUEST_INCOMPLETE = 0x010d,
        WTF_H3_MESSAGE_ERROR = 0x010e,
        WTF_H3_CONNECT_ERROR = 0x010f,
        WTF_H3_VERSION_FALLBACK = 0x0110,
    }

    [NativeTypeName("unsigned int")]
    public enum wtf_datagram_send_state_t : uint
    {
        WTF_DATAGRAM_SEND_UNKNOWN = 0,
        WTF_DATAGRAM_SEND_SENT = 1,
        WTF_DATAGRAM_SEND_LOST_SUSPECTED = 2,
        WTF_DATAGRAM_SEND_LOST_DISCARDED = 3,
        WTF_DATAGRAM_SEND_ACKNOWLEDGED = 4,
        WTF_DATAGRAM_SEND_ACKNOWLEDGED_SPURIOUS = 5,
        WTF_DATAGRAM_SEND_CANCELED = 6,
    }

    [NativeTypeName("unsigned int")]
    public enum wtf_qpack_error_t : uint
    {
        WTF_QPACK_DECOMPRESSION_FAILED = 0x0200,
        WTF_QPACK_ENCODER_STREAM_ERROR = 0x0201,
        WTF_QPACK_DECODER_STREAM_ERROR = 0x0202,
    }

    [NativeTypeName("unsigned int")]
    public enum wtf_h3_datagram_error_t : uint
    {
        WTF_H3_DATAGRAM_ERROR = 0x33,
    }

    [NativeTypeName("unsigned int")]
    public enum wtf_capsule_type_t : uint
    {
        WTF_CAPSULE_DATAGRAM = 0x00,
        WTF_CAPSULE_CLOSE_WEBTRANSPORT_SESSION = 0x2843,
        WTF_CAPSULE_DRAIN_WEBTRANSPORT_SESSION = 0x78ae,
    }

    [NativeTypeName("unsigned int")]
    public enum wtf_session_event_type_t : uint
    {
        WTF_SESSION_EVENT_CONNECTED,
        WTF_SESSION_EVENT_DISCONNECTED,
        WTF_SESSION_EVENT_DRAINING,
        WTF_SESSION_EVENT_STREAM_OPENED,
        WTF_SESSION_EVENT_DATAGRAM_SEND_STATE_CHANGE,
        WTF_SESSION_EVENT_DATAGRAM_RECEIVED,
    }

    [NativeTypeName("unsigned int")]
    public enum wtf_stream_event_type_t : uint
    {
        WTF_STREAM_EVENT_DATA_RECEIVED,
        WTF_STREAM_EVENT_SEND_COMPLETE,
        WTF_STREAM_EVENT_PEER_CLOSED,
        WTF_STREAM_EVENT_CLOSED,
        WTF_STREAM_EVENT_ABORTED,
    }

    public unsafe partial struct wtf_buffer_t
    {
        [NativeTypeName("uint32_t")]
        public uint length;

        [NativeTypeName("uint8_t *")]
        public byte* data;
    }

    public unsafe partial struct wtf_http_header_t
    {
        [NativeTypeName("const char *")]
        public sbyte* name;

        [NativeTypeName("const char *")]
        public sbyte* value;
    }

    public unsafe partial struct wtf_connection_request_t
    {
        [NativeTypeName("const char *")]
        public sbyte* origin;

        [NativeTypeName("const char *")]
        public sbyte* path;

        [NativeTypeName("const char *")]
        public sbyte* authority;

        [NativeTypeName("const wtf_http_header_t *")]
        public wtf_http_header_t* headers;

        [NativeTypeName("size_t")]
        public nuint header_count;

        public void* peer_address;

        [NativeTypeName("size_t")]
        public nuint address_length;
    }

    public unsafe partial struct wtf_session_event_t
    {
        public wtf_session_event_type_t type;

        [NativeTypeName("wtf_session_t *")]
        public wtf_session* session;

        public void* user_context;

        [NativeTypeName("__AnonymousRecord_wtf_L235_C5")]
        public _Anonymous_e__Union Anonymous;

        [UnscopedRef]
        public ref _Anonymous_e__Union._disconnected_e__Struct disconnected
        {
            get
            {
                return ref Anonymous.disconnected;
            }
        }

        [UnscopedRef]
        public ref _Anonymous_e__Union._stream_opened_e__Struct stream_opened
        {
            get
            {
                return ref Anonymous.stream_opened;
            }
        }

        [UnscopedRef]
        public ref _Anonymous_e__Union._datagram_send_state_changed_e__Struct datagram_send_state_changed
        {
            get
            {
                return ref Anonymous.datagram_send_state_changed;
            }
        }

        [UnscopedRef]
        public ref _Anonymous_e__Union._datagram_received_e__Struct datagram_received
        {
            get
            {
                return ref Anonymous.datagram_received;
            }
        }

        [StructLayout(LayoutKind.Explicit)]
        public partial struct _Anonymous_e__Union
        {
            [FieldOffset(0)]
            [NativeTypeName("__AnonymousRecord_wtf_L236_C9")]
            public _disconnected_e__Struct disconnected;

            [FieldOffset(0)]
            [NativeTypeName("__AnonymousRecord_wtf_L241_C9")]
            public _stream_opened_e__Struct stream_opened;

            [FieldOffset(0)]
            [NativeTypeName("__AnonymousRecord_wtf_L246_C9")]
            public _datagram_send_state_changed_e__Struct datagram_send_state_changed;

            [FieldOffset(0)]
            [NativeTypeName("__AnonymousRecord_wtf_L252_C9")]
            public _datagram_received_e__Struct datagram_received;

            public unsafe partial struct _disconnected_e__Struct
            {
                [NativeTypeName("uint32_t")]
                public uint error_code;

                [NativeTypeName("const char *")]
                public sbyte* reason;
            }

            public unsafe partial struct _stream_opened_e__Struct
            {
                [NativeTypeName("wtf_stream_t *")]
                public wtf_stream* stream;

                public wtf_stream_type_t stream_type;
            }

            public unsafe partial struct _datagram_send_state_changed_e__Struct
            {
                public wtf_buffer_t* buffers;

                [NativeTypeName("uint32_t")]
                public uint buffer_count;

                public wtf_datagram_send_state_t state;
            }

            public unsafe partial struct _datagram_received_e__Struct
            {
                [NativeTypeName("const uint32_t")]
                public uint length;

                [NativeTypeName("const uint8_t *")]
                public byte* data;
            }
        }
    }

    public unsafe partial struct wtf_stream_event_t
    {
        public wtf_stream_event_type_t type;

        [NativeTypeName("wtf_stream_t *")]
        public wtf_stream* stream;

        public void* user_context;

        [NativeTypeName("__AnonymousRecord_wtf_L265_C5")]
        public _Anonymous_e__Union Anonymous;

        [UnscopedRef]
        public ref _Anonymous_e__Union._data_received_e__Struct data_received
        {
            get
            {
                return ref Anonymous.data_received;
            }
        }

        [UnscopedRef]
        public ref _Anonymous_e__Union._send_complete_e__Struct send_complete
        {
            get
            {
                return ref Anonymous.send_complete;
            }
        }

        [UnscopedRef]
        public ref _Anonymous_e__Union._aborted_e__Struct aborted
        {
            get
            {
                return ref Anonymous.aborted;
            }
        }

        [StructLayout(LayoutKind.Explicit)]
        public partial struct _Anonymous_e__Union
        {
            [FieldOffset(0)]
            [NativeTypeName("__AnonymousRecord_wtf_L266_C9")]
            public _data_received_e__Struct data_received;

            [FieldOffset(0)]
            [NativeTypeName("__AnonymousRecord_wtf_L272_C9")]
            public _send_complete_e__Struct send_complete;

            [FieldOffset(0)]
            [NativeTypeName("__AnonymousRecord_wtf_L278_C9")]
            public _aborted_e__Struct aborted;

            public unsafe partial struct _data_received_e__Struct
            {
                public wtf_buffer_t* buffers;

                [NativeTypeName("uint32_t")]
                public uint buffer_count;

                [NativeTypeName("_Bool")]
                public byte fin;
            }

            public unsafe partial struct _send_complete_e__Struct
            {
                public wtf_buffer_t* buffers;

                [NativeTypeName("uint32_t")]
                public uint buffer_count;

                [NativeTypeName("_Bool")]
                public byte cancelled;
            }

            public partial struct _aborted_e__Struct
            {
                [NativeTypeName("uint32_t")]
                public uint error_code;
            }
        }
    }

    public unsafe partial struct wtf_error_details_t
    {
        [NativeTypeName("uint32_t")]
        public uint error_code;

        [NativeTypeName("const char *")]
        public sbyte* description;

        [NativeTypeName("_Bool")]
        public byte is_application_error;

        [NativeTypeName("_Bool")]
        public byte is_transport_error;

        [NativeTypeName("_Bool")]
        public byte is_protocol_error;
    }

    [NativeTypeName("unsigned int")]
    public enum wtf_certificate_type_t : uint
    {
        WTF_CERT_TYPE_NONE,
        WTF_CERT_TYPE_FILE,
        WTF_CERT_TYPE_FILE_PROTECTED,
        WTF_CERT_TYPE_HASH,
        WTF_CERT_TYPE_HASH_STORE,
        WTF_CERT_TYPE_CONTEXT,
        WTF_CERT_TYPE_PKCS12,
    }

    public unsafe partial struct wtf_certificate_config_t
    {
        public wtf_certificate_type_t cert_type;

        [NativeTypeName("__AnonymousRecord_wtf_L340_C5")]
        public _cert_data_e__Union cert_data;

        [NativeTypeName("const char *")]
        public sbyte* principal;

        [NativeTypeName("const char *")]
        public sbyte* ca_cert_file;

        [StructLayout(LayoutKind.Explicit)]
        public unsafe partial struct _cert_data_e__Union
        {
            [FieldOffset(0)]
            [NativeTypeName("__AnonymousRecord_wtf_L342_C9")]
            public _file_e__Struct file;

            [FieldOffset(0)]
            [NativeTypeName("__AnonymousRecord_wtf_L348_C9")]
            public _protected_file_e__Struct protected_file;

            [FieldOffset(0)]
            [NativeTypeName("__AnonymousRecord_wtf_L355_C9")]
            public _hash_e__Struct hash;

            [FieldOffset(0)]
            [NativeTypeName("__AnonymousRecord_wtf_L360_C9")]
            public _hash_store_e__Struct hash_store;

            [FieldOffset(0)]
            [NativeTypeName("__AnonymousRecord_wtf_L366_C9")]
            public _pkcs12_e__Struct pkcs12;

            [FieldOffset(0)]
            public void* context;

            public unsafe partial struct _file_e__Struct
            {
                [NativeTypeName("const char *")]
                public sbyte* cert_path;

                [NativeTypeName("const char *")]
                public sbyte* key_path;
            }

            public unsafe partial struct _protected_file_e__Struct
            {
                [NativeTypeName("const char *")]
                public sbyte* cert_path;

                [NativeTypeName("const char *")]
                public sbyte* key_path;

                [NativeTypeName("const char *")]
                public sbyte* password;
            }

            public unsafe partial struct _hash_e__Struct
            {
                [NativeTypeName("const char *")]
                public sbyte* thumbprint;
            }

            public unsafe partial struct _hash_store_e__Struct
            {
                [NativeTypeName("const char *")]
                public sbyte* thumbprint;

                [NativeTypeName("const char *")]
                public sbyte* store_name;
            }

            public unsafe partial struct _pkcs12_e__Struct
            {
                [NativeTypeName("const void *")]
                public void* data;

                [NativeTypeName("size_t")]
                public nuint data_size;

                [NativeTypeName("const char *")]
                public sbyte* password;
            }
        }
    }

    public unsafe partial struct wtf_server_config_t
    {
        [NativeTypeName("const char *")]
        public sbyte* host;

        [NativeTypeName("uint16_t")]
        public ushort port;

        public wtf_certificate_config_t* cert_config;

        [NativeTypeName("uint32_t")]
        public uint max_sessions_per_connection;

        [NativeTypeName("uint32_t")]
        public uint max_streams_per_session;

        [NativeTypeName("uint64_t")]
        public ulong max_data_per_session;

        [NativeTypeName("uint32_t")]
        public uint idle_timeout_ms;

        [NativeTypeName("uint32_t")]
        public uint handshake_timeout_ms;

        [NativeTypeName("_Bool")]
        public byte enable_0rtt;

        [NativeTypeName("_Bool")]
        public byte enable_migration;

        [NativeTypeName("wtf_connection_validator_t")]
        public delegate* unmanaged[Cdecl]<wtf_connection_request_t*, void*, wtf_connection_decision_t> connection_validator;

        [NativeTypeName("wtf_session_callback_t")]
        public delegate* unmanaged[Cdecl]<wtf_session_event_t*, void> session_callback;

        public void* user_context;
    }

    public unsafe partial struct wtf_context_config_t
    {
        public wtf_log_level_t log_level;

        [NativeTypeName("wtf_log_callback_t")]
        public delegate* unmanaged[Cdecl]<wtf_log_level_t, sbyte*, sbyte*, int, sbyte*, void*, void> log_callback;

        public void* log_user_context;

        [NativeTypeName("uint32_t")]
        public uint worker_thread_count;

        [NativeTypeName("_Bool")]
        public byte enable_load_balancing;

        [NativeTypeName("_Bool")]
        public byte disable_encryption;

        public wtf_execution_profile_t execution_profile;
    }

    public unsafe partial struct wtf_version_info_t
    {
        [NativeTypeName("uint32_t")]
        public uint major;

        [NativeTypeName("uint32_t")]
        public uint minor;

        [NativeTypeName("uint32_t")]
        public uint patch;

        [NativeTypeName("const char *")]
        public sbyte* version;
    }

    public static unsafe partial class Methods
    {
        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_version_info_t* wtf_get_version();

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_result_t wtf_context_create([NativeTypeName("const wtf_context_config_t *")] wtf_context_config_t* config, [NativeTypeName("wtf_context_t **")] wtf_context** context);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern void wtf_context_destroy([NativeTypeName("wtf_context_t *")] wtf_context* context);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_result_t wtf_context_set_log_level([NativeTypeName("wtf_context_t *")] wtf_context* context, wtf_log_level_t level);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_result_t wtf_server_create([NativeTypeName("wtf_context_t *")] wtf_context* context, [NativeTypeName("const wtf_server_config_t *")] wtf_server_config_t* config, [NativeTypeName("wtf_server_t **")] wtf_server** server);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_result_t wtf_server_start([NativeTypeName("wtf_server_t *")] wtf_server* server);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_result_t wtf_server_stop([NativeTypeName("wtf_server_t *")] wtf_server* server);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_server_state_t wtf_server_get_state([NativeTypeName("wtf_server_t *")] wtf_server* server);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern void wtf_server_destroy([NativeTypeName("wtf_server_t *")] wtf_server* server);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_result_t wtf_session_close([NativeTypeName("wtf_session_t *")] wtf_session* session, [NativeTypeName("uint32_t")] uint error_code, [NativeTypeName("const char *")] sbyte* reason);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_result_t wtf_session_drain([NativeTypeName("wtf_session_t *")] wtf_session* session);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_result_t wtf_session_send_datagram([NativeTypeName("wtf_session_t *")] wtf_session* session, [NativeTypeName("const wtf_buffer_t *")] wtf_buffer_t* buffers, [NativeTypeName("uint32_t")] uint buffer_count);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_result_t wtf_session_create_stream([NativeTypeName("wtf_session_t *")] wtf_session* session, wtf_stream_type_t type, [NativeTypeName("wtf_stream_t **")] wtf_stream** stream);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_session_state_t wtf_session_get_state([NativeTypeName("wtf_session_t *")] wtf_session* session);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_result_t wtf_session_get_peer_address([NativeTypeName("wtf_session_t *")] wtf_session* session, void* address_buffer, [NativeTypeName("size_t *")] nuint* buffer_size);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern void wtf_session_set_context([NativeTypeName("wtf_session_t *")] wtf_session* session, void* user_context);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern void* wtf_session_get_context([NativeTypeName("wtf_session_t *")] wtf_session* session);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_result_t wtf_stream_send([NativeTypeName("wtf_stream_t *")] wtf_stream* stream, [NativeTypeName("const wtf_buffer_t *")] wtf_buffer_t* buffers, [NativeTypeName("uint32_t")] uint buffer_count, [NativeTypeName("_Bool")] byte fin);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_result_t wtf_stream_close([NativeTypeName("wtf_stream_t *")] wtf_stream* stream);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_result_t wtf_stream_abort([NativeTypeName("wtf_stream_t *")] wtf_stream* stream, [NativeTypeName("uint32_t")] uint error_code);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_result_t wtf_stream_get_id([NativeTypeName("wtf_stream_t *")] wtf_stream* stream, [NativeTypeName("uint64_t *")] ulong* stream_id);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern void wtf_stream_set_callback([NativeTypeName("wtf_stream_t *")] wtf_stream* stream, [NativeTypeName("wtf_stream_callback_t")] delegate* unmanaged[Cdecl]<wtf_stream_event_t*, void> callback);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern void wtf_stream_set_context([NativeTypeName("wtf_stream_t *")] wtf_stream* stream, void* user_context);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern void* wtf_stream_get_context([NativeTypeName("wtf_stream_t *")] wtf_stream* stream);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_stream_type_t wtf_stream_get_type([NativeTypeName("wtf_stream_t *")] wtf_stream* stream);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_stream_state_t wtf_stream_get_state([NativeTypeName("wtf_stream_t *")] wtf_stream* stream);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_result_t wtf_stream_set_priority([NativeTypeName("wtf_stream_t *")] wtf_stream* stream, [NativeTypeName("uint16_t")] ushort priority);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_result_t wtf_stream_set_receive_enabled([NativeTypeName("wtf_stream_t *")] wtf_stream* stream, [NativeTypeName("_Bool")] byte enabled);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        [return: NativeTypeName("wtf_stream_t *")]
        public static extern wtf_stream* wtf_session_find_stream_by_id([NativeTypeName("wtf_session_t *")] wtf_session* session, [NativeTypeName("uint64_t")] ulong stream_id);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        [return: NativeTypeName("const char *")]
        public static extern sbyte* wtf_result_to_string(wtf_result_t result);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        [return: NativeTypeName("const char *")]
        public static extern sbyte* wtf_webtransport_error_to_string([NativeTypeName("uint32_t")] uint error_code);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern wtf_result_t wtf_get_error_details([NativeTypeName("uint32_t")] uint error_code, wtf_error_details_t* details);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        [return: NativeTypeName("_Bool")]
        public static extern byte wtf_is_valid_application_error([NativeTypeName("uint32_t")] uint error_code);

        [DllImport("wtf", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        [return: NativeTypeName("const char *")]
        public static extern sbyte* wtf_http3_error_to_string([NativeTypeName("uint64_t")] ulong http3_error);
    }
}

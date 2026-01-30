using System.Runtime.InteropServices;
using System.Text;
using System.Text.Unicode;

namespace WebTransportFast;

public unsafe class EchoServer
{
    const byte FALSE = 0;
    const byte TRUE = 1;

    public int port;
    public string cert;
    public string key;

    public wtf_context* g_context;
    public bool g_running = true;
    public wtf_server* g_server;

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void session_callback_delegate(wtf_session_event_t* evt);

    private static session_callback_delegate _session_callback = session_callback;

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate wtf_connection_decision_t connection_validator_delegate(wtf_connection_request_t* request,
        void* user_data);

    private static connection_validator_delegate _connection_validator = connection_validator;

    public EchoServer(int port, string cert, string key)
    {
        this.port = port;
        this.cert = cert;
        this.key = key;
    }

    static wtf_connection_decision_t connection_validator(wtf_connection_request_t* request, void* user_data)
    {
        return wtf_connection_decision_t.WTF_CONNECTION_ACCEPT;
    }

    static void session_callback(wtf_session_event_t* evt)
    {
        var session_ctx = evt->user_context;

        switch (evt->type)
        {
            case wtf_session_event_type_t.WTF_SESSION_EVENT_CONNECTED:
            {
                Console.Out.WriteLine("[SESSION] New session connected");
                break;
            }

            default:
                break;
        }
    }

    public bool Start()
    {
        // stack allocated (no pinning required)
        wtf_context_config_t context_config = new()
        {
            log_level = wtf_log_level_t.WTF_LOG_LEVEL_TRACE,
            log_callback = null,
            worker_thread_count = 4,
            enable_load_balancing = TRUE,
        };

        byte[] certPathBytes = Encoding.UTF8.GetBytes(cert + '\0');
        sbyte* certPath = stackalloc sbyte[certPathBytes.Length];
        for (int i = 0; i < certPathBytes.Length; i++)
        {
            certPath[i] = (sbyte)certPathBytes[i];
        }

        byte[] keyPathBytes = Encoding.UTF8.GetBytes(key + '\0');
        sbyte* keyPath = stackalloc sbyte[keyPathBytes.Length];
        for (int i = 0; i < keyPathBytes.Length; i++)
        {
            keyPath[i] = (sbyte)keyPathBytes[i];
        }

        var cert_config = new wtf_certificate_config_t()
        {
            cert_type = wtf_certificate_type_t.WTF_CERT_TYPE_FILE,
            cert_data = new wtf_certificate_config_t._cert_data_e__Union()
            {
                file = new wtf_certificate_config_t._cert_data_e__Union._file_e__Struct()
                {
                    cert_path = certPath,
                    key_path = keyPath,
                }
            }
        };

        wtf_server_config_t server_config = new()
        {
            port = (ushort)port,
            cert_config = &cert_config,
            session_callback =
                (delegate* unmanaged[Cdecl]<wtf_session_event_t*, void>)Marshal.GetFunctionPointerForDelegate(
                    _session_callback),
            connection_validator =
                (delegate* unmanaged[Cdecl]<wtf_connection_request_t*, void*, wtf_connection_decision_t>)Marshal
                    .GetFunctionPointerForDelegate(_connection_validator),
            max_sessions_per_connection = 32,
            max_streams_per_session = 256,
            idle_timeout_ms = 60000,
            handshake_timeout_ms = 10000,
            enable_0rtt = TRUE,
            enable_migration = TRUE,
        };

        fixed (wtf_context** g_contextPtr = &g_context)
        fixed (wtf_server** g_serverPtr = &g_server)
        {
            var status = Methods.wtf_context_create(&context_config, g_contextPtr);
            if (status != wtf_result_t.WTF_SUCCESS)
            {
                var msg = Marshal.PtrToStringAnsi((IntPtr)Methods.wtf_result_to_string(status));
                Console.Out.WriteLine($"[ERROR] Failed to create context: {msg}");
                return false;
            }

            status = Methods.wtf_server_create(g_context, &server_config, g_serverPtr);
            if (status != wtf_result_t.WTF_SUCCESS)
            {
                var msg = Marshal.PtrToStringAnsi((IntPtr)Methods.wtf_result_to_string(status));
                Console.Out.WriteLine($"[ERROR] Failed to create server: {msg}");
                Methods.wtf_context_destroy(g_context);
                return false;
            }

            status = Methods.wtf_server_start(g_server);
            if (status != wtf_result_t.WTF_SUCCESS)
            {
                var msg = Marshal.PtrToStringAnsi((IntPtr)Methods.wtf_result_to_string(status));
                Console.Out.WriteLine($"[ERROR] Failed to start server: {msg}");
                Methods.wtf_server_destroy(g_server);
                Methods.wtf_context_destroy(g_context);
                return false;
            }
        }

        return true;
    }

    public bool Stop()
    {
        var status = Methods.wtf_server_stop(g_server);
        if (status != wtf_result_t.WTF_SUCCESS)
        {
            return false;
        }
        Methods.wtf_server_destroy(g_server);
        Methods.wtf_context_destroy(g_context);
        return true;
    }
}

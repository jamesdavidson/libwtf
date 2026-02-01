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
    public delegate void stream_callback_delegate(wtf_stream_event_t* evt);

    private static stream_callback_delegate _stream_callback = stream_callback;

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
        // From machine clients these headers could include authorisation but from the browser
        // these headers will be empty. See https://github.com/w3c/webtransport/issues/263
        for (int i = 0; i < (int)request->header_count; i++)
        {
            var k = Marshal.PtrToStringAnsi((IntPtr)request->headers[i].name);
            var v = Marshal.PtrToStringAnsi((IntPtr)request->headers[i].value);
            Console.Out.WriteLine($"[CONN] Header: {k} = {v}");
        }

        return wtf_connection_decision_t.WTF_CONNECTION_ACCEPT;
    }

    static void session_callback(wtf_session_event_t* evt)
    {
        if (evt->user_context == null)
        {
            evt->user_context = (void*)new IntPtr(1);
        }

        switch (evt->type)
        {
            case wtf_session_event_type_t.WTF_SESSION_EVENT_CONNECTED:
            {
                Console.Out.WriteLine("[SESSION] New session connected {0}", (IntPtr)evt->user_context);
                break;
            }

            case wtf_session_event_type_t.WTF_SESSION_EVENT_STREAM_OPENED:
            {
                Console.Out.WriteLine("[SESSION] New stream opened on session {0}", (IntPtr)evt->user_context);

                // Get or create stream context
                var stream_ctx = Methods.wtf_stream_get_context(evt->stream_opened.stream);

                if (stream_ctx == null)
                {
                    stream_ctx = (void*) new IntPtr(1);

                    // stream_ctx = malloc(sizeof(stream_context_t));
                    // if (stream_ctx) {
                    //     stream_ctx->stream = event->stream_opened.stream;
                    //     stream_ctx->stream_id = (uint32_t)g_stats.streams_created;
                    //     stream_ctx->session = event->session;
                    //     stream_ctx->created_time = time(NULL);
                    //     stream_ctx->is_server_initiated = false;
                    //     stream_ctx->needs_welcome = false;
                    //     stream_ctx->stream_type[0] = '\0';

                        Methods.wtf_stream_set_context(evt->stream_opened.stream, stream_ctx);
                    // }
                }

                Methods.wtf_stream_set_callback(evt->stream_opened.stream,
                    (delegate* unmanaged[Cdecl]<wtf_stream_event_t*, void>)Marshal.GetFunctionPointerForDelegate(
                        _stream_callback));

                Console.Out.WriteLine("[SESSION] Stream {0} configured", (IntPtr)stream_ctx);
                break;
            }

            case wtf_session_event_type_t.WTF_SESSION_EVENT_DISCONNECTED: {
                var msg = Marshal.PtrToStringAnsi((IntPtr)evt->disconnected.reason);
                if (msg == null || msg == "") msg = "none";

                Console.Out.WriteLine("[SESSION] Session {0} disconnected (error: {1}, reason: {2})",
                    (IntPtr)evt->user_context,
                    evt->disconnected.error_code,
                    msg);

                break;
            }

            case wtf_session_event_type_t.WTF_SESSION_EVENT_DATAGRAM_RECEIVED: {
                Console.Out.WriteLine("[DATAGRAM] Received on session {0} ({1} bytes)",
                    (IntPtr)evt->user_context,
                    evt->datagram_received.length);

                ReadOnlySpan<byte> datagramData = new ReadOnlySpan<byte>(evt->datagram_received.data, (int)evt->datagram_received.length);

                var n = datagramData.Length;
                var reversedPtr = Marshal.AllocHGlobal(n);
                var reversed = (byte*)reversedPtr;
                Console.Out.WriteLine($"reversedPtr = {reversedPtr}");
                Console.Out.WriteLine($"reversed = {(IntPtr)reversed}");
                for (int i = 0; i < n; i++)
                {
                    reversed[i] = datagramData[n - i - 1];
                }

                var buffer = new wtf_buffer_t()
                {
                    data = reversed,
                    length = (uint)n,
                };

                wtf_result_t result = Methods.wtf_session_send_datagram(evt->session, &buffer,1);
                if (result == wtf_result_t.WTF_SUCCESS) {
                    Console.Out.WriteLine("[DATAGRAM] Echoed {0} bytes", n);
                } else
                {
                    var msg = Marshal.PtrToStringAnsi((IntPtr)Methods.wtf_result_to_string(result));
                    Console.Out.WriteLine("[DATAGRAM] Failed to echo: {0}", msg);
                    Marshal.FreeHGlobal((IntPtr)reversed);
                }

                break;
            }

            case wtf_session_event_type_t.WTF_SESSION_EVENT_DATAGRAM_SEND_STATE_CHANGE:
            {
                // only free buffers if resending is not going to happen as per WTF_DATAGRAM_SEND_STATE_IS_FINAL in wtf.h
                var sendState = evt->datagram_send_state_changed.state;
                var mightResend = sendState >= wtf_datagram_send_state_t.WTF_DATAGRAM_SEND_LOST_DISCARDED;
                if (!mightResend) {
                    for (var i = 0; i < evt->datagram_send_state_changed.buffer_count; i++)
                    {
                        var data = evt->datagram_send_state_changed.buffers[i].data;
                        var dataPtr = (IntPtr)data;
                        Console.Out.WriteLine($"data = {(IntPtr)data}");
                        Console.Out.WriteLine($"dataPtr = {dataPtr}");

                        if (dataPtr != null && dataPtr != IntPtr.Zero)
                        {
                            Marshal.FreeHGlobal(dataPtr);
                        }
                    }
                }

                break;
            }

            case wtf_session_event_type_t.WTF_SESSION_EVENT_DRAINING:
                Console.Out.WriteLine("[SESSION] Session {0} is draining", (IntPtr)evt->user_context);
                break;
        }
    }

    static void stream_callback(wtf_stream_event_t* evt)
    {
        switch (evt->type) {
            case wtf_stream_event_type_t.WTF_STREAM_EVENT_SEND_COMPLETE: {
                for (var i = 0; i < evt->send_complete.buffer_count; i++)
                {
                    var data = evt->send_complete.buffers[i].data;
                    var dataPtr = (IntPtr)data;
                    Console.Out.WriteLine($"data = {(IntPtr)data}");
                    Console.Out.WriteLine($"dataPtr = {dataPtr}");

                    if (dataPtr != null && dataPtr != IntPtr.Zero)
                    {
                        Marshal.FreeHGlobal(dataPtr);
                    }
                }

                break;
            }

            case wtf_stream_event_type_t.WTF_STREAM_EVENT_DATA_RECEIVED: {
                if (evt->data_received.fin == TRUE) {
                    break;
                }
                Console.Out.WriteLine("[STREAM] Data received on stream {0}", (IntPtr)evt->user_context);

                var asdf = new MemoryStream();
                var n = evt->data_received.buffer_count;
                for (var i = 0; i < n; i++)
                {
                    var buf = evt->data_received.buffers[i];
                    var span = new ReadOnlySpan<byte>(buf.data, (int)buf.length);
                    asdf.Write(span);
                }

                Console.Out.WriteLine("[STREAM] Received {0} bytes", asdf.Length);

                var responsePtr = Marshal.AllocHGlobal((int)asdf.Length);

                var buffer = new wtf_buffer_t()
                {
                    data = (byte*)responsePtr,
                    length = (uint)asdf.Length,
                };
                var status = Methods.wtf_stream_send(evt->stream, &buffer, 1, FALSE);
                if (status == wtf_result_t.WTF_SUCCESS) {
                    Console.Out.WriteLine("[STREAM] Echoed {0} bytes", asdf.Length);
                } else
                {
                    var msg = Marshal.PtrToStringAnsi((IntPtr)Methods.wtf_result_to_string(status));
                    Console.Out.WriteLine("[STREAM] Failed to echo: {0}", msg);
                    Marshal.FreeHGlobal(responsePtr);
                }

                break;
            }

            case wtf_stream_event_type_t.WTF_STREAM_EVENT_PEER_CLOSED:
                Console.Out.WriteLine("[STREAM] Stream {0} closed by peer", (IntPtr)evt->user_context);
                break;

            case wtf_stream_event_type_t.WTF_STREAM_EVENT_CLOSED:
                Console.Out.WriteLine("[STREAM] Stream {0} fully closed", (IntPtr)evt->user_context);
                // if (stream_ctx) {
                //     free(stream_ctx);
                // }
                break;

            case wtf_stream_event_type_t.WTF_STREAM_EVENT_ABORTED:
                Console.Out.WriteLine("[STREAM] Stream {0} aborted with error {1}", (IntPtr)evt->user_context, evt->aborted.error_code);
                // if (stream_ctx) {
                //     free(stream_ctx);
                // }
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

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace WebTransportFast;

public static partial class Program
{
    [LibraryImport("wtf")]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static partial IntPtr wtf_result_to_string(wtf_result_t result);

    private static string ResultToString(wtf_result_t result)
    {
        IntPtr p = wtf_result_to_string(result);
        return p == IntPtr.Zero ? string.Empty : Marshal.PtrToStringUTF8(p) ?? string.Empty;
    }

    public static void Main(string[] args)
    {
        for (int i = 0; i < 15; i++)
        {
            Console.Out.WriteLine(ResultToString((wtf_result_t)i));
        }
    }

    // Mirror the native enum layout (defaults to int).
    private enum wtf_result_t : int
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
        WTF_ERROR_FLOW_CONTROL
    }

    private enum wtf_log_level_t
    {
        WTF_LOG_LEVEL_TRACE = 0,
        WTF_LOG_LEVEL_DEBUG,
        WTF_LOG_LEVEL_INFO,
        WTF_LOG_LEVEL_WARN,
        WTF_LOG_LEVEL_ERROR,
        WTF_LOG_LEVEL_FATAL
    }

    private enum wtf_execution_profile_t
    {
        WTF_EXECUTION_PROFILE_LOW_LATENCY = 0,
        WTF_EXECUTION_PROFILE_DEFAULT,
        WTF_EXECUTION_PROFILE_THROUGHPUT,
        WTF_EXECUTION_PROFILE_SCAVENGER
    }

// //! Library context configuration
//     typedef struct {
//         wtf_log_level_t log_level;                  //! Global logging level
//         wtf_log_callback_t log_callback;            //! Custom logging callback
//         void* log_user_context;                     //! Context for log callback
//         uint32_t worker_thread_count;               //! Number of worker threads
//         bool enable_load_balancing;                 //! Enable load balancing
//         bool disable_encryption;                    //! Disable encryption for testing
//         wtf_execution_profile_t execution_profile;  //! Performance profile
//     } wtf_context_config_t;

}

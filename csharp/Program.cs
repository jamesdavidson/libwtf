using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using WebTransportFast;

Console.Out.WriteLine("asdf");

const byte FALSE = 0;
const byte TRUE = 1;

var context_config = new wtf_context_config_t
{
    log_level = wtf_log_level_t.WTF_LOG_LEVEL_TRACE,
    log_callback = null,
    worker_thread_count = 4,
    enable_load_balancing = TRUE,
};

unsafe
{
    wtf_context* context;
    var status = Methods.wtf_context_create(&context_config, &context);
    if (status != wtf_result_t.WTF_SUCCESS)
    {
        //var msg = Methods.wtf_result_to_string(status);
        Console.Out.WriteLine($"Failed to create context");
        Environment.Exit(1);
    }
    else
    {
        Console.Out.WriteLine($"Created context {status}");
    }
}

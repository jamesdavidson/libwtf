using WebTransportFast;

var server = new EchoServer(8443, "cert.pem", "key.pem");
server.Start();
CancellationTokenSource cts = new();
Console.CancelKeyPress += (_, _) => cts.Cancel();
await Task.Delay(Timeout.Infinite, cts.Token);
server.Stop();

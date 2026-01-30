using WebTransportFast;

var server = new EchoServer(8443, "keystore.p12");
server.Start();
Console.Out.WriteLine("ok");

using System.Reflection;
using clojure.lang;

// var server = new EchoServer(8443, "cert.pem", "key.pem");
// server.Start();
// CancellationTokenSource cts = new();
// Console.CancelKeyPress += (_, _) => cts.Cancel();
// await Task.Delay(Timeout.Infinite, cts.Token);
// server.Stop();
Assembly.Load("clojure.data.json");

RT.Init();

var CLOJURE_MAIN = Symbol.intern("clojure.main");
var REQUIRE = RT.var("clojure.core", "require");
var MAIN = RT.var("clojure.main", "main");

REQUIRE.invoke(CLOJURE_MAIN);
MAIN.applyTo(RT.seq(args));

~/.homebrew/Cellar/openssl@3/3.6.0
~/.homebrew/Cellar/libmsquic/2.5.6

cc -I/Users/jamesdavidson/.homebrew/Cellar/libmsquic/2.5.6/include -L/Users/jamesdavidson/.homebrew/Cellar/libmsquic/2.5.6/lib -lmsquic asdf.c

rm -rfv build ; cmake -DMSQUIC_INCLUDE_DIRS=/Users/jamesdavidson/.homebrew/Cellar/libmsquic/2.5.6/include -DWTF_USE_EXTERNAL_MSQUIC=on -B build
export LIBRARY_PATH=$LIBRARY_PATH:/Users/jamesdavidson/.homebrew/Cellar/libmsquic/2.5.6/lib
cmake --build build


export DYLD_LIBRARY_PATH=build/output:/Users/jamesdavidson/.homebrew/Cellar/libmsquic/2.5.6/lib:/Users/jamesdavidson/.homebrew/Cellar/openssl@3/3.6.0/lib
dotnet run --project csharp


dotnet tool install --global ClangSharpPInvokeGenerator --version 20.1.2.4
export DYLD_LIBRARY_PATH=/Users/jamesdavidson/.dotnet/tools/.store/clangsharppinvokegenerator/20.1.2.4/clangsharppinvokegenerator.osx-arm64/20.1.2.4/tools/any/osx-arm64
ClangSharpPInvokeGenerator --language c --file include/wtf.h --output csharp/Interop.cs --namespace WebTransportFast --additional "-isystem/Library/Developer/CommandLineTools/usr/lib/clang/17/include" --libraryPath wtf --headerFile csharp/warning.txt


export DYLD_LIBRARY_PATH=$(realpath build/output)
dotnet run --project csharp

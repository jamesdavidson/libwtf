~/.homebrew/Cellar/openssl@3/3.6.0
~/.homebrew/Cellar/libmsquic/2.5.6

cc -I/Users/jamesdavidson/.homebrew/Cellar/libmsquic/2.5.6/include -L/Users/jamesdavidson/.homebrew/Cellar/libmsquic/2.5.6/lib -lmsquic asdf.c

rm -rfv build ; cmake -DMSQUIC_INCLUDE_DIRS=/Users/jamesdavidson/.homebrew/Cellar/libmsquic/2.5.6/include -DWTF_USE_EXTERNAL_MSQUIC=on -B build
export LIBRARY_PATH=$LIBRARY_PATH:/Users/jamesdavidson/.homebrew/Cellar/libmsquic/2.5.6/lib
cmake --build build

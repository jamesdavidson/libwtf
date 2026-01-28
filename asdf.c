#include <msquic.h>

int
main (int argc, char **argv) {
  const void ** QuicApi;
  MsQuicOpenVersion(1, QuicApi);
  return 0;
}

#include <stdio.h>
#include <nodec.h>
#include <nodec-primitive.h>


/*-----------------------------------------------------------------
  Client Test
-----------------------------------------------------------------*/
const char* http_request_parts[] = {
  "GET / HTTP/1.1\r\n",
  "Host: 127.0.0.1\r\n",
  "Connection: close\r\n",
  "\r\n",
  NULL
};

void test_as_client() {
  uv_stream_t* conn = async_tcp_connect("127.0.0.1", "8080");
  {with_stream(conn) {
    const char* s;
    for (size_t i = 0; (s = http_request_parts[i]) != NULL; i++) {
      printf("write: %s\n", s);
      async_write(conn, s);
      async_wait(100);
    }
    printf("await response...\n");
    char* body = async_read_all(conn);
    {with_free(body) {
      printf("received:\n%s", body);
    }}
  }}
}

/*-----------------------------------------------------------------
  Main
-----------------------------------------------------------------*/

static void entry() {
  printf("in the main loop\n");
  test_as_client();
}

int main() {
  async_main(entry);
  return 0;
}
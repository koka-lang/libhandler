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
  write_one_char

  Writes a single character to the stream
-----------------------------------------------------------------*/

static void write_one_char(uv_stream_t* conn, const char* pch) {
  uv_buf_t buf = { 1, (char*)pch };
  async_write_buf(conn, buf); // send one byte
  async_wait(100); // put in a delay to force a new chunk
}

/*-----------------------------------------------------------------
  test_as_client2_connection

  Attempt to break the system by sending a request one byte at a
  time. Called by test_as_client2
-----------------------------------------------------------------*/

static void test_as_client2_connection(uv_stream_t* conn) {
  const char* s;
  for (size_t i = 0; (s = http_request_parts[i]) != NULL; i++)
    for (const char* pch = s; *pch; pch++)
      write_one_char(conn, pch);
  printf("await response...\n");
  char* body = async_read_all(conn);
  {with_free(body) {
    printf("received:\n%s", body);
  }}
}

/*-----------------------------------------------------------------
  test_as_client_one_byte_at_a_time
-----------------------------------------------------------------*/

void test_as_client_one_byte_at_a_time() {
  uv_stream_t* conn = async_tcp_connect("127.0.0.1", "8080");
  {with_stream(conn) {
    test_as_client2_connection(conn);
  }}
}

static long file_get_size(FILE* file) {
  const long original_position = ftell(file);
  fseek(file, 0, SEEK_END);
  const long ans = ftell(file);
  fseek(file, original_position, SEEK_SET);
  return ans;
}

/*-----------------------------------------------------------------
  test_read_file

  reads the contents of a file into an uv_buf_t
-----------------------------------------------------------------*/

static uv_buf_t read_file(const char* path) {
  uv_buf_t ans = { 0, 0 };
  FILE* file = 0;
  const errno_t rc = fopen_s(&file, path, "r");
  if (rc == 0 && file != 0) {
    const long len = file_get_size(file);
    ans = nodec_buf_alloc(len);
    fread(ans.base, ans.len, 1, file);
    fclose(file);
  }
  else if (rc != 0 && file == 0) {
    nodec_throw(123); // what to put here?
  }
  return ans;
}

/*-----------------------------------------------------------------
  test_client_file

  Reads an HTTP client request from a file and sends it to
  the server.
-----------------------------------------------------------------*/

static void test_client_file(const char* path) {
  uv_buf_t buf = read_file(path);
  {with_free(buf.base) {
    uv_stream_t* conn = async_tcp_connect("127.0.0.1", "8080");
    {with_stream(conn) {
      async_write_buf(conn, buf);
      printf("await response...\n");
      char* body = async_read_all(conn);
      {with_free(body) {
        printf("received:\n%s", body);
      }}
    }}
  }}
}

/*-----------------------------------------------------------------
  Main
-----------------------------------------------------------------*/

static void entry() {
  printf("in the main loop\n");
  //test_as_client_one_byte_at_a_time();
  const char* path = ".\\..\\..\\nodec\\test\\example1.txt";
  test_client_file(path);
}

int main() {
  async_main(entry);
  return 0;
}
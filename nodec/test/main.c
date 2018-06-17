#include <stdio.h>
#include <nodec.h>

/*-----------------------------------------------------------------
  Test files
-----------------------------------------------------------------*/
static void test_stat() {
  const char* path = "cenv.h";
  printf("stat file %s\n", path);
  uv_stat_t stat = async_stat(path);
  printf("file %s last access time: %li\n", path, stat.st_atim.tv_sec);
}

static void test_fileread() {
  const char* path = "cenv.h";
  printf("opening file: %s\n", path);
  char* contents = async_fread_full(path);
  {with_free(contents) {
    printf("read %Ii bytes from %s:\n...\n", strlen(contents), path);    
  }}
}

static void test_files() {
  test_stat();
  test_fileread();
}

/*-----------------------------------------------------------------
  Test interleave
-----------------------------------------------------------------*/

lh_value test_statx(lh_value arg) {
  test_stat();
  return lh_value_null;
}
lh_value test_filereadx(lh_value arg) {
  test_fileread();
  return lh_value_null;
}
lh_value test_filereads(lh_value arg) {
  printf("test filereads\n");
  lh_actionfun* actions[2] = { &test_filereadx, &test_statx };
  interleave(2, actions);
  return lh_value_null;
}

static void test_interleave() {
  lh_actionfun* actions[3] = { &test_filereadx, &test_statx, &test_filereads };
  interleave(3, actions);
}

/*-----------------------------------------------------------------
  TCP
-----------------------------------------------------------------*/

static void test_tcp() {
  channel_t* ch = nodec_tcp_listen_at4("127.0.0.1", 8080, 0, 0);
  {defer(channel_freev, lh_value_ptr(ch)) {
    channel_elem e = channel_receive(ch);
    printf("got a connection!\n");
    uv_stream_t* client = (uv_stream_t*)lh_ptr_value(e.data);
    uv_buf_t buf = uv_buf_init(nodec_nalloc(8*1024+1,char), 8*1024);
    ssize_t nread = async_read(client, buf, 0);
    buf.base[nread] = 0;
    fprintf(stderr,"received:%Ii bytes\n%s\n", nread, buf.base);
    nodec_free(buf.base);
    if (client != NULL) async_shutdown(client);
  }}
}

/*-----------------------------------------------------------------
  Main
-----------------------------------------------------------------*/

static void entry() {
  printf("in the main loop\n");
  //test_files();
  //test_interleave();
  test_tcp();
}


int main() {
  async_main(entry);
  return 0;
}
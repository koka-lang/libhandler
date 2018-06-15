#include <stdio.h>
#include <nodec.h>

/*-----------------------------------------------------------------
Main
-----------------------------------------------------------------*/
static void test_stat() {
  const char* path = "cenv.h";
  uv_stat_t stat = async_stat(path);
  printf("file %s last access time: %li\n", path, stat.st_atim.tv_sec);
}

static void test_fileread() {
  printf("opening file\n");
  char* contents = async_fread_full("cenv.h");
  printf("read %Ii bytes:\n%s\n", strlen(contents), contents);
  free(contents);
}

lh_value test_statx(lh_value arg) {
  test_stat();
  return lh_value_null;
}
lh_value test_filereadx(lh_value arg) {
  test_fileread();
  return lh_value_null;
}

static void test_interleave() {
  lh_actionfun* actions[3] = { &test_filereadx, &test_filereadx, &test_statx };
  interleave(3, actions);
}

static void entry() {
  printf("in the main loop\n");
  //test_stat();
  //test_fileread();
  test_interleave();
}


int main() {
  async_main(entry);
  return 0;
}
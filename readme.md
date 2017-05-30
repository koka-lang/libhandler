<!--madoko
Title         : Libhandler
Author        : Daan Leijen
Logo          : True 
code {
  background-color: #EEE;
}
[TITLE]
-->

# Overview 

`libhandler` implements algebraic effect handlers in C. It works by
capturing stacks in safe and portable manner. Algebraic effects 
handlers can for example be used to program asynchronous code in 
straightline manner and we hope to use it to make programming with
`libuv` more convenient.

For a primer on algebraic effects, see the 
relevant section in the [koka book].

Enjoy!\
-- Daan.

[koka book]: https://bit.do/kokabook

# Building

Building `libhandler` consists of generating a static C library that
can be linked in your own projects. `libhandler` is written to be 
as portable as possible but it depends on some platform specific
assumptions regarding stacks and `setjmp` implementations. On new
platforms please test carefully. Currently tested platforms include:

- (`gcc`,`clang`,`cl`)`-x86-pc-windows`  (32 bit)
- (`gcc`,`clang`,`cl`)`-x86_64-pc-windows`  (64 bit)

## Unix/MacOSX

Build using regular `configure` and `make`:
```
  $ ./configure
  $ make depend
  $ make
```
Use `VARIANT=release` to build a release version, and `tests`
as a target to run tests. For example:
```
  $ make tests VARIANT=release
```

Configuration options:

* `--cc=<cc>`
  : Specify the c-compiler to use (e.g. `gcc`, `clang`, etc.)
* `--cc-opts=<options>`
  : Specify extra c-compiler flags to use (e.g. `-m64`).
* `--host=<arch>-<vendor>-<os>`
  : Specify the platform as architecture (`x86`,`x86-64`,`amd64`),
    vendor (`pc`), and operating system (`windows`,`linux`,etc).
* `--abi=<abi>`
  : Specify the calling convention ABI. For example, on Linux
    the `x32` ABI may have to specified explicitly.

Make targets:

* `VARIANT=<debug|release>`
  : Specify the build variant.
* `staticlib`
  : Build a static library.
* `tests`
  : Build and run tests.
* `clean`
  : Clean all outputs.


## Windows

There are two ways to build on Windows:

1. Use the Microsoft Visual C++ IDE. The [2015 Community edition][msvc]
   is available for free for non-commercial use.
   The solution can be found at:
   ```
   ide/msvc/libhandler.sln
   ```
2. Use the regular `configure`/`make` for using other C compilers like
   `gcc` and `clang`. To run `configure` and `make` you need to install
   `msys2`, available at <http://msys2.github.io>. Please follow the
   installation instruction carefully. After install, you can install 
   further tools using the `msys2` package manager:
   - `pacman -S mingw-w64-x86_64-gcc` (c compiler)
   - `pacman -S mingw-w64-x86_64-gdb` (debugger)
   - `pacman -S make` (make)
   After this you can run `configure` and `make` as described above.

Successful configurations on Windows using `msys2` have been:

- `gcc-x86_64-w64-mingw32`\
   Using just `./configure`
- `gcc-x86-w64-mingw32`\
   Using the `mingw32` shell with `mingw-w64-i686-toolchain` installed.   
- `clang-x86_64-pc-windows`\
   Using `./configure --cc=/c/programs/llvm/bin/clang`.
- `clang-x86-pc-windows`  (32-bit)\
   Using `./configure --cc=/c/programs/llvm/bin/clang --cc-opts=-m32`.

Using the Visual Studio IDE:

- `cl-x86_64-pc-windows`
   Selecting 64-bit build.
- `cl-x86-pc-windows`
   Selecting 32-bit build.

Using 32-bit `mingw32` on windows (for `bash`) with  32-bit `clang` installed on
a regular command prompt:

- `gcc-x86-pc-windows`\
   Configure with `bash ./configure` and make in the windows shell
- `clang-x86-pc-windows`\
   Configure with
  `bash ./configure --cc=clang` and make in the windows shell

[msvc]: https://www.microsoft.com/en-us/download/details.aspx?id=48146
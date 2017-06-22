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

Warning: this library is still under active development and the 
API may change.

`libhandler` implements algebraic effect handlers in C. It works by
capturing stacks in safe and portable manner. Algebraic effects 
handlers can for example be used to program asynchronous code in 
straightline manner and we hope to use it to make programming with
`libuv` more convenient.

This library is described in detail in the accompanying [technical
report][tr]. For a primer on algebraic effects, see the 
relevant section in the [koka book]. 

Enjoy!\
-- Daan.

[tr]: https://www.microsoft.com/en-us/research/publication/implementing-algebraic-effects-c
[koka book]: https://bit.do/kokabook


# Building

Building `libhandler` consists of generating a static C library that
can be linked in your own projects. `libhandler` is written to be 
as portable as possible but it depends on some platform specific
assumptions regarding stacks and `setjmp` implementations. On new
platforms please test carefully. Currently tested platforms include:

- (`gcc`,`clang`,`cl`)`-x86-pc-windows`  (32 bit)
- (`gcc`,`clang`,`cl`)`-x64-pc-windows`  (64 bit)
- (`gcc`,`clang`)`-amd64-pc-linux`  (64 bit, Ubuntu 16.04)
- `gcc-x64-w64-mingw32`   (64 bit, Windows/MSys2)
- (`gcc`,`clang`)`-arm-linux-unknown` (32-bit, ARMv7 (raspberry pi 3, Rasbian/Debian Jessie))

C++ support is working but still under development.


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
* `--asm-opts=<options>`
  : Specify extra assembler flags to use (e.g. `-m64`).
* `--target=<arch>-<vendor>-<os>`
  : Specify the platform as architecture (`x86`,`x86-64`,`amd64`),
    vendor (`pc`), and operating system (`windows`,`linux`,etc).
* `--abi=<abi>`
  : Specify the calling convention ABI. For example, on Linux
    the `x32` ABI may have to specified explicitly.
* `--ar=<archiver>`
  : Specify the archiver for creating a static libary.
* `--cxx=<c++ compiler>`
  : Specify the C++ compiler to use.

Make targets:

* `VARIANT=<debug|testopt|release>`
  : Specify the build variant. 'testopt' builds optimized but with assertions enabled.
* `staticlib`
  : Build a static library.
* `tests`
  : Build and run tests.
* `bench`
  : Build and run benchmarks.
* `clean`
  : Clean all outputs.
* `staticlibxx`
  : Build the library for C++ (with exception and destructor unwinding support).
* `testsxx`
  : Build and run tests for C++.


## Windows

There are three ways to build on Windows:

1. Use the Microsoft Visual C++ IDE. The [2015 Community edition][msvc]
   is available for free for non-commercial use.
   The solution can be found at:
   ```
   ide/msvc/libhandler.sln
   ```

2. Enable the "Linux subsystem" on Windows 10. See [MSDN][winlinux]
   for installation instructions. Once enabled, you can simply
   run `bash` on the command prompt to enter Ubuntu Linux from Windows.
   Use `apt` to install the development tools:
   - `sudo apt-get update`
   - `sudo apt install build-essential`
   - `sudo apt install clang`
   
   After this you can run `configure` and `make` as described above.

3. On older Windows versions, you can use `msys2`, 
   available at <http://msys2.github.io>. Please follow the
   installation instruction carefully. After install, you can install 
   further tools using the `msys2` package manager:
   - `pacman -S mingw-w64-x86_64-gcc` (c compiler)
   - `pacman -S mingw-w64-x86_64-gdb` (debugger)
   - `pacman -S make` (make)
   
   After this you can run `configure` and `make` as described above.

Successful configurations `bash` on Windows have been:

- `gcc-amd64-pc-linux-gnu`\
  Using just `./configure`
- `clang-amd64-pc-linux-gnu`\
  Use `sudo apt install clang` followed by `./configure --cc=clang`

Successful configurations on Windows using `msys2` have been:

- `gcc-x64-w64-mingw32`\
   Using just `./configure`
- `gcc-x86-w64-mingw32`\
   Using the `mingw32` shell with `mingw-w64-i686-toolchain` installed.   
- `clang-x64-pc-windows`\
   Using `./configure --cc=/c/programs/llvm/bin/clang`.
- `clang-x86-pc-windows`  (32-bit)\
   Using `./configure --cc=/c/programs/llvm/bin/clang --cc-opts=-m32 --asm-opts=-m32`.

Using the Visual Studio IDE:

- `cl-x64-pc-windows`
   Selecting 64-bit build.
- `cl-x86-pc-windows`
   Selecting 32-bit build.

[msvc]: https://www.microsoft.com/en-us/download/details.aspx?id=48146
[winlinux]: https://msdn.microsoft.com/en-us/commandline/wsl/install_guide

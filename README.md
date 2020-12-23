# Gpick - advanced color picker and palette editor.

[![Build Status](https://dev.azure.com/thezbyg/Gpick/_apis/build/status/thezbyg.gpick?branchName=master)](https://dev.azure.com/thezbyg/Gpick/_build/latest?definitionId=1&branchName=master)

Gpick is an application that allows you to sample any color from anywhere on the desktop, and use it to create palettes (i.e. collections of colors) for use in graphic design applications. Gpick also has other features that help in the creation of color palettes, such as:

* The ability to create a palette from an imported image
* Automatic naming of colors
* Color scheme generator
* Import and export from various file formats

## Building from source



### Compiler

Some of C++14 features are required. Compilation is currently only tested on gcc version 5.3.1.

### Build dependencies

SCons 2.4 or newer: a software construction tool ([http://www.scons.org](http://www.scons.org)).

CMake 3.1 or newer: build process management application ([https://cmake.org/](https://cmake.org/)).

Either SCons or CMake can be used.

Ragel 6.8 or newer: state machine compiler ([http://www.colm.net/open-source/ragel](http://www.colm.net/open-source/ragel)).

### Dependencies

GTK+ 2.24 ([http://www.gtk.org](http://www.gtk.org)).

GTK+ 3.0 ([http://www.gtk.org](http://www.gtk.org)).

Either GTK+ 2.x or GTK+ 3.x can be used.

Lua 5.4, 5.3 or 5.2 ([http://www.lua.org](http://www.lua.org)).

Expat ([http://expat.sourceforge.net](http://expat.sourceforge.net)).

Boost 1.58 or newer ([http://www.boost.org](http://www.boost.org)).
Used libraries:

 * Filesystem.
 * System.
 * Interprocess.
 * Test (only when building/running tests).

### Optional dependencies

gettext ([http://www.gnu.org/s/gettext](http://www.gnu.org/s/gettext)). Required if ENABLE\_NLS is enabled. Required by default.

### Building

Using SCons:

`scons` to compile all files and place executable file in `build/source/`.

`scons install` to install executable and resources to `DESTDIR`. By default `DESTDIR` is `/usr/local`.

Using CMake:

`mkdir build && cd build` to create out-of-source build directory.

`cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local` to prepare build files for installation to '/usr/local'.

`make` to compile all files.

`make install` to install executable and resources to `DESTDIR`. Default `DESTDIR` value is set by `CMAKE_INSTALL_PREFIX` variable.

### Build options

ENABLE\_NLS - compile with gettext support. Enabled by default.

USE\_GTK3 - use GTK3 instead of GTK2. Enabled by default.


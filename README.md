# [`llvm-msvc`](https://llvm-msvc.com)
[![windows-llvm-msvc-build](https://github.com/backengineering/llvm-msvc/actions/workflows/windows-llvm-msvc-build.yml/badge.svg?branch=dev)](https://github.com/backengineering/llvm-msvc/actions/workflows/windows-llvm-msvc-build.yml)
[![android-llvm-msvc-build](https://github.com/backengineering/llvm-msvc/actions/workflows/android-llvm-msvc-build.yml/badge.svg?branch=dev)](https://github.com/backengineering/llvm-msvc/actions/workflows/android-llvm-msvc-build.yml)
[![macos-arm64-llvm-msvc-build](https://github.com/backengineering/llvm-msvc/actions/workflows/macos-arm64-llvm-msvc-build.yml/badge.svg)](https://github.com/backengineering/llvm-msvc/actions/workflows/macos-arm64-llvm-msvc-build.yml)

![image](https://github.com/backengineering/llvm-msvc/assets/13917777/86a7eb6a-466a-4290-8ec5-e3affc3e3c0a)
[![Github All Releases](https://img.shields.io/github/downloads/backengineering/llvm-msvc/total.svg)](https://github.com/backengineering/llvm-msvc/releases) 
[![GitHub release](https://img.shields.io/github/release/backengineering/llvm-msvc.svg)](https://github.com/backengineering/llvm-msvc/releases) 

[``llvm-msvc``](https://llvm-msvc.com) is a compiler based on ``LLVM`` that isn't limited by ``MSVC``. The aim is to provide the same experience as ``MSVC`` on Windows. You can use naked functions anywhere and also add custom support like obfuscation.

<img width="732" height="697" alt="image" src="https://github.com/user-attachments/assets/743ba706-24c4-48f3-92af-cc2f9e9f055f" />


## Features:
- Compatible with ``MSVC`` syntax as much as possible.
- Almost fully compatible with ``SEH``.
- Added some special Intrinsic functions(``__vmx_vmread``/``__vmx_write``).
- Supports ``x64``/``ARM64`` windows drivers.
- Supports ``AArch64`` android GKI drivers.
- Allows naked ``X64`` inline asm.
- Enables multiple cores compilation.
- Supports ``/MP`` when precompiled headers are present.
- Supports ``/GL`` (LTO optimization).


## FAQ
### Why do we make this project?
- ``Clang`` follows the ``GCC`` standard, while ``MSVC`` has its own unique syntax.
- Some of the code is pretty hacky. Can't submit it officially.
- Waiting for the official fix takes too long.

### How to use llvm-msvc?
- Install Visual Studio 2015-2022 (I recommend using 2022) along with WDK11.
- Download and install llvm-msvc from the following repository [llvm-msvc_X86_64_installer-PDB.exe](https://github.com/backengineering/llvm-msvc/releases).
- Create your Windows driver project and choose "LLVM-MSVC_v143_KernelMode" as your platform toolset.
- Compile your project.

  
### How to compile?

```
X86+ARM64：clang+lld+lldb+debug
cmake -Bbuild -A X64 -DLLDB_ENABLE_PYTHON=OFF -DLLVM_ENABLE_PROJECTS="clang;lld;lldb" -DCMAKE_INSTALL_PREFIX=E:\llvm\install-debug-64 -DLLVM_TARGETS_TO_BUILD="X86;AArch64" -DCMAKE_BUILD_TYPE=Debug llvm
msbuild /m -p:Configuration=Debug INSTALL.vcxproj 
--------------------------------------------------
X86+ARM64：clang+lld+lldb+RelWithDebInfo
mkdir build2
pushd build2
cmake .. -G "Visual Studio 17 2022" -A X64 -DLLVM_ENABLE_RPMALLOC=ON -DCMAKE_CXX_FLAGS="/utf-8" -DCMAKE_C_FLAGS="/utf-8" -DLLVM_ENABLE_PACK_PDB=ON -DLLDB_ENABLE_PYTHON=OFF -DLLVM_ENABLE_PROJECTS="clang;lld;lldb" -DCMAKE_INSTALL_PREFIX=E:\llvm\install-RelWithDebInfo-64 -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_ZLIB=OFF -DLLVM_TARGETS_TO_BUILD="X86;AArch64" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLLVM_USE_CRT_RELEASE=MT ../llvm
msbuild /m -p:Configuration=RelWithDebInfo INSTALL.vcxproj 
--------------------------------------------------
X86+ARM64：clang+lld+lldb+release
mkdir build3
pushd build3
cmake .. -G "Visual Studio 17 2022" -A X64 -DLLVM_ENABLE_RPMALLOC=ON -DCMAKE_CXX_FLAGS="/utf-8" -DCMAKE_C_FLAGS="/utf-8" -DLLDB_ENABLE_PYTHON=OFF -DLLVM_ENABLE_PROJECTS="clang;lld;lldb" -DCMAKE_INSTALL_PREFIX=E:\llvm\install-release-64 -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_ZLIB=OFF -DLLVM_TARGETS_TO_BUILD="X86;AArch64" -DCMAKE_BUILD_TYPE=release -DLLVM_USE_CRT_RELEASE=MT ../llvm
msbuild /m -p:Configuration=release INSTALL.vcxproj 
```

### How to contribute?
- https://github.com/HyunCafe/contribute-practice
- https://docs.github.com/en/get-started/quickstart/contributing-to-projects

### How can I learn LLVM?
If you don't know how to learn ``LLVM``, you can check out this [repository](https://github.com/gmh5225/awesome-llvm-security) of mine.

### Can it run on linux?
Yes.

### Can it run on macos?
Yes.

## Credits
- ``LLVM``
- Some anonymous people

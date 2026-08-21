#!/bin/sh
# Run Visual Studio cl from WSL. Start cmd from a drive letter so it is not
# stuck with a UNC WSL cwd (cmd cannot cd into those).
set -e
win=$(wslpath -w "$PWD")
cd /mnt/c/Windows
cmd.exe /c "pushd ${win} && msvc-cl.cmd $*"
exit $?

#!/bin/sh
# Run Visual Studio cl from WSL. cmd cannot start in a UNC cwd, so hop via C:\Windows.
set -e
win=$(wslpath -w "$PWD")
cmd.exe /c "cd /d C:\Windows && pushd ${win} && msvc-cl.cmd $*"
exit $?

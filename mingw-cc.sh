#!/bin/sh
# Run Windows MinGW gcc from WSL (gcc on the Windows PATH, not the WSL cross compiler).
set -e
win=$(wslpath -w "$PWD")
cd /mnt/c/Windows
cmd.exe /c "pushd ${win} && gcc $*"
exit $?

echo off
cls
rm test.obj
gcc -O2 -nostartfiles -Wall --std=c99 -c test.c -o test.obj
ld -s --subsystem windows --kill-at --no-seh -e entry_point test.obj -o test.exe  -L%LIBRARY_PATH% -lkernel32 -lshell32 -lole32 -lShlwapi

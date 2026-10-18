#!/bin/sh
# Name: InkBoy
# Author: InkBoy
# DontUseFBInk

# Filled by bin/deploy after the package has been verified.
run_dir='@RUN_DIR@'
remote_root='@REMOTE_ROOT@'
cd "$run_dir" || exit 1
test -x ./inkboy && test -r ./sameboy_libretro.so || exit 1

# The descriptor stays open across exec; the lock is released when InkBoy exits.
exec 9>"$remote_root/launcher.lock"
flock -n 9 || exit 0
exec ./inkboy > "$remote_root/launcher.log" 2>&1

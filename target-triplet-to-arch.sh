#!/bin/sh
if echo "$1" | grep -Eq '^aarch64-'; then
  echo aarch64
elif echo "$1" | grep -Eq 'i[[:digit:]]86-'; then
  echo i386
else
  echo "$1" | grep -Eo '^[[:alnum:]_]*'
fi

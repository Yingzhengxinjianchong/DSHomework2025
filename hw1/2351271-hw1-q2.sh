#!/bin/bash

# if there is no or more two parameters, fault.
if [ ${#} -ne 1 ]
then
  echo "Using: ${0} <target directory>" >&2
  exit 1
fi

directory="${1}"

# test whether it is a directory.
if [ ! -d "${directory}" ]
then
  echo "Fault: '${directory}' is not a valid directory" >&2
  exit 1
fi

# output
ls -p "${directory}" | grep -v / | tr 'A-Z' 'a-z' | sort

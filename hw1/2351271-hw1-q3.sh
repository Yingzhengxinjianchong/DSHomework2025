#!/bin/bash

# test parameters
if [ ${#} -ne 2 ]
then
  echo "Using: ${0} <target directory> <target file>" >&2
  exit 1
fi

search_dir="${1}"
file_name="${2}"

# test: whether it is a valid directory
if [ ! -d "${search_dir}" ]
then
  echo "Fault: '${search_dir}' is not a valid directory." >&2
  exit 1
fi

# find
found_path=$(find "${search_dir}" -name "${file_name}")

# output
if [ -n "${found_path}" ]
then
  echo "${found_path}"
else
  echo "not exist"
fi

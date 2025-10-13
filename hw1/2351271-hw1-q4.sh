#!/bin/bash

# test parameters
if [ ${#} -ne 2 ]
then
  echo "Using: ${0} <target file path> <target word>" >&2
  exit 1
fi

file_name=${1}
word_to_find=${2}

# test whether it is a valid file
if [ ! -f "${file_name}" ]
then
  echo "Fault: '${file_name}' is not a valid file." >&2
  exit 1
fi

# counting
count=$(grep -i -w -o "${word_to_find}" "${file_name}" | wc -l)

# output
if [ ${count} -gt 0 ]
then
  echo "${count}"
else
  echo "not exist"
fi

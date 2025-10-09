#!/bin/bash

# test parameters
if [ ${#} -ne 1 ]
then
  echo "Using: ${0} <time>" >&2
  exit 1
fi

time="${1}"

# test whether it is a positive integer
if ! [[ ${time} =~ ^[1-9][0-9]*+$ ]]
then
  echo "Fault: input a positive integer such as 1, 99, 187" >&2
  exit 1
fi

# counting down
while [ ${time} -gt 0 ]
do
  echo "${time}..."

  if [ ${time} -lt 10 ]
  then
    sleep ${time}
    time=0
  else
    sleep 10
    time=$(( time - 10 ))
  fi
done

echo "0..."

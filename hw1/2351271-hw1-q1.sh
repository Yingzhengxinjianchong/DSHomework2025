#!/bin/bash

sum=0

for (( i=2; i<=100; i++ ))
do
  is_prime=1; # 1 means it is a prime, 0 otherwise.
  for (( j=2; j<i; j++ ))
  do
    if (( i % j == 0 ))
    then
      is_prime=0
      break
    fi
  done

  if [ ${is_prime} -eq 1 ]
  then
    sum=$((sum+i))
  fi
done

echo ${sum}

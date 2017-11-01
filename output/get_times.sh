#!/bin/bash

file=$1

echo "accesses get set"

grep -ie "avg"  $file | cut -d ' ' -f2,19,22

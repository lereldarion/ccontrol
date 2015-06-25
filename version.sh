#!/bin/sh
ver=$(printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)")
echo "m4_define([VERSION_NUMBER], [$ver])" > version.m4

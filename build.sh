#!/bin/bash

set -e

while true
do
    echo
    echo "=================================="
    echo "     431 DSP Build System"
    echo "=================================="
    echo "1) Build"
    echo "2) Flash"
    echo "3) Build + Flash"
    echo "0) Exit"
    echo

    read -p "Selection: " CHOICE

    case $CHOICE in

        1)
            make clean
            make
            ;;

        2)
            st-flash --reset write build/431_DSP.bin 0x08000000
            ;;

        3)
            make clean
            make
            st-flash --reset write build/431_DSP.bin 0x08000000
            ;;

        0)
            exit 0
            ;;

        *)
            echo "Invalid selection."
            ;;
    esac
done
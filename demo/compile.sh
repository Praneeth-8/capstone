#!/bin/bash
gcc -Wall -Wextra receive_demo.c net.c protocol.c -o receive
gcc -Wall -Wextra send_demo.c net.c protocol.c -o send

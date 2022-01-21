Project was imported and originally made for CS 3377 (Systems Programming in UNIX and Other Environments) with Professor Nhut Nguyen.

Thoughts: Originally made in Linux vim for use with command line arguments. Won't work with CLion because a library from UNIX isn't recognized, so it's just here for
record-keeping. Uses fork, pipe, execvp, and wait to perform commands. Takes one set of input commands before terminating. Supports commands separated by semicolons, 
file I/O redirection, and piped commands (max 2 pipes). The trouble was formatting the input from the command line and standardizing it somehow to fit with a command execution 
loop. A 2D array is used to store commands, their options, and symbols between then (e.g. ";", "|", ">", "<", ">>"). I remember it took a frustratingly long time to figure out
that I should only use wait(NULL) after closing all pipe file descriptors.

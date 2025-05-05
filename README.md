Project Author: Dat Nguyen

Course: Comp Sci 4760

Date: 5/05/2025

GitHub Link: github.com/O-wll/OPSYS4760-PROJECT-5

Description of Project:

This project utilizes a simulated clock, resource structure, and PCB table to keep track of processes and what resources they are allocated. oss.c is the main program that grants/blocks requests of resources, it will be in charge of ensuring that either the process terminates on its own once releasing resources, or if deadlocked, gets terminated by main program. Users will be
allowed to adjust parameters for their testing needs. user.c is the child process that requests resources or releases them, helping to simulate resource management for oss.c, user.c will either terminate naturally or by the main program as a result of deadlocking.

oss.h is a header file that stores the actual structures needed, such as simulated clock as seen by previous projects, message queues, and the shared memory keys needed. Along with some constants.

User will be able to:

View the log file of OSS output, see what the PCB table looks like, the resources available, and using -v, more details about the actions oss.c was doing

Control max number of processes.

Control max number of processes running at the same time (Cannot exceed 18).

Control the interval between process launches.

Name the log file to your liking

Get either basic or detailed output.

How to compile, build, and use project:

The project comes with a makefile so ensure that when running this project that the makefile is in it.

Type 'make' and this will generate both the oss exe and user exe along with their object file.

user exe is for testing of user, you will only need to do ./oss.

When done and want to delete, run 'make clean' and the exe and object files will be deleted.

To use the project, use ./oss -h for info on how to use it.

Issues Ran Into:

Could not fix the fact that sometimes ./oss reaches alarm state and has to execute all processes.

A bit of confusion on how the program should've worked

Time struggles, I apologize for this being late.

General complexity of the project required a lot of research and mental headaches.

General syntax/coding slipups that went unnoticed for a bit.

Small issue with delimiters that eventually I fixed.

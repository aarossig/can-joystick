# CAN Joystick

## Overview

A small project that monitors the CAN bus of a vehicle and creates a uinput
virtual joystick. This allows the car to be used as an input device to
any simulators/games.

## Warranty

This code comes with no warranty. It is for educational purposes only. If you
chose to use this code with your vehicle it is at your own risk. The authors do
not advise that you do this. Any and all damages resulting from the use of this
project are your responsibility.

## Hardware

This code uses the ViewTool Ginkgo USB-CAN bridge and complimenting libusbcan.

## Building

This project uses git submodules. Be sure to initialize them first:

    git submodule init
    git submodule update

Alternatively, you can clone with ``--recurse-submodules`` to achieve the same.

Once the project has been setup, the build is as simple as invoking:

    make

## Running

Once the project has compiled successfully, use the following to run the
tool. The ``LD_LIBRARY_PATH`` must be supplied to allow the application
to find the shared object for communicating with the ViewTool device.

In addition, ``sudo`` is used to allow hardware access. This could probably
be avoided with udev rules.

    sudo LD_LIBRARY_PATH=libusbcan/lib/linux_64bit ./canjoystick

/*
  my_machine.h - build configuration for the grblHAL CH32V317 (PickOMatic) driver

  NOTE: This is a skeleton, the full option review is deliverable 7.
        N_AXIS and the board selection live here because both the core
        and the driver must see them; the build force-includes this file
        into every compilation unit (see Makefile / .cproject).

  Part of grblHAL

  Copyright (c) 2026 Ronan Mingon

  grblHAL is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  grblHAL is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with grblHAL. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _MY_MACHINE_H_
#define _MY_MACHINE_H_

// Board selection
#define BOARD_PICKOMATIC

// 7 stepper channels: X, Y, Z, A, B, C, U
#define N_AXIS 7

// Options (final review in deliverable 7)
//#define USB_SERIAL_CDC  1 // deliverable 4, USB CDC as second stream
//#define ETHERNET_ENABLE 1 // phase 2

#endif // _MY_MACHINE_H_

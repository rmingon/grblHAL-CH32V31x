/*
  main.c - entry point for the grblHAL CH32V317 (PickOMatic) driver

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

#include <stdbool.h>

#include "ch32v30x.h"

/*
  Deliverable 1: skeleton only.
  grblmain() from grbl/grbllib.c takes over from here once the driver
  (driver.c) is in place; for now just prove that clock setup, startup
  code and the toolchain produce a running image.
*/

int main (void)
{
    SystemCoreClockUpdate();

    while(true) {
        __NOP();
    }
}

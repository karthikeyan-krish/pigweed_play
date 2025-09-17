# Copyright 2023 The Pigweed Authors
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
# the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
"""Flash the bms binary to a connected STM32 Board.

Usage:
  bazel run //tools:flash
"""
import argparse
import subprocess

from python.runfiles import runfiles
from serial.tools import list_ports

_BINARY_APP_PATH = "_main/apps/blinky/application.elf"
_OPENOCD_PATH = "openocd/bin/openocd"
_OPENOCD_CONFIG_PATH = (
    "_main/apps/blinky/openocd_stm32l4xx.cfg"
)


def flash():
  r = runfiles.Create()
  parser = argparse.ArgumentParser()
  parser.add_argument(
    'action',
    nargs='?',
    default='program',
    const='program',
    choices=['program', 'erase'],
  )
  args = parser.parse_args()
  openocd = r.Rlocation(_OPENOCD_PATH)
  binary = r.Rlocation(_BINARY_APP_PATH)
  openocd_cfg = r.Rlocation(_OPENOCD_CONFIG_PATH)

  print(f"binary Rlocation is: {binary}")
  print(f"openocd Rlocation is: {openocd}")
  print(f"openocd config Rlocation is: {openocd_cfg}")

  assert binary is not None
  assert openocd_cfg is not None

  # Variables referred to the OpenOCD config.
  env = {
      "PW_GDB_PORT": "disabled",
  }

  call = [openocd, "-f", f"{openocd_cfg}"]
  if args.action == "program":
      call.append("-c")
      call.append(f"program {binary} reset exit")
  if args.action == "erase":
      call.append("-c")
      call.append(f"init")
      call.append("-c")
      call.append(f"halt")
      call.append("-c")
      call.append(f"flash erase_sector 0 0 last")
      call.append("-c")
      call.append(f"shutdown")
    
  subprocess.check_call(call, env=env)


if __name__ == "__main__":
  flash()

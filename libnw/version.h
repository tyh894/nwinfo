// SPDX-License-Identifier: Unlicense

#pragma once

#define QUOTE_(x) #x
#define QUOTE(x) QUOTE_(x)
//??
#define NWINFO_MAJOR_VERSION 1
#define NWINFO_MINOR_VERSION 6
#define NWINFO_MICRO_VERSION 8

#define NWINFO_VERSION      NWINFO_MAJOR_VERSION,NWINFO_MINOR_VERSION,NWINFO_MICRO_VERSION
#define NWINFO_VERSION_STR  QUOTE(NWINFO_MAJOR_VERSION.NWINFO_MINOR_VERSION.NWINFO_MICRO_VERSION)

#define NWINFO_COMPANY      "A1ive"
#define NWINFO_COPYRIGHT    "Copyright (c) 2026 A1ive"
#define NWINFO_FILEDESC     "Hardware information utility for Windows"

#define NWINFO_GUI          "¹¤¿Ø»úÉÚ±ø"
#define NWINFO_CLI          "NWinfo CLI"
#define NWINFO_DLL          "NWinfo DLL"

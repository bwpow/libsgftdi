/*
*    ShaGa FTDI library - extension to libftdi1 using libshaga
*    Copyright (c) 2016-2023, SAGE team s.r.o., Samuel Kupka
*
*    This library is distributed under the
*    GNU Library General Public License version 2.
*
*    A copy of the GNU Library General Public License (LGPL) is included
*    in this distribution, in the file COPYING.LIB.
*/
#ifndef _HEAD_SGFTDI_lite_mt
#define _HEAD_SGFTDI_lite_mt

#ifdef SGFTDI
	#error You must include only one sgftdi*.h
#endif // SGFTDI

#include <shagalite_mt.h>

#ifndef OS_LINUX
	#error This software works only in GNU/Linux
#endif // OS_LINUX

#define SGFTDI
#define SGFTDI_LITE

#include <libusb-1.0/libusb.h>

#include "sgftdi/ftdi.h"
#include "sgftdi/ftdistream.h"

#endif // _HEAD_SGFTDI_lite_mt

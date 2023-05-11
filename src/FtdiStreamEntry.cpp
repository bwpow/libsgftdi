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
#include "internal.h"

using namespace shaga;

FtdiStreamEntry::FtdiStreamEntry (struct ftdi_context *_ftdi) :
	ftdi (_ftdi)
{ }

void FtdiStreamEntry::set_read_start_enabled (const bool enabled)
{
	read_start_enabled = enabled;
}

void FtdiStreamEntry::set_read_include_modem_status (const bool enabled)
{
	read_include_modem_status = enabled;
}

void FtdiStreamEntry::set_read_transfers (const uint_fast32_t packets_per_transfer, const uint_fast32_t transfers)
{
	read_transfers = transfers;
	read_packets_per_transfer = packets_per_transfer;
}

void FtdiStreamEntry::set_write_transfers (const uint_fast32_t packets_per_transfer, const uint_fast32_t transfers)
{
	write_transfers = transfers;
	write_packets_per_transfer = packets_per_transfer;
}

void FtdiStreamEntry::set_callback (Callback callback)
{
	read_callback = callback;
	write_callback = callback;
}

void FtdiStreamEntry::set_read_callback (Callback callback)
{
	read_callback = callback;
}

void FtdiStreamEntry::set_write_callback (Callback callback)
{
	write_callback = callback;
}

void FtdiStreamEntry::set_counter_callback (CounterCallback callback)
{
	counter_callback = callback;
}

void FtdiStreamEntry::set_reset_callback (ResetCallback callback)
{
	reset_callback = callback;
}

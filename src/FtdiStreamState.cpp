/*
*    ShaGa FTDI library - extension to libftdi1 using libshaga
*    Copyright (c) 2016-2021, SAGE team s.r.o., Samuel Kupka
*
*    This library is distributed under the
*    GNU Library General Public License version 2.
*
*    A copy of the GNU Library General Public License (LGPL) is included
*    in this distribution, in the file COPYING.LIB.
*/
#include "internal.h"

using namespace shaga;

FtdiStreamState::FtdiStreamState (FtdiStreams &_streams) :
	streams (_streams),
	num_streams (streams.size ()),
	error_spsc (64)
{
	try {
		if (0 == num_streams) {
			cThrow ("No streams were defined"sv);
		}

		usb_ctx = streams.at (0).ftdi->usb_ctx;

		notice_event_fd = ::eventfd (0, 0);
		if (notice_event_fd < 0) {
			cThrow ("Unable to init eventfd: {}"sv, strerror (errno));
		}
	}
	catch (...)
	{
		if (notice_event_fd >= 0) {
			::close (notice_event_fd);
		}
		throw;
	}
}

FtdiStreamState::~FtdiStreamState ()
{
	::libusb_set_pollfd_notifiers (usb_ctx, nullptr, nullptr, nullptr);

	if (notice_event_fd >= 0) {
		::close (notice_event_fd);
	}

	if (timer_fd >= 0) {
		::close (timer_fd);
	}

	if (usb_epoll_fd >= 0) {
		::close (usb_epoll_fd);
	}

	if (epoll_fd >= 0) {
		::close (epoll_fd);
	}

	streamstates.reset ();
}

void FtdiStreamState::issue_notice (void) noexcept
{
	uint64_t c = 0x01;
	if (::write (notice_event_fd, &c, sizeof (c)) < 0) {
		P::print ("Error writing to notice eventfd: {}"sv, strerror (errno));
	}
}

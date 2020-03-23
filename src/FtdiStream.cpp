/*
*    ShaGa FTDI library - extension to libftdi1 using libshaga
*    Copyright (c) 2016-2020, SAGE team s.r.o., Samuel Kupka
*
*    This library is distributed under the
*    GNU Library General Public License version 2.
*
*    A copy of the GNU Library General Public License (LGPL) is included
*    in this distribution, in the file COPYING.LIB.
*/
#include "internal.h"

using namespace shaga;

FtdiStream::FtdiStream (FtdiStreams &streams) :
	_state (std::make_unique<FtdiStreamState> (streams))
{
	bool are_some_transfers = false;

	for (FtdiStreamEntry &stream : _state->streams) {
		if (stream.ftdi->usb_ctx != _state->usb_ctx) {
			cThrow ("All streams must use the same USB context"sv);
		}

		if (_state->read_packetsize > stream.ftdi->max_packet_size) {
			_state->read_packetsize = stream.ftdi->max_packet_size;
		}

		if (_state->write_packetsize > stream.ftdi->writebuffer_chunksize) {
			_state->write_packetsize = stream.ftdi->writebuffer_chunksize;
		}

		if (stream.read_transfers > 0) {
			if (stream.read_callback != nullptr) {
				are_some_transfers = true;

				if (0 == stream.read_packets_per_transfer) {
					cThrow ("Read packets per transfer is zero"sv);
				}
			}
			else {
				cThrow ("Read transfers is nonzero, but read callback is not set"sv);
			}
		}

		if (stream.write_transfers > 0) {
			if (stream.write_callback != nullptr) {
				are_some_transfers = true;

				if (0 == stream.write_packets_per_transfer) {
					cThrow ("Write packets per transfer is zero"sv);
				}

				if (stream.write_transfers != 1) {
					cThrow ("Only one write transfer is allowed per stream"sv);
				}
			}
			else {
				cThrow ("Write transfers is nonzero, but write callback is not set"sv);
			}
		}
	}

	if (false == are_some_transfers) {
		cThrow ("No streams have either reading or writing transfers"sv);
	}

	if (1 != ::libusb_pollfds_handle_timeouts (_state->usb_ctx)) {
		cThrow ("Unable to handle timeouts in libusb"sv);
	}

	_naked_state = _state.get ();
}

FtdiStream::~FtdiStream ()
{
	stop_thread ();
	stop_poll ();

	_naked_state = nullptr;
	_state.reset ();
}

void FtdiStream::set_timeout (const uint64_t timeout)
{
	_naked_state->timeout = timeout;
}

uint64_t FtdiStream::get_timeout (void) const
{
	return _naked_state->timeout;
}

size_t FtdiStream::get_errors (shaga::COMMON_LIST &append_to_lst)
{
	size_t cnt = 0;
	std::string buf;
	while (_naked_state->error_spsc.pop_front (buf) == true) {
		++cnt;
		append_to_lst.push_back (std::move (buf));
	}

	return cnt;
}

shaga::COMMON_LIST FtdiStream::get_errors (void)
{
	COMMON_LIST lst;
	get_errors (lst);
	return lst;
}

size_t FtdiStream::print_errors (const std::string_view prefix)
{
	size_t cnt = 0;
	std::string buf;
	while (_naked_state->error_spsc.pop_front (buf) == true) {
		++cnt;
		P::_print (buf, prefix);
	}

	return cnt;
}

bool FtdiStream::is_ending (void) const
{
	#ifdef SHAGA_THREADING
	return _naked_state->should_cancel.load (std::memory_order::memory_order_relaxed);
	#else
	return _naked_state->should_cancel;
	#endif // SHAGA_THREADING
}

void FtdiStream::enable_reading (const uint_fast32_t stream_id)
{
	#ifdef SHAGA_THREADING
	/* Lock both at the same time, avoid deadlock */
	std::unique_lock<std::mutex> lck1 (_naked_state->list_mutex, std::defer_lock);
	std::unique_lock<std::mutex> lck2 (_mutex, std::defer_lock);
	std::lock (lck1, lck2);
	#endif // SHAGA_THREADING

	if (false == _naked_state->is_started_thr && false == _naked_state->is_started_poll) {
		return;
	}

	#ifdef SHAGA_THREADING
	if (_naked_state->should_cancel.load (std::memory_order::memory_order_acquire) == true) {
		return;
	}
	#else
	if (true == _naked_state->should_cancel) {
		return;
	}
	#endif // SHAGA_THREADING

	if (stream_id >= _naked_state->num_streams) {
		cThrow ("Undefined stream id"sv);
	}

	if (nullptr == _naked_state->streams.at (stream_id).read_callback) {
		cThrow ("No read callback defined for stream id {}"sv, stream_id);
	}

	const int fd = _naked_state->streams.at (stream_id).read_callback (FtdiStreamEntry::CallbackType::READ_GET_FD, nullptr, 0);
	if (fd < 0) {
		cThrow ("Error reported by read callback for stream id {}"sv, stream_id);
	}

	_naked_state->list_disable.erase (fd);
	_naked_state->list_enable.insert (fd);

	#ifdef SHAGA_THREADING
	lck1.unlock ();
	lck2.unlock ();
	#endif // SHAGA_THREADING

	_naked_state->issue_notice ();
}

void FtdiStream::disable_reading (const uint_fast32_t stream_id)
{
	#ifdef SHAGA_THREADING
	/* Lock both at the same time, avoid deadlock */
	std::unique_lock<std::mutex> lck1 (_naked_state->list_mutex, std::defer_lock);
	std::unique_lock<std::mutex> lck2 (_mutex, std::defer_lock);
	std::lock (lck1, lck2);
	#endif // SHAGA_THREADING

	if (false == _naked_state->is_started_thr && false == _naked_state->is_started_poll) {
		return;
	}

	#ifdef SHAGA_THREADING
	if (_naked_state->should_cancel.load (std::memory_order::memory_order_acquire) == true) {
		return;
	}
	#else
	if (true == _naked_state->should_cancel) {
		return;
	}
	#endif // SHAGA_THREADING

	if (stream_id >= _naked_state->num_streams) {
		cThrow ("Undefined stream id"sv);
	}

	if (nullptr == _naked_state->streams.at (stream_id).read_callback) {
		cThrow ("No read callback defined for stream id {}"sv, stream_id);
	}

	const int fd = _naked_state->streams.at (stream_id).read_callback (FtdiStreamEntry::CallbackType::READ_GET_FD, nullptr, 0);
	if (fd < 0) {
		cThrow ("Error reported by read callback for stream id {}"sv, stream_id);
	}

	_naked_state->list_enable.erase (fd);
	_naked_state->list_disable.insert (fd);

	#ifdef SHAGA_THREADING
	lck1.unlock ();
	lck2.unlock ();
	#endif // SHAGA_THREADING

	_naked_state->issue_notice ();
}

void FtdiStream::reset_stream (const uint_fast32_t stream_id)
{
	#ifdef SHAGA_THREADING
	/* Lock both at the same time, avoid deadlock */
	std::unique_lock<std::mutex> lck1 (_naked_state->list_mutex, std::defer_lock);
	std::unique_lock<std::mutex> lck2 (_mutex, std::defer_lock);
	std::lock (lck1, lck2);
	#endif // SHAGA_THREADING

	if (false == _naked_state->is_started_thr && false == _naked_state->is_started_poll) {
		return;
	}

	#ifdef SHAGA_THREADING
	if (_naked_state->should_cancel.load (std::memory_order::memory_order_acquire) == true) {
		return;
	}
	#else
	if (true == _naked_state->should_cancel) {
		return;
	}
	#endif // SHAGA_THREADING

	if (stream_id >= _naked_state->num_streams) {
		cThrow ("Undefined stream id"sv);
	}

	_naked_state->list_reset.insert (stream_id);

	#ifdef SHAGA_THREADING
	lck1.unlock ();
	lck2.unlock ();
	#endif // SHAGA_THREADING

	_naked_state->issue_notice ();
}

bool FtdiStream::is_started_thread (void) const
{
	#ifdef SHAGA_THREADING
	return _naked_state->is_started_thr;
	#else
	return false;
	#endif // SHAGA_THREADING
}

bool FtdiStream::is_active_thread (void) const
{
	#ifdef SHAGA_THREADING
	return (true == _naked_state->is_started_thr) && (false == _naked_state->should_cancel.load (std::memory_order::memory_order_relaxed));
	#else
	return false;
	#endif // SHAGA_THREADING
}

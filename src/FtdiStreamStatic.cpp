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

#include <termios.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <fcntl.h>

using namespace shaga;

class FtdiStreamStatic
{
	public:
		static void cancel (FtdiStreamState * const state) noexcept
		{
			if (nullptr == state) {
				return;
			}

			state->should_run = false;

			#ifdef SHAGA_THREADING
			state->should_cancel.store (true, std::memory_order::memory_order_release);
			#else
			state->should_cancel = true;
			#endif // SHAGA_THREADING

			state->issue_notice ();
		}

		template <typename... Args>
		static void error (FtdiStreamState * const state, const std::string_view format, const Args & ... args) noexcept
		{
			if (nullptr == state) {
				return;
			}

			cancel (state);

			try {
				state->error_spsc.push_back (fmt::format (format, args...));
			}
			catch (...) { /* Intentionally ignored */ }
		}

		static void LIBUSB_CALL read_callback (struct libusb_transfer * const transfer) noexcept
		{
			FtdiStreamStaticState * const streamstate = reinterpret_cast<FtdiStreamStaticState *> (transfer->user_data);

			if (nullptr == streamstate) {
				return;
			}

			++streamstate->counter_callbacks;

			if (false == streamstate->enabled) {
				P::print ("@{},{}: read_callback disabled"sv, streamstate->stream_id, streamstate->transfer_id);
				return;
			}

			FtdiStreamState * const state = streamstate->state;

			if (LIBUSB_TRANSFER_CANCELLED == transfer->status || false == state->should_run) {
				streamstate->enabled = false;
				cancel (state);
				return;
			}

			try {
				if (LIBUSB_TRANSFER_COMPLETED == transfer->status) {
					/* First two bytes of every transfer contain modem status */
					if (transfer->actual_length > 2) {
						state->ts_activity = state->ts_now;

						char *ptr = reinterpret_cast<char *> (transfer->buffer);
						uint32_t length = transfer->actual_length;

						/* One transfer can contain more messages. Each message is at most state->read_packetsize bytes */
						while (length > 0) {
							const uint32_t packetLen = std::min (length, state->read_packetsize);

							FtdiStreamEntry &entry = state->streams[streamstate->stream_id];
							if (true == streamstate->is_modem_status) {
								if (packetLen >= 2) {
									streamstate->counter_bytes += packetLen - 2;
									entry.read_callback (FtdiStreamEntry::CallbackType::READ_BUFFER, ptr, packetLen);
								}
							}
							else {
								if (packetLen > 2) {
									streamstate->counter_bytes += packetLen - 2;
									/* User doesn't want modem status, skip first two bytes */
									entry.read_callback (FtdiStreamEntry::CallbackType::READ_BUFFER, ptr + 2, packetLen - 2);
								}
							}

							ptr += packetLen;
							length -= packetLen;
						}
					}
					else if (true == streamstate->is_modem_status && 2 == transfer->actual_length) {
						char *ptr = reinterpret_cast<char *> (transfer->buffer);
						FtdiStreamEntry &entry = state->streams[streamstate->stream_id];
						entry.read_callback (FtdiStreamEntry::CallbackType::READ_BUFFER, ptr, 2);
					}

					if (::libusb_submit_transfer (transfer) != LIBUSB_SUCCESS) {
						cThrow ("Submit transfer failed"sv);
					}
				}
				else {
					cThrow ("Unknown state"sv);
				}
			}
			catch (const std::exception &e) {
				streamstate->enabled = false;
				error (state, "@{},{}: read callback - {}"sv, streamstate->stream_id, streamstate->transfer_id, e.what ());
			}
			catch (...) {
				streamstate->enabled = false;
				error (state, "@{},{}: read callback - unknown exception"sv, streamstate->stream_id, streamstate->transfer_id);
			}
		}

		static void LIBUSB_CALL write_callback (struct libusb_transfer * const transfer) noexcept
		{
			FtdiStreamStaticState * const streamstate = reinterpret_cast<FtdiStreamStaticState *> (transfer->user_data);

			if (nullptr == streamstate) {
				return;
			}

			++streamstate->counter_callbacks;

			if (false == streamstate->enabled) {
				P::print ("@{},{}: write_callback disabled"sv, streamstate->stream_id, streamstate->transfer_id);
				return;
			}

			FtdiStreamState * const state = streamstate->state;

			if (LIBUSB_TRANSFER_CANCELLED == transfer->status || false == state->should_run) {
				streamstate->enabled = false;
				transfer->length = 0;
				cancel (state);
				return;
			}

			try {
				//P::print ("@{},{}: confirm {} bytes"sv, streamstate->stream_id, streamstate->transfer_id, transfer->actual_length);

				if (transfer->actual_length > 0) {
					streamstate->counter_bytes += transfer->actual_length;
					transfer->length = state->streams[streamstate->stream_id].write_callback (FtdiStreamEntry::CallbackType::WRITE_CONFIRM_TRANSFER, nullptr, transfer->actual_length);
					if (transfer->length != 0) {
						cThrow ("Callback WRITE_CONFIRM_TRANSFER reported error {}"sv, transfer->length);
					}
				}

				transfer->length = state->streams[streamstate->stream_id].write_callback (FtdiStreamEntry::CallbackType::WRITE_FILL_BUFFER, reinterpret_cast<char *> (transfer->buffer), streamstate->buffer_size);
				if (transfer->length < 0) {
					cThrow ("Callback WRITE_FILL_BUFFER reported error {}"sv, transfer->length);
				}
				else if (0 == transfer->length) {
					/* Nothing to send, disable this stream */
					streamstate->enabled = false;
					//P::print ("@{},{}: write_callback - nothing to transfer, disabling"sv, streamstate->stream_id, streamstate->transfer_id);
				}
				else if (::libusb_submit_transfer (transfer) != LIBUSB_SUCCESS) {
					cThrow ("Submit transfer failed"sv);
				}
			}
			catch (const std::exception &e) {
				error (state, "@{},{}: write callback - {}"sv, streamstate->stream_id, streamstate->transfer_id, e.what ());
				streamstate->enabled = false;
			}
			catch (...) {
				error (state, "@{},{}: write callback - unknown exception"sv, streamstate->stream_id, streamstate->transfer_id);
				streamstate->enabled = false;
			}
		}

		static void LIBUSB_CALL add_to_epoll (int sock, const uint32_t ev, void * const user_data) noexcept
		{
			if (nullptr == user_data) {
				return;
			}

			FtdiStreamState * const state = reinterpret_cast<FtdiStreamState *> (user_data);

			try {
				LINUX::add_to_epoll (sock, ev, state->epoll_fd, true);
			}
			catch (const std::exception &e) {
				error (state, "FtdiStreamStatic::add_to_epoll : {}"sv, e.what ());
			}
			catch (...) {
				error (state, "FtdiStreamStatic::add_to_epoll : Unknown error"sv);
			}
		}

		static void LIBUSB_CALL add_to_usb_epoll (int sock, const short ev, void * const user_data) noexcept
		{
			if (nullptr == user_data) {
				return;
			}

			FtdiStreamState * const state = reinterpret_cast<FtdiStreamState *> (user_data);

			try {
				LINUX::add_to_epoll (sock, ev, state->usb_epoll_fd, true);
			}
			catch (const std::exception &e) {
				error (state, "FtdiStreamStatic::add_to_usb_epoll : {}"sv, e.what ());
			}
			catch (...) {
				error (state, "FtdiStreamStatic::add_to_usb_epoll : Unknown error"sv);
			}
		}

		static void LIBUSB_CALL remove_from_usb_epoll (int sock, void * const user_data) noexcept
		{
			if (nullptr == user_data) {
				return;
			}

			FtdiStreamState * const state = reinterpret_cast<FtdiStreamState *> (user_data);

			try {
				LINUX::remove_from_epoll (sock, state->usb_epoll_fd);
			}
			catch (const std::exception &e) {
				error (state, "FtdiStreamStatic::remove_from_usb_epoll : {}"sv, e.what ());
			}
			catch (...) {
				error (state, "FtdiStreamStatic::remove_from_usb_epoll : Unknown error"sv);
			}
		}

		static_assert (EAGAIN == EWOULDBLOCK);

		static void event_timer (FtdiStreamState * const state)
		{
			uint64_t val;
			const ssize_t sze = ::read (state->timer_fd, &val, sizeof (val));
			if (sze < 0) {
				switch (errno) {
					case EWOULDBLOCK:
					case EINTR:
						return;

					default:
						error (state, "Error reading from timer_fd: {}"sv, strerror (errno));
						return;
				}
			}

			for (auto &[fd, streamstate] : *state->streamstates) {
				(void) fd;
				FtdiStreamEntry &entry = state->streams.at (streamstate.stream_id);
				if (entry.counter_callback != nullptr) {
					entry.counter_callback (streamstate.is_reading, streamstate.transfer_id, streamstate.counter_callbacks, streamstate.counter_bytes);
				}
				streamstate.counter_callbacks = 0;
				streamstate.counter_bytes = 0;
			}

			if (0 == state->timeout) {
				return;
			}

			state->ts_now = get_monotime_sec ();

			if ((state->ts_activity + state->timeout) < state->ts_now) {
				error (state, "Timeout reached"sv);
			}
		}

		static void event_notice (FtdiStreamState * const state)
		{
			uint64_t val;
			const ssize_t sze = ::read (state->notice_event_fd, &val, sizeof (val));
			if (sze < 0) {
				switch (errno) {
					case EWOULDBLOCK:
					case EINTR:
						return;

					default:
						error (state, "Error reading from event_fd: {}"sv, strerror (errno));
						return;
				}
			}

			if (false == state->should_run) {
				/* Nothing to do here */
				return;
			}

			/* Check if we should stop running */

			#ifdef SHAGA_THREADING
				if (true == state->should_cancel.load (std::memory_order::memory_order_acquire)) {
					state->should_run = false;
					return;
				}
				std::lock_guard<std::mutex> lock (state->list_mutex);
			#else
				if (true == state->should_cancel) {
					state->should_run = false;
					return;
				}
			#endif // SHAGA_THREADING

			process_reset_stream_entry (state, false);

			for (const int lst : state->list_enable) {
				auto [iter_begin, iter_end] = state->streamstates->equal_range (lst);
				for (auto iter = iter_begin; iter != iter_end; ++iter) {
					if (std::exchange (iter->second.enabled, true) == false) {
						/* This entry was disabled before, so we need to submit it again */
						iter->second.submit ();
					}
				}
			}
			state->list_enable.clear ();

			for (const int lst : state->list_disable) {
				auto [iter_begin, iter_end] = state->streamstates->equal_range (lst);
				for (auto iter = iter_begin; iter != iter_end; ++iter) {
					if (true == iter->second.enabled) {
						/* This entry is now enabled, so call cancel */
						iter->second.cancel ();
					}
				}
			}
			state->list_disable.clear ();
		}

		static void process_cleanup (FtdiStreamState * const state) noexcept
		{
			cancel (state);

			::libusb_set_pollfd_notifiers (state->usb_ctx, nullptr, nullptr, nullptr);

			if (state->timer_fd >= 0) {
				::close (state->timer_fd);
				state->timer_fd = -1;
			}

			if (state->usb_epoll_fd >= 0) {
				::close (state->usb_epoll_fd);
				state->usb_epoll_fd = -1;
			}

			if (state->epoll_fd >= 0) {
				::close (state->epoll_fd);
				state->epoll_fd = -1;
			}

			state->streamstates.reset ();
		}

		static void process_reset_stream_entry (FtdiStreamState * const state, const bool reset_all)
		{
			auto func = [](FtdiStreamEntry &stream) -> void {
				if (nullptr != stream.reset_callback) {
					stream.reset_callback (stream.ftdi);
				}
				else {
					/* We don't know in what state we are, switch to reset*/
					if (::ftdi_set_bitmode (stream.ftdi, 0xff, BITMODE_RESET) < 0) {
						cThrow ("Can't reset mode"sv);
					}

					/* Purge anything remaining in the buffers*/
					if (::ftdi_tcioflush (stream.ftdi) < 0) {
						cThrow ("Can't Purge"sv);
					}
				}
			};

			if (true == reset_all) {
				for (FtdiStreamEntry &stream : state->streams) {
					func (stream);
				}
			}
			else {
				for (const uint_fast32_t stream_id : state->list_reset) {
					func (state->streams.at (stream_id));
				}
				state->list_reset.clear ();
			}
		}

		static void process_init (FtdiStreamState * const state)
		{
			if (nullptr == state) {
				cThrow ("State isn't initialized"sv);
			}

			if (nullptr != state->streamstates) {
				cThrow ("State wasn't properly destroyed"sv);
			}

			try {
				state->streamstates = std::make_unique<FtdiStreamStaticStates_t> ();
				FtdiStreamStaticStates_t *streamstates = state->streamstates.get ();

				state->epoll_fd = -1;
				state->usb_epoll_fd = -1;
				state->timer_fd = -1;

				state->cancel_counter = 3;

				const uint64_t now = get_monotime_sec ();
				state->ts_now = now;
				state->ts_activity = now;

				state->list_enable.clear ();
				state->list_disable.clear ();
				state->list_reset.clear ();

				state->should_run = true;

				#ifdef SHAGA_THREADING
					state->should_cancel.store (false, std::memory_order::memory_order_release);
				#else
					state->should_cancel = false;
				#endif // SHAGA_THREADING

				state->epoll_fd = ::epoll_create1 (0);
				if (state->epoll_fd < 0) {
					cThrow ("Unable to init epoll: {}"sv, strerror (errno));
				}

				state->usb_epoll_fd = ::epoll_create1 (0);
				if (state->usb_epoll_fd < 0) {
					cThrow ("Unable to init epoll: {}"sv, strerror (errno));
				}
				else {
					add_to_epoll (state->usb_epoll_fd, EPOLLIN | EPOLLET, state);
				}

				add_to_epoll (state->notice_event_fd, EPOLLIN | EPOLLET, state);

				state->timer_fd = ::timerfd_create (CLOCK_MONOTONIC, TFD_NONBLOCK);
				if (state->timer_fd < 0) {
					cThrow ("Unable to init timer_fd: {}"sv, strerror (errno));
				}
				else {
					struct itimerspec timspec;
					bzero (&timspec, sizeof (timspec));
					timspec.it_interval.tv_sec = 1;
					timspec.it_interval.tv_nsec = 0;
					timspec.it_value.tv_sec = 0;
					timspec.it_value.tv_nsec = 1;

					if (timerfd_settime (state->timer_fd, 0, &timspec, 0) != 0) {
						cThrow ("Unable to start timer_fd: {}"sv, strerror (errno));
					}

					add_to_epoll (state->timer_fd, EPOLLIN | EPOLLET, state);
				}

				if (const struct libusb_pollfd** pollfd = ::libusb_get_pollfds (state->usb_ctx); nullptr != pollfd) {
					try {
						for (int i = 0; pollfd[i] != nullptr; ++i) {
							add_to_usb_epoll (pollfd[i]->fd, pollfd[i]->events, state);
						}
					} catch (...) {
						::libusb_free_pollfds (pollfd);
						throw;
					}
					::libusb_free_pollfds (pollfd);
				}
				else {
					cThrow ("Unable to get pollfds"sv);
				}

				::libusb_set_pollfd_notifiers (state->usb_ctx, add_to_usb_epoll, remove_from_usb_epoll, state);

				process_reset_stream_entry (state, true);

				for (uint_fast32_t stream_id = 0; stream_id < state->num_streams; ++stream_id) {
					FtdiStreamEntry &stream = state->streams[stream_id];
					P::debug_print ("FtdiStream init streamd_id = {}, read transfers = {}, write transfers = {}"sv, stream_id, stream.read_transfers, stream.write_transfers);

					auto create_streamstate = [&](const bool is_reading, const uint_fast32_t transfer_id) -> void {
						const int eventfd = (true == is_reading) ?
							(stream.read_callback (FtdiStreamEntry::CallbackType::READ_GET_FD, nullptr, 0)) :
							(stream.write_callback (FtdiStreamEntry::CallbackType::WRITE_GET_FD, nullptr, 0));

						auto ret = streamstates->emplace (std::piecewise_construct, std::make_tuple (eventfd), std::make_tuple (
							stream_id,
							transfer_id,
							is_reading,
							stream.read_include_modem_status,
							eventfd,
							state
						));
						ret->second.init (stream);
					};

					for (uint_fast32_t i = 0; i < stream.read_transfers; ++i) {
						create_streamstate (true, i);
					}

					for (uint_fast32_t i = 0; i < stream.write_transfers; ++i) {
						create_streamstate (false, stream.read_transfers + i);
					}
				}

			}
			catch (const std::exception &e) {
				process_cleanup (state);
				error (state, "FtdiStreamStatic::process_init : {}"sv, e.what ());
				throw;
			}
			catch (...) {
				process_cleanup (state);
				error (state, "FtdiStreamStatic::process_init : unknown exception"sv);
				throw;
			}
		}

		static bool process_step (FtdiStreamState * const state, const int timeout = -1)
		{
			if (nullptr == state->streamstates) {
				return false;
			}

			const int ret = ::epoll_wait (state->epoll_fd, state->epoll_events, state->num_epoll_events, timeout);
			if (ret < 0) {
				cancel (state);
				return false;
			}

			if (0 == ret) {
				/* Timeout reached */
				return true;
			}

			try {
				for (int index = 0; index < ret; ++index) {
					struct epoll_event &e = state->epoll_events[index];
					const int sock = e.data.fd;

					if (sock == state->usb_epoll_fd) {
						const int err = ::libusb_handle_events_timeout (state->usb_ctx, &state->libusb_timeout);
						if (err != LIBUSB_SUCCESS && err != LIBUSB_ERROR_INTERRUPTED) {
							cThrow ("Error handling libusb events"sv);
						}
					}
					else if (sock == state->notice_event_fd) {
						event_notice (state);
					}
					else if (sock == state->timer_fd) {
						event_timer (state);
					}
					else {
						auto [iter_begin, iter_end] = state->streamstates->equal_range (sock);
						for (auto iter = iter_begin; iter != iter_end; ++iter) {
							if (std::exchange (iter->second.enabled, true) == false) {
								/* This entry was disabled before, so we need to submit it again */
								iter->second.submit ();
							}
						}
					}
				}

				if (false == state->should_run) {
					try {
						process_reset_stream_entry (state, true);
					}
					catch (...) { /* Intentionally ignored */ }

					bool are_all_disabled = true;
					for (auto &entry : *(state->streamstates)) {
						if (true == entry.second.enabled) {
							entry.second.cancel ();
							are_all_disabled = false;
						}
					}

					if ((--state->cancel_counter) < 0) {
						are_all_disabled = true;
					}

					if (true == are_all_disabled) {
						return false;
					}
				}
			}
			catch (...) {
				cancel (state);
				throw;
			}

			return true;
		}

		static void process_loop_thread (FtdiStreamState * const state) noexcept
		{
			try {
				if (nullptr == state->streamstates) {
					cThrow ("State wasn't properly initialized"sv);
				}

				while (process_step (state)) {}
			}
			catch (const std::exception &e) {
				error (state, "FtdiStreamStatic::process_loop : {}"sv, e.what ());
			}
			catch (...) {
				error (state, "FtdiStreamStatic::process_loop : unknown exception"sv);
			}

			process_cleanup (state);
		}
};


FtdiStreamStaticState::FtdiStreamStaticState (
	const uint_fast32_t _stream_id,
	const uint_fast32_t _transfer_id,
	const bool _is_reading,
	const bool _is_modem_status,
	const int _eventfd,
	FtdiStreamState * const _state
) :
	stream_id (_stream_id),
	transfer_id (_transfer_id),
	is_reading (_is_reading),
	is_modem_status (_is_modem_status),
	eventfd (_eventfd),
	state (_state),
	transfer (::libusb_alloc_transfer (0))
{
	P::debug_print ("FtdiStream init tranfer @{},{}: reading = {}, fd = {}"sv, stream_id, transfer_id, is_reading, eventfd);

	if (nullptr == transfer) {
		cThrow ("@{},{}: Unable to allocate transfer"sv, stream_id, transfer_id);
	}
}

FtdiStreamStaticState::~FtdiStreamStaticState ()
{
	if (nullptr != transfer) {
		if (nullptr != transfer->buffer) {
			::free (transfer->buffer);
			transfer->buffer = nullptr;
		}
		::libusb_free_transfer (transfer);
		transfer = nullptr;
		P::debug_print ("FtdiStream destroy tranfer @{},{}"sv, stream_id, transfer_id);
	}
}

void FtdiStreamStaticState::init (FtdiStreamEntry &stream)
{
	if (true == is_reading) {
		enabled = stream.read_start_enabled;
		buffer_size = state->read_packetsize * stream.read_packets_per_transfer;
		::libusb_fill_bulk_transfer (
			transfer, // the transfer to populate
			stream.ftdi->usb_dev, // handle of the device that will handle the transfer
			stream.ftdi->out_ep, // address of the endpoint where this transfer will be sent
			reinterpret_cast<unsigned char *> (::malloc (buffer_size)), // data buffer
			buffer_size, // length of data buffer
			FtdiStreamStatic::read_callback, // callback function to be invoked on transfer completion
			this, // user data to pass to callback function
			0 // timeout for the transfer in milliseconds
		);
	} else {
		enabled = true;
		buffer_size = state->write_packetsize * stream.write_packets_per_transfer;
		::libusb_fill_bulk_transfer (
			transfer, // the transfer to populate
			stream.ftdi->usb_dev, // handle of the device that will handle the transfer
			stream.ftdi->in_ep, // address of the endpoint where this transfer will be sent
			reinterpret_cast<unsigned char *> (::malloc (buffer_size)), // data buffer
			0,  // length of data buffer
			FtdiStreamStatic::write_callback, // callback function to be invoked on transfer completion
			this, // user data to pass to callback function
			0 // timeout for the transfer in milliseconds
		);
	}

	if (nullptr == transfer->buffer) {
		cThrow ("@{},{}: Unable to allocate transfer buffer of {} bytes"sv, stream_id, transfer_id, buffer_size);
	}

	transfer->type = LIBUSB_TRANSFER_TYPE_BULK;
	transfer->flags = 0;

	if (false == is_reading) {
		FtdiStreamStatic::add_to_epoll (eventfd, EPOLLIN | EPOLLET, state);
	}

	submit ();
}

void FtdiStreamStaticState::submit (void)
{
	if (nullptr == transfer || nullptr == transfer->buffer) {
		cThrow ("@{},{}: Unable to submit null transfer"sv, stream_id, transfer_id);
	}

	if (false == state->should_run) {
		return;
	}

	if (true == enabled) {
		if (true == is_reading) {
			transfer->length = buffer_size;
		} else {
			transfer->length = state->streams[stream_id].write_callback (FtdiStreamEntry::CallbackType::WRITE_FILL_BUFFER, reinterpret_cast<char *> (transfer->buffer), buffer_size);
		}

		if (transfer->length < 0) {
			cThrow ("@{},{}: Callback WRITE_FILL_BUFFER reported error {}"sv, stream_id, transfer_id, transfer->length);
		}
		else if (0 == transfer->length) {
			/* Nothing to transfer */
			enabled = false;
		}
		else if (::libusb_submit_transfer (transfer) != 0) {
			cThrow ("@{},{}: Submit transfer error"sv, stream_id, transfer_id);
		}
	}
}

void FtdiStreamStaticState::cancel (void)
{
	if (nullptr == transfer) {
		cThrow ("@{},{}: Unable to cancel null transfer"sv, stream_id, transfer_id);
	}

	if (true == enabled) {
		::libusb_cancel_transfer (transfer);
	}
}

int FtdiStream::get_poll_fd (void)
{
	#ifdef SHAGA_THREADING
	std::lock_guard<std::mutex> lock (_mutex);
	#endif // SHAGA_THREADING

	return _naked_state->epoll_fd;
}

void FtdiStream::start_poll (void)
{
	#ifdef SHAGA_THREADING
	std::lock_guard<std::mutex> lock (_mutex);
	#endif // SHAGA_THREADING

	if (true == _naked_state->is_started_thr) {
		cThrow ("Thread version is already started"sv);
	}

	if (false == _naked_state->is_started_poll) {
		try {
			FtdiStreamStatic::process_init (_naked_state);
			_naked_state->is_started_poll = true;
		}
		catch (...) {
			FtdiStreamStatic::process_cleanup (_naked_state);
			_naked_state->is_started_poll = false;
			throw;
		}
	}
}

void FtdiStream::stop_poll (void)
{
	#ifdef SHAGA_THREADING
	std::lock_guard<std::mutex> lock (_mutex);
	#endif // SHAGA_THREADING

	if (true == _naked_state->is_started_poll) {
		#ifdef SHAGA_THREADING
		_naked_state->should_cancel.store (true, std::memory_order::memory_order_release);
		#else
		_naked_state->should_cancel = true;
		#endif // SHAGA_THREADING

		_naked_state->issue_notice ();

		/* Now wait for all streams to finish */
		while (FtdiStreamStatic::process_step (_naked_state));

		FtdiStreamStatic::process_cleanup (_naked_state);
		_naked_state->is_started_poll = false;
	}
}

bool FtdiStream::poll (const int timeout)
{
	if (true == _naked_state->is_started_poll) {
		return FtdiStreamStatic::process_step (_naked_state, timeout);
	}
	else {
		return false;
	}
}

void FtdiStream::start_thread (void)
{
	#ifdef SHAGA_THREADING
	std::lock_guard<std::mutex> lock (_mutex);

	if (true == _naked_state->is_started_poll) {
		cThrow ("Polling version is already started"sv);
	}

	if (false == _naked_state->is_started_thr) {
		try {
			FtdiStreamStatic::process_init (_naked_state);
			_thr = std::thread (FtdiStreamStatic::process_loop_thread, _naked_state);
			_naked_state->is_started_thr = true;
		}
		catch (...) {
			FtdiStreamStatic::process_cleanup (_naked_state);
			_naked_state->is_started_thr = false;
			throw;
		}
	}
	#else
	cThrow ("This version of the library is compiled without threading support"sv);
	#endif // SHAGA_THREADING
}

void FtdiStream::stop_thread (void)
{
	#ifdef SHAGA_THREADING
	std::lock_guard<std::mutex> lock (_mutex);

	if (true == _naked_state->is_started_thr) {
		_naked_state->should_cancel.store (true, std::memory_order::memory_order_release);
		_naked_state->issue_notice ();

		/* Now wait for thread to join */
		_thr.join ();

		FtdiStreamStatic::process_cleanup (_naked_state);
		_naked_state->is_started_thr = false;
	}
	#endif // SHAGA_THREADING
}

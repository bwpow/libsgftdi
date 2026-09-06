/*
*    ShaGa FTDI library - extension to libftdi1 using libshaga
*    Copyright (c) 2016-2026, SAGE team s.r.o., Samuel Kupka
*
*    This library is distributed under the GNU Library General Public License version 2.
*/
#include <gtest/gtest.h>

#include <set>
#include <unistd.h>

namespace
{
	struct MockUsb
	{
		std::set<libusb_transfer *> pending;
		std::set<libusb_transfer *> cancelling;
		int submit_calls {0};
		int fail_submit_call {0};
		int drain_calls {0};
		int cancellation_delay {0};
		int handle_failures {0};
		bool freed_pending {false};
		int open_calls {0};
		int close_calls {0};
		int descriptor_result {0};
		int init_calls {0};
		int exit_calls {0};

		void reset (void)
		{
			pending.clear ();
			cancelling.clear ();
			submit_calls = 0;
			fail_submit_call = 0;
			drain_calls = 0;
			cancellation_delay = 0;
			handle_failures = 0;
			freed_pending = false;
			open_calls = 0;
			close_calls = 0;
			descriptor_result = 0;
			init_calls = 0;
			exit_calls = 0;
		}
	} mock_usb;

	libusb_device_handle * const fake_handle = reinterpret_cast<libusb_device_handle *> (0x1234);

	void complete_transfer (libusb_transfer * const transfer, const libusb_transfer_status status, const int actual_length)
	{
		ASSERT_EQ (1U, mock_usb.pending.erase (transfer));
		mock_usb.cancelling.erase (transfer);
		transfer->status = status;
		transfer->actual_length = actual_length;
		transfer->callback (transfer);
	}

	struct StreamFixture : public ::testing::Test
	{
		void SetUp (void) override
		{
			mock_usb.reset ();
		}

		void TearDown (void) override
		{
			EXPECT_TRUE (mock_usb.pending.empty ());
			EXPECT_FALSE (mock_usb.freed_pending);
		}

		static ftdi_context context (const uint32_t packet_size)
		{
			ftdi_context ret {};
			ret.usb_ctx = nullptr; /* libusb's default context is valid here. */
			ret.usb_dev = fake_handle;
			ret.max_packet_size = packet_size;
			ret.writebuffer_chunksize = packet_size;
			ret.out_ep = 0x81;
			ret.in_ep = 0x02;
			return ret;
		}
	};
}

extern "C"
{
	void __real_libusb_free_transfer (libusb_transfer *transfer);

	int __wrap_libusb_init (libusb_context **context)
	{
		++mock_usb.init_calls;
		*context = reinterpret_cast<libusb_context *> (0x4321);
		return LIBUSB_SUCCESS;
	}

	void __wrap_libusb_exit (libusb_context *)
	{
		++mock_usb.exit_calls;
	}

	int __wrap_libusb_submit_transfer (libusb_transfer *transfer)
	{
		++mock_usb.submit_calls;
		if (mock_usb.fail_submit_call == mock_usb.submit_calls) {
			return LIBUSB_ERROR_IO;
		}
		if (false == mock_usb.pending.insert (transfer).second) {
			return LIBUSB_ERROR_BUSY;
		}
		return LIBUSB_SUCCESS;
	}

	int __wrap_libusb_cancel_transfer (libusb_transfer *transfer)
	{
		if (0 == mock_usb.pending.count (transfer)) {
			return LIBUSB_ERROR_NOT_FOUND;
		}
		mock_usb.cancelling.insert (transfer);
		return LIBUSB_SUCCESS;
	}

	static int mock_handle_events (void)
	{
		++mock_usb.drain_calls;
		if (mock_usb.handle_failures > 0) {
			--mock_usb.handle_failures;
			return LIBUSB_ERROR_IO;
		}
		if (mock_usb.drain_calls <= mock_usb.cancellation_delay) {
			return LIBUSB_SUCCESS;
		}

		const std::vector<libusb_transfer *> ready (mock_usb.cancelling.begin (), mock_usb.cancelling.end ());
		for (libusb_transfer *transfer : ready) {
			mock_usb.pending.erase (transfer);
			mock_usb.cancelling.erase (transfer);
			transfer->status = LIBUSB_TRANSFER_CANCELLED;
			transfer->actual_length = 0;
			transfer->callback (transfer);
		}
		return LIBUSB_SUCCESS;
	}

	int __wrap_libusb_handle_events_timeout (libusb_context *, timeval *)
	{
		return mock_handle_events ();
	}

	int __wrap_libusb_handle_events_timeout_completed (libusb_context *, timeval *, int *)
	{
		return mock_handle_events ();
	}

	int __wrap_libusb_pollfds_handle_timeouts (libusb_context *)
	{
		return 1;
	}

	const libusb_pollfd **__wrap_libusb_get_pollfds (libusb_context *)
	{
		return static_cast<const libusb_pollfd **> (::calloc (1, sizeof (libusb_pollfd *)));
	}

	void __wrap_libusb_free_pollfds (const libusb_pollfd **pollfds)
	{
		::free (const_cast<libusb_pollfd **> (pollfds));
	}

	void __wrap_libusb_set_pollfd_notifiers (libusb_context *, libusb_pollfd_added_cb, libusb_pollfd_removed_cb, void *)
	{ }

	void __wrap_libusb_free_transfer (libusb_transfer *transfer)
	{
		if (mock_usb.pending.count (transfer) > 0) {
			mock_usb.freed_pending = true;
			mock_usb.pending.erase (transfer);
			mock_usb.cancelling.erase (transfer);
		}
		__real_libusb_free_transfer (transfer);
	}

	int __wrap_libusb_open (libusb_device *, libusb_device_handle **handle)
	{
		++mock_usb.open_calls;
		*handle = fake_handle;
		return LIBUSB_SUCCESS;
	}

	void __wrap_libusb_close (libusb_device_handle *)
	{
		++mock_usb.close_calls;
	}

	int __wrap_libusb_get_device_descriptor (libusb_device *, libusb_device_descriptor *descriptor)
	{
		::memset (descriptor, 0, sizeof (*descriptor));
		return mock_usb.descriptor_result;
	}

	int __wrap_libusb_get_string_descriptor_ascii (libusb_device_handle *, uint8_t, unsigned char *data, int length)
	{
		if (length > 0) {
			data[0] = '\0';
		}
		return 0;
	}
}

static_assert (false == std::is_copy_constructible_v<FtdiContext>);
static_assert (false == std::is_copy_assignable_v<FtdiContext>);

TEST_F (StreamFixture, borrowed_null_context_uses_libusb_default_context)
{
	FtdiContext context (false);
	try {
		(void) context.init (nullptr);
		FAIL () << "An empty device configuration must not initialize successfully";
	}
	catch (const shaga::CommonException &e) {
		EXPECT_STREQ ("Unable to find usb device", e.what ());
	}
	EXPECT_EQ (nullptr, context.get_context ());
	EXPECT_EQ (nullptr, context.get_libusb_context ());
	EXPECT_FALSE (context.created_libusb_context ());
}

TEST_F (StreamFixture, owned_and_borrowed_context_failure_cleanup_matches_ownership)
{
	{
		FtdiContext owned (true);
		EXPECT_THROW ((void) owned.init (), shaga::CommonException);
		EXPECT_EQ (1, mock_usb.init_calls);
		EXPECT_EQ (1, mock_usb.exit_calls);
		EXPECT_EQ (nullptr, owned.get_context ());
		EXPECT_EQ (nullptr, owned.get_libusb_context ());
		EXPECT_FALSE (owned.created_libusb_context ());
	}

	mock_usb.reset ();
	{
		FtdiContext borrowed (false);
		auto *external = reinterpret_cast<libusb_context *> (0x8765);
		EXPECT_THROW ((void) borrowed.init (external), shaga::CommonException);
		EXPECT_EQ (0, mock_usb.init_calls);
		EXPECT_EQ (0, mock_usb.exit_calls);
		EXPECT_EQ (nullptr, borrowed.get_context ());
		EXPECT_EQ (nullptr, borrowed.get_libusb_context ());
		EXPECT_FALSE (borrowed.created_libusb_context ());
	}
}

TEST_F (StreamFixture, null_ftdi_and_mixed_usb_contexts_are_rejected)
{
	FtdiStreams null_streams;
	null_streams.emplace_back (nullptr);
	EXPECT_THROW (FtdiStream stream (null_streams), shaga::CommonException);

	ftdi_context first = context (64);
	ftdi_context second = context (64);
	first.usb_ctx = reinterpret_cast<libusb_context *> (0x1111);
	second.usb_ctx = reinterpret_cast<libusb_context *> (0x2222);
	FtdiStreams mixed;
	mixed.emplace_back (&first);
	mixed.emplace_back (&second);
	EXPECT_THROW (FtdiStream stream (mixed), shaga::CommonException);
}

TEST_F (StreamFixture, transfer_size_arithmetic_is_checked_before_allocation)
{
	ftdi_context ftdi = context (UINT32_MAX);
	FtdiStreams streams;
	auto &entry = streams.emplace_back (&ftdi);
	entry.set_read_callback ([](const FtdiStreamEntry::CallbackType type, char *, const int) -> int {
		return (FtdiStreamEntry::CallbackType::READ_GET_FD == type) ? -1 : 0;
	});
	entry.set_reset_callback ([](ftdi_context *) {});
	entry.set_read_transfers (2, 1);

	FtdiStream stream (streams);
	EXPECT_THROW (stream.start_poll (), shaga::CommonException);
	EXPECT_TRUE (mock_usb.pending.empty ());
}

TEST_F (StreamFixture, delayed_cancellation_is_drained_before_free_and_restart)
{
	ftdi_context ftdi = context (64);
	FtdiStreams streams;
	auto &entry = streams.emplace_back (&ftdi);
	entry.set_read_callback ([](const FtdiStreamEntry::CallbackType type, char *, const int) -> int {
		return (FtdiStreamEntry::CallbackType::READ_GET_FD == type) ? -1 : 0;
	});
	entry.set_reset_callback ([](ftdi_context *) {});
	entry.set_read_transfers (1, 1);

	FtdiStream stream (streams);
	mock_usb.cancellation_delay = 6;
	stream.start_poll ();
	ASSERT_EQ (1U, mock_usb.pending.size ());
	stream.stop_poll ();
	EXPECT_GE (mock_usb.drain_calls, 7);
	EXPECT_TRUE (mock_usb.pending.empty ());

	mock_usb.cancellation_delay = mock_usb.drain_calls;
	stream.start_poll ();
	ASSERT_EQ (1U, mock_usb.pending.size ());
	stream.stop_poll ();
}

TEST_F (StreamFixture, throwing_counter_callback_still_cleans_poll_state)
{
	ftdi_context ftdi = context (64);
	int reset_calls {0};
	bool throw_counter {true};
	FtdiStreams streams;
	auto &entry = streams.emplace_back (&ftdi);
	entry.set_read_callback ([](const FtdiStreamEntry::CallbackType type, char *, const int) -> int {
		return (FtdiStreamEntry::CallbackType::READ_GET_FD == type) ? -1 : 0;
	});
	entry.set_counter_callback ([&](const bool, const uint_fast32_t, const uint_fast32_t, const uint_fast32_t) {
		if (true == std::exchange (throw_counter, false)) {
			throw std::runtime_error ("counter callback failure");
		}
	});
	entry.set_reset_callback ([&](ftdi_context *) {
		++reset_calls;
	});
	entry.set_read_transfers (1, 1);

	FtdiStream stream (streams);
	stream.start_poll ();
	::usleep (1'000);
	EXPECT_THROW (stream.stop_poll (), std::runtime_error);
	EXPECT_TRUE (mock_usb.pending.empty ());
	EXPECT_FALSE (mock_usb.freed_pending);
	EXPECT_GE (mock_usb.drain_calls, 1);
	EXPECT_GE (reset_calls, 2);

	stream.start_poll ();
	ASSERT_EQ (1U, mock_usb.pending.size ());
	stream.stop_poll ();
	EXPECT_TRUE (mock_usb.pending.empty ());
}

TEST_F (StreamFixture, destructor_contains_throwing_counter_callback_after_cleanup)
{
	EXPECT_EXIT ({
		mock_usb.reset ();
		ftdi_context ftdi = context (64);
		int reset_calls {0};
		FtdiStreams streams;
		auto &entry = streams.emplace_back (&ftdi);
		entry.set_read_callback ([](const FtdiStreamEntry::CallbackType type, char *, const int) -> int {
			return (FtdiStreamEntry::CallbackType::READ_GET_FD == type) ? -1 : 0;
		});
		entry.set_counter_callback ([](const bool, const uint_fast32_t, const uint_fast32_t, const uint_fast32_t) {
			throw std::runtime_error ("counter callback failure");
		});
		entry.set_reset_callback ([&](ftdi_context *) {
			++reset_calls;
		});
		entry.set_read_transfers (1, 1);
		{
			FtdiStream stream (streams);
			stream.start_poll ();
			::usleep (1'000);
		}
		if (false == mock_usb.pending.empty () || true == mock_usb.freed_pending || mock_usb.drain_calls < 1 || reset_calls < 2) {
			::_exit (1);
		}
		::_exit (0);
	}, ::testing::ExitedWithCode (0), "");
}

TEST_F (StreamFixture, partial_initialization_is_cancelled_and_drained)
{
	ftdi_context ftdi = context (64);
	FtdiStreams streams;
	auto &entry = streams.emplace_back (&ftdi);
	entry.set_read_callback ([](const FtdiStreamEntry::CallbackType type, char *, const int) -> int {
		return (FtdiStreamEntry::CallbackType::READ_GET_FD == type) ? -1 : 0;
	});
	entry.set_reset_callback ([](ftdi_context *) {});
	entry.set_read_transfers (1, 2);

	FtdiStream stream (streams);
	mock_usb.fail_submit_call = 2;
	mock_usb.cancellation_delay = 5;
	mock_usb.handle_failures = 2;
	EXPECT_THROW (stream.start_poll (), shaga::CommonException);
	EXPECT_TRUE (mock_usb.pending.empty ());
	EXPECT_GE (mock_usb.drain_calls, 6);
}

TEST_F (StreamFixture, mixed_endpoint_packet_sizes_strip_only_real_status_boundaries)
{
	ftdi_context small = context (64);
	ftdi_context large = context (512);
	std::vector<std::string> received (2);
	FtdiStreams streams;
	for (size_t i = 0; i < 2; ++i) {
		auto &entry = streams.emplace_back ((0 == i) ? &small : &large);
		entry.set_read_callback ([&, i](const FtdiStreamEntry::CallbackType type, char *buffer, const int length) -> int {
			if (FtdiStreamEntry::CallbackType::READ_BUFFER == type) {
				received[i].append (buffer, length);
			}
			return (FtdiStreamEntry::CallbackType::READ_GET_FD == type) ? -1 : 0;
		});
		entry.set_reset_callback ([](ftdi_context *) {});
		entry.set_read_transfers (1, 1);
	}

	FtdiStream stream (streams);
	stream.start_poll ();
	ASSERT_EQ (2U, mock_usb.pending.size ());
	libusb_transfer *large_transfer = *std::next (mock_usb.pending.begin ());
	if (large_transfer->length != 512) {
		large_transfer = *mock_usb.pending.begin ();
	}
	ASSERT_EQ (512, large_transfer->length);
	for (int i = 0; i < 512; ++i) {
		large_transfer->buffer[i] = static_cast<unsigned char> (i);
	}
	complete_transfer (large_transfer, LIBUSB_TRANSFER_COMPLETED, 512);
	ASSERT_EQ (510U, received[1].size ());
	EXPECT_EQ (static_cast<char> (2), received[1][0]);
	EXPECT_EQ (static_cast<char> (64), received[1][62]);

	libusb_transfer *small_transfer = nullptr;
	for (libusb_transfer *candidate : mock_usb.pending) {
		if (64 == candidate->length) {
			small_transfer = candidate;
			break;
		}
	}
	ASSERT_NE (nullptr, small_transfer);
	complete_transfer (small_transfer, LIBUSB_TRANSFER_COMPLETED, 2);
	EXPECT_TRUE (received[0].empty ());
	complete_transfer (small_transfer, LIBUSB_TRANSFER_COMPLETED, 1);
	EXPECT_TRUE (received[0].empty ());
	stream.stop_poll ();
}

TEST_F (StreamFixture, modem_only_packet_is_delivered_when_requested)
{
	ftdi_context ftdi = context (64);
	std::string received;
	FtdiStreams streams;
	auto &entry = streams.emplace_back (&ftdi);
	entry.set_read_include_modem_status (true);
	entry.set_read_callback ([&](const FtdiStreamEntry::CallbackType type, char *buffer, const int length) -> int {
		if (FtdiStreamEntry::CallbackType::READ_BUFFER == type) {
			received.assign (buffer, length);
		}
		return (FtdiStreamEntry::CallbackType::READ_GET_FD == type) ? -1 : 0;
	});
	entry.set_reset_callback ([](ftdi_context *) {});
	entry.set_read_transfers (1, 1);

	FtdiStream stream (streams);
	stream.start_poll ();
	libusb_transfer *transfer = *mock_usb.pending.begin ();
	transfer->buffer[0] = 0x10;
	transfer->buffer[1] = 0x60;
	complete_transfer (transfer, LIBUSB_TRANSFER_COMPLETED, 2);
	ASSERT_EQ (2U, received.size ());
	EXPECT_EQ (static_cast<char> (0x10), received[0]);
	EXPECT_EQ (static_cast<char> (0x60), received[1]);
	stream.stop_poll ();
}

TEST_F (StreamFixture, write_error_confirms_only_reported_partial_bytes_and_stops)
{
	ftdi_context ftdi = context (64);
	int fill_calls {0};
	int confirmed {0};
	FtdiStreams streams;
	auto &entry = streams.emplace_back (&ftdi);
	entry.set_write_callback ([&](const FtdiStreamEntry::CallbackType type, char *buffer, const int length) -> int {
		if (FtdiStreamEntry::CallbackType::WRITE_GET_FD == type) {
			return -1;
		}
		if (FtdiStreamEntry::CallbackType::WRITE_CONFIRM_TRANSFER == type) {
			confirmed += length;
			return 0;
		}
		++fill_calls;
		::memset (buffer, 0xA5, 8);
		return 8;
	});
	entry.set_reset_callback ([](ftdi_context *) {});
	entry.set_write_transfers (1, 1);

	FtdiStream stream (streams);
	stream.start_poll ();
	ASSERT_EQ (1U, mock_usb.pending.size ());
	libusb_transfer *transfer = *mock_usb.pending.begin ();
	complete_transfer (transfer, LIBUSB_TRANSFER_ERROR, 3);
	EXPECT_EQ (3, confirmed);
	EXPECT_EQ (1, fill_calls);
	EXPECT_TRUE (stream.is_ending ());
	EXPECT_FALSE (stream.get_errors ().empty ());
	stream.stop_poll ();
}

TEST_F (StreamFixture, oversized_write_fill_is_rejected_on_initial_and_resubmit_paths)
{
	ftdi_context ftdi = context (64);
	int fill_calls {0};
	FtdiStreams streams;
	auto &entry = streams.emplace_back (&ftdi);
	entry.set_write_callback ([&](const FtdiStreamEntry::CallbackType type, char *, const int length) -> int {
		if (FtdiStreamEntry::CallbackType::WRITE_GET_FD == type) {
			return -1;
		}
		if (FtdiStreamEntry::CallbackType::WRITE_CONFIRM_TRANSFER == type) {
			return 0;
		}
		return (0 == fill_calls++) ? 8 : length + 1;
	});
	entry.set_reset_callback ([](ftdi_context *) {});
	entry.set_write_transfers (1, 1);

	FtdiStream stream (streams);
	stream.start_poll ();
	libusb_transfer *transfer = *mock_usb.pending.begin ();
	complete_transfer (transfer, LIBUSB_TRANSFER_COMPLETED, 8);
	EXPECT_TRUE (stream.is_ending ());
	EXPECT_FALSE (stream.get_errors ().empty ());
	stream.stop_poll ();

	mock_usb.reset ();
	fill_calls = 1;
	FtdiStream initial_stream (streams);
	EXPECT_THROW (initial_stream.start_poll (), shaga::CommonException);
}

TEST_F (StreamFixture, get_strings_closes_temporary_handle_on_descriptor_error_and_checks_lengths)
{
	ftdi_context ftdi {};
	auto *device = reinterpret_cast<libusb_device *> (0x5678);
	mock_usb.descriptor_result = LIBUSB_ERROR_IO;
	EXPECT_EQ (-11, ftdi_usb_get_strings_ex (&ftdi, device, nullptr, 0, nullptr, 0, nullptr, 0));
	EXPECT_EQ (1, mock_usb.open_calls);
	EXPECT_EQ (1, mock_usb.close_calls);
	EXPECT_EQ (nullptr, ftdi.usb_dev);

	char output = 'x';
	EXPECT_EQ (-1, ftdi_usb_get_strings_ex (&ftdi, device, &output, 0, nullptr, 0, nullptr, 0));
	EXPECT_EQ ('x', output);
	EXPECT_EQ (1, mock_usb.open_calls);
}

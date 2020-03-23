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
#ifndef _HEAD_SGFTDI_internal
#define _HEAD_SGFTDI_internal

#ifndef SGFTDI
	#error You must include sgftdi*.h
#endif // SGFTDI

#include <sys/epoll.h>

/* Multimap from file descriptor to stream state */
typedef std::unordered_multimap<int, FtdiStreamStaticState> FtdiStreamStaticStates_t;

class FtdiStreamState
{
	private:
		FtdiStreams streams;
		const uint_fast32_t num_streams;

		struct libusb_context *usb_ctx {nullptr};
		int notice_event_fd {-1};

		std::unique_ptr<FtdiStreamStaticStates_t> streamstates;

		int epoll_fd {-1};
		int usb_epoll_fd {-1};
		int timer_fd {-1};

		/* Set from ftdi->max_packet_size, usually 64 or 512 bytes */
		uint32_t read_packetsize {UINT32_MAX};

		/* Set from ftdi->writebuffer_chunksize, usually 4096 bytes */
		uint32_t write_packetsize {UINT32_MAX};

		/* Timeout in secounds without any activity */
		uint_fast64_t timeout {10};

		/* Number of cancel loops unilt forced exit */
		int cancel_counter {3};

		/* Is thread started */
		volatile bool is_started_thr {false};

		/* Is polling version started */
		volatile bool is_started_poll {false};

		/* This is used only by ftdistream_internal thread */
		/* When false, thread should end correctly */
		volatile bool should_run {true};

		/* This is used to control ftdistream_internal thread from FtdiStream */
		/* Can be set by both threads */
		#ifdef SHAGA_THREADING
		std::atomic<bool> should_cancel {false};
		#else
		bool should_cancel {false};
		#endif // SHAGA_THREADING

		volatile uint_fast64_t ts_now {0};
		volatile uint_fast64_t ts_activity {0};

		struct timeval libusb_timeout {0, 0};
		static const constexpr int num_epoll_events {512};
		struct epoll_event epoll_events[num_epoll_events];

		#ifdef SHAGA_THREADING
		std::mutex list_mutex;
		#endif // SHAGA_THREADING

		/* These lists use file descriptors */
		std::unordered_set<int> list_enable;
		std::unordered_set<int> list_disable;

		/* This list used stream_id */
		std::unordered_set<uint_fast32_t> list_reset;

		shaga::StringSPSC error_spsc;

	public:
		explicit FtdiStreamState (FtdiStreams &_streams);
		~FtdiStreamState ();

		/* Non-copyable */
		FtdiStreamState (FtdiStreamState const&) = delete;
		FtdiStreamState& operator= (FtdiStreamState const&) = delete;

		void issue_notice (void) noexcept;

		friend class FtdiStream;
		friend class FtdiStreamStatic;
		friend class FtdiStreamStaticState;
};

class FtdiStreamStaticState
{
	private:
		const uint_fast32_t stream_id;
		const uint_fast32_t transfer_id;
		const bool is_reading;
		const bool is_modem_status;
		const int eventfd;
		FtdiStreamState * const state;
		struct libusb_transfer * transfer {nullptr};

		int buffer_size {0};
		volatile bool enabled {false};
		volatile uint_fast32_t counter_callbacks {0};
		volatile uint_fast32_t counter_bytes {0};

	public:
		explicit FtdiStreamStaticState (
			const uint_fast32_t _stream_id,
			const uint_fast32_t _transfer_id,
			const bool _is_reading,
			const bool _is_modem_status,
			const int _eventfd,
			FtdiStreamState * const _state);

		~FtdiStreamStaticState ();

		void init (FtdiStreamEntry &stream);
		void submit (void);
		void cancel (void);

		friend class FtdiStreamStatic;
};

#endif // _HEAD_SGFTDI_internal

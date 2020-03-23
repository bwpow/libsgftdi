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
#ifndef _HEAD_SGFTDI_ftdistream
#define _HEAD_SGFTDI_ftdistream

#ifndef SGFTDI
	#error You must include sgftdi*.h
#endif // SGFTDI

extern "C"
{
	int ftdi_init_ex (struct ftdi_context *ftdi, struct libusb_context *usb_ctx);
	struct ftdi_context *ftdi_new_ex (struct libusb_context *usb_ctx);
	void ftdi_deinit_ex (struct ftdi_context *ftdi);
	void ftdi_free_ex (struct ftdi_context *ftdi);
	int ftdi_usb_get_strings_ex (struct ftdi_context *ftdi, struct libusb_device *dev, char *manufacturer, int mnf_len, char *description, int desc_len, char *serial, int serial_len);
}

class FtdiStreamStatic;
class FtdiStreamStaticState;
class FtdiStreamState;

class FtdiContext
{
	public:
		struct Config {
			int speed {115'200};
			uint8_t device {0};
			uint8_t port {0};

			enum ftdi_bits_type databits = BITS_8;
			enum ftdi_stopbits_type stopbits = STOP_BIT_1;
			enum ftdi_parity_type parity = ODD;

			enum class FlowControl : int {
				DISABLE_FLOW_CTRL = SIO_DISABLE_FLOW_CTRL,
				RTS_CTS = SIO_RTS_CTS_HS,
				DTR_DSR = SIO_DTR_DSR_HS,
				XON_XOFF = SIO_XON_XOFF_HS
			} flow {FlowControl::DISABLE_FLOW_CTRL};
		};

	private:
		struct ftdi_context *_ctx {nullptr};
		struct libusb_context *_usb_ctx {nullptr};
		const bool _create_libusb_context {true};

		Config _config;

		void set_ftdi_params (void);

	public:
		explicit FtdiContext (const bool create_libusb_context = true);
		~FtdiContext ();

		void populate_config (const shaga::INI &ini, const std::string_view section);
		void populate_config (std::shared_ptr<shaga::INI> ini, const std::string_view section);
		void populate_config (const shaga::INI *const ini, const std::string_view section);

		/* If you pass nullptr to init function and create_libusb_context == true, libusb context will be created */
		struct ftdi_context * init (struct libusb_context *usb_ctx = nullptr);

		/* Will destroy ftdi context and also libusb context, if created */
		void clear (void);

		/* Return context or nullptr if no context has been generated yet */
		struct ftdi_context * get_context (void) const noexcept;
		struct libusb_context * get_libusb_context (void) const noexcept;

		Config & get_config (void)
		{
			return _config;
		}

};

class FtdiStreamEntry
{
	public:
		enum class CallbackType {
			/* Return file descriptor to poll when data is ready */
			/* Ignore 'buffer' and 'len' */
			WRITE_GET_FD,
			READ_GET_FD,

			/* Fill 'buffer' with up to 'len' bytes and return real length of data */
			/* Return zero if no data is ready, negative number in case of error */
			WRITE_FILL_BUFFER,

			/* Confirmation, that 'len' bytes of data have been sent and front of the buffer may move */
			/* Return zero on success, non-zero otherwise */
			/* Ignore 'buffer' parameter */
			WRITE_CONFIRM_TRANSFER,

			/* Got' buffer' filled with 'len' bytes. */
			/* If modem status is included, it is stored in the first two bytes of 'buffer' and 'len' is at least 2. */
			/* Return value is ignored */
			READ_BUFFER,
		};

		/*
			*** Modem status bytes ***
			Layout of the first byte:
			- B0..B3 - must be 0
			- B4       Clear to send (CTS)
						 0 = inactive
						 1 = active
			- B5       Data set ready (DTS)
						 0 = inactive
						 1 = active
			- B6       Ring indicator (RI)
						 0 = inactive
						 1 = active
			- B7       Receive line signal detect (RLSD)
						 0 = inactive
						 1 = active

			Layout of the second byte:
			- B0       Data ready (DR)
			- B1       Overrun error (OE)
			- B2       Parity error (PE)
			- B3       Framing error (FE)
			- B4       Break interrupt (BI)
			- B5       Transmitter holding register (THRE)
			- B6       Transmitter empty (TEMT)
			- B7       Error in RCVR FIFO
		*/

		/* Important! Callbacks will be called from other thread. Don't forget about synchronization! */
		typedef std::function<int(const CallbackType type, char * const buffer, const int len)> Callback;
		typedef std::function<void(const bool is_reading, const uint_fast32_t tranfer_id, const uint_fast32_t cnt_callbacks, const uint_fast32_t cnt_bytes)> CounterCallback;
		typedef std::function<void(void)> ResetCallback;

	private:
		struct ftdi_context * const ftdi {nullptr};

		bool read_start_enabled {true};
		bool read_include_modem_status {false};

		Callback read_callback {nullptr};
		uint_fast32_t read_transfers {0};
		uint_fast32_t read_packets_per_transfer {0};

		Callback write_callback {nullptr};
		uint_fast32_t write_transfers {0};
		uint_fast32_t write_packets_per_transfer {0};

		CounterCallback counter_callback {nullptr};
		ResetCallback reset_callback {nullptr};

	public:
		explicit FtdiStreamEntry (struct ftdi_context *_ftdi);

		/* Start reading immediately, default = yes */
		void set_read_start_enabled (const bool enabled);

		/* Include modem status bytes, default = no */
		void set_read_include_modem_status (const bool enabled);

		void set_read_transfers (const uint_fast32_t packets_per_transfer = 1, const uint_fast32_t transfers = 1);
		void set_write_transfers (const uint_fast32_t packets_per_transfer = 1, const uint_fast32_t transfers = 1);

		void set_callback (Callback callback);
		void set_read_callback (Callback callback);
		void set_write_callback (Callback callback);

		void set_counter_callback (CounterCallback callback);
		void set_reset_callback (ResetCallback callback);

		friend class FtdiStream;
		friend class FtdiStreamState;
		friend class FtdiStreamStatic;
		friend class FtdiStreamStaticState;
};

typedef std::vector<FtdiStreamEntry> FtdiStreams;

class FtdiStream
{
	private:
		std::unique_ptr<FtdiStreamState> _state;
		FtdiStreamState *_naked_state {nullptr};

		#ifdef SHAGA_THREADING
		std::thread _thr;
		std::mutex _mutex;
		#endif // SHAGA_THREADING

	public:
		explicit FtdiStream (FtdiStreams &streams);
		~FtdiStream ();

		/* Non-copyable */
		FtdiStream (FtdiStream const&) = delete;
		FtdiStream& operator= (FtdiStream const&) = delete;

		/* Thread unsafe methods, don't use while active */
		void set_timeout (const uint_fast64_t timeout);
		uint_fast64_t get_timeout (void) const;

		/* Thread safe methods */
		size_t get_errors (shaga::COMMON_LIST &append_to_lst);
		shaga::COMMON_LIST get_errors (void);
		size_t print_errors (const std::string_view prefix = ""sv);
		bool is_ending (void) const;

		/* Polling version */
		int get_poll_fd (void);
		void start_poll (void);
		void stop_poll (void);
		bool poll (const int timeout = 0);

		/* Threading version */
		void start_thread (void);
		void stop_thread (void);

		void enable_reading (const uint_fast32_t stream_id);
		void disable_reading (const uint_fast32_t stream_id);
		void reset_stream (const uint_fast32_t stream_id);

		bool is_started_thread (void) const;
		bool is_active_thread (void) const;
};

#endif // _HEAD_vdudump_ftdistream

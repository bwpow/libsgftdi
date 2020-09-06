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

bool FtdiContext::get_string_descriptor_ascii (libusb_device_handle *devh, uint8_t desc_idx, std::string &str)
{
	str.resize (512);

	const int ret = ::libusb_get_string_descriptor_ascii (devh, desc_idx, reinterpret_cast<unsigned char *> (str.data ()), str.size ());
	if (ret < 0) {
		str.clear ();
		return false;
	}
	else {
		str.resize (ret);
		return true;
	}
}

void FtdiContext::set_ftdi_params (void)
{
	int ret;

	ret = ::ftdi_usb_reset (_ctx);
	if (0 != ret) {
		cThrow ("Unable to reset device"sv);
	}

	ret = ::ftdi_set_baudrate (_ctx, _config.speed);
	if (0 != ret) {
		cThrow ("Unable to set baudrate"sv);
	}

	ret = ::ftdi_set_line_property (_ctx, _config.databits, _config.stopbits, _config.parity);
	if (0 != ret) {
		cThrow ("Unable to set parameters"sv);
	}

	ret = ::ftdi_setflowctrl (_ctx, static_cast<int> (_config.flow));
	if (0 != ret) {
		cThrow ("Unable to set flow control"sv);
	}
}

FtdiContext::FtdiContext (const bool create_libusb_context) :
	_create_libusb_context (create_libusb_context)
{ }

FtdiContext::~FtdiContext ()
{
	clear ();
}

void FtdiContext::populate_config (const shaga::INI &ini, const std::string_view section)
{
	populate_config (&ini, section);
}

void FtdiContext::populate_config (std::shared_ptr<shaga::INI> ini, const std::string_view section)
{
	populate_config (ini.get (), section);
}

void FtdiContext::populate_config (const shaga::INI *const ini, const std::string_view section)
{
	const uint32_t baudrate = ini->get_uint32 (section, "baudrate"sv, 0);
	if (baudrate > 0) {
		_config.speed = baudrate;
	}

	const std::string_view parity = ini->get_string (section, "parity"sv);
	if (parity.empty () == false) {
		if (STR::icompare (parity, "odd"sv)) {
			_config.parity = ODD;
		}
		else if (STR::icompare (parity, "even"sv)) {
			_config.parity = EVEN;
		}
		else if (STR::icompare (parity, "none"sv)) {
			_config.parity = NONE;
		}
		else {
			cThrow ("Undefined parity '{}'. Possible values are 'odd', 'even' and 'none'."sv, parity);
		}
	}

	const std::string_view flowctrl = ini->get_string (section, "flowctrl"sv);
	if (flowctrl.empty () == false) {
		if (STR::icompare (flowctrl, "rts_cts"sv)) {
			_config.flow = Config::FlowControl::RTS_CTS;
		}
		else if (STR::icompare (flowctrl, "dtr_dsr"sv)) {
			_config.flow = Config::FlowControl::DTR_DSR;
		}
		else if (STR::icompare (flowctrl, "xon_xoff"sv)) {
			_config.flow = Config::FlowControl::XON_XOFF;
		}
		else if (STR::icompare (flowctrl, "off"sv)) {
			_config.flow = Config::FlowControl::DISABLE_FLOW_CTRL;
		}
		else {
			cThrow ("Undefined flow control '{}'. Possible values are 'rts_cts', 'dtr_dsr', 'xon_xoff' and 'off'."sv, parity);
		}
	}

	const uint32_t databits = ini->get_uint32 (section, "databits"sv, UINT8_MAX);
	if (databits != UINT8_MAX) {
		switch (databits) {
			case 7:
				_config.databits = BITS_7;
				break;
			case 8:
				_config.databits = BITS_8;
				break;
			default:
				cThrow ("Undefined databits value '{}'. Possible values are '7' and '8'."sv, databits);
		}
	}

	const uint32_t stopbits = ini->get_uint32 (section, "stopbits"sv, UINT8_MAX);
	if (stopbits != UINT8_MAX) {
		switch (stopbits) {
			case 1:
				_config.stopbits = STOP_BIT_1;
				break;
			case 2:
				_config.stopbits = STOP_BIT_2;
				break;
			default:
				cThrow ("Undefined stopbits value '{}'. Possible values are '1' and '2'."sv, databits);
		}
	}

	const auto usb_vendor = STR::to_uint16 (ini->get_string (section, "usb_vendor"sv), 16);
	if (usb_vendor != 0) {
		_config.usb_vendor = usb_vendor;
	}

	const auto usb_product = STR::to_uint16 (ini->get_string (section, "usb_product"sv), 16);
	if (usb_product != 0) {
		_config.usb_product = usb_product;
	}

	const uint8_t device = ini->get_uint8 (section, "usb_device"sv, UINT8_MAX);
	if (device != UINT8_MAX) {
		_config.usb_device = device;
	}

	const uint8_t port = ini->get_uint8 (section, "ftdi_port"sv, UINT8_MAX);
	if (port != UINT8_MAX) {
		_config.ftdi_port = port;
	}
}

struct ftdi_context * FtdiContext::init (struct libusb_context *usb_ctx) try
{
	int ret;

	clear ();

	if (nullptr == usb_ctx) {
		if (false == _create_libusb_context) {
			cThrow ("USB context is not provided"sv);
		}
		else {
			ret = ::libusb_init (&_usb_ctx);
			if (ret != 0) {
				cThrow ("Unable to init USB: {}"sv, ::libusb_error_name (ret));
			}
			::libusb_set_pollfd_notifiers (_usb_ctx, nullptr, nullptr, nullptr);
			_ctx = ::ftdi_new_ex (_usb_ctx);
		}
	}
	else {
		_ctx = ::ftdi_new_ex (usb_ctx);
	}

	if (nullptr == _ctx) {
		cThrow ("Unable to allocate FTDI context"sv);
	}

	switch (_config.ftdi_port) {
		case 0:
			if (::ftdi_set_interface (_ctx, INTERFACE_A) != 0) {
				cThrow ("Unable to set interface port A"sv);
			}
			break;

		case 1:
			if (::ftdi_set_interface (_ctx, INTERFACE_B) != 0) {
				cThrow ("Unable to set interface port B"sv);
			}
			break;

		case 2:
			if (::ftdi_set_interface (_ctx, INTERFACE_C) != 0) {
				cThrow ("Unable to set interface port C"sv);
			}
			break;

		case 3:
			if (::ftdi_set_interface (_ctx, INTERFACE_D) != 0) {
				cThrow ("Unable to set interface port D"sv);
			}
			break;

		default:
			cThrow ("Bad port number {}"sv, _config.ftdi_port);
	}

	struct ftdi_device_list *devlist = nullptr;
	struct ftdi_device_list *curdev = nullptr;
	struct libusb_device *usbdev = nullptr;

	ret = ::ftdi_usb_find_all (_ctx, &devlist, _config.usb_vendor, _config.usb_product);
	if (ret < 0) {
		cThrow ("ftdi_usb_find_all failed: {} ({})"sv, ret, ::ftdi_get_error_string (_ctx));
	}

	try {
		uint32_t index = 0;
		for (curdev = devlist; curdev != nullptr; ++index) {
			if (index == _config.usb_device) {
				usbdev = curdev->dev;
				break;
			}
			curdev = curdev->next;
		}

		if (nullptr == usbdev) {
			cThrow ("Unable to find device {}"sv, _config.usb_device);
		}

		ret = ::ftdi_usb_open_dev (_ctx, usbdev);
		if (ret < 0) {
			cThrow ("Unable to open device {}: {}"sv, _config.usb_device, ::ftdi_get_error_string (_ctx));
		}

		struct libusb_device_descriptor desc;
		if (::libusb_get_device_descriptor (usbdev, &desc) != 0) {
			cThrow ("Unable to get device {} descriptor"sv, _config.usb_device);
		}

		get_string_descriptor_ascii (_ctx->usb_dev, desc.iManufacturer, _manufacturer);
		get_string_descriptor_ascii (_ctx->usb_dev, desc.iProduct, _description);
		get_string_descriptor_ascii (_ctx->usb_dev, desc.iSerialNumber, _serial);

		::ftdi_list_free (&devlist);
	}
	catch (...) {
		if (devlist != nullptr) {
			::ftdi_list_free (&devlist);
		}
		throw;
	}

	set_ftdi_params ();

	return _ctx;
}
catch (...)
{
	if (nullptr != _ctx) {
		::ftdi_free_ex (_ctx);
		_ctx = nullptr;
	}

	if (nullptr != _usb_ctx) {
		::libusb_exit (_usb_ctx);
		_usb_ctx = nullptr;
	}

	throw;
}

void FtdiContext::clear (void)
{
	if (nullptr != _ctx) {
		::ftdi_free_ex (_ctx);
		_ctx = nullptr;
	}

	if (nullptr != _usb_ctx) {
		::libusb_exit (_usb_ctx);
		_usb_ctx = nullptr;
	}
}

struct ftdi_context * FtdiContext::get_context (void) const noexcept
{
	return _ctx;
}

struct libusb_context * FtdiContext::get_libusb_context (void) const noexcept
{
	return _usb_ctx;
}

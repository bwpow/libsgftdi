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

/* This file should be included from ftdi.c right before ftdi_init function. */

int ftdi_init_ex (struct ftdi_context *ftdi, struct libusb_context *usb_ctx)
{
	struct ftdi_eeprom* eeprom = (struct ftdi_eeprom *) malloc (sizeof (struct ftdi_eeprom));
	ftdi->usb_ctx = usb_ctx;
	ftdi->usb_dev = NULL;
	ftdi->usb_read_timeout = 5000;
	ftdi->usb_write_timeout = 5000;

	ftdi->type = TYPE_BM;    /* chip type */
	ftdi->baudrate = -1;
	ftdi->bitbang_enabled = 0;  /* 0: normal mode 1: any of the bitbang modes enabled */

	ftdi->readbuffer = NULL;
	ftdi->readbuffer_offset = 0;
	ftdi->readbuffer_remaining = 0;
	ftdi->writebuffer_chunksize = 4096;
	ftdi->max_packet_size = 0;
	ftdi->error_str = NULL;
	ftdi->module_detach_mode = AUTO_DETACH_SIO_MODULE;

	ftdi_set_interface (ftdi, INTERFACE_ANY);
	ftdi->bitbang_mode = 1; /* when bitbang is enabled this holds the number of the mode  */

	if (eeprom == NULL) {
		ftdi_error_return (-2, "Can't malloc struct ftdi_eeprom");
	}

	memset (eeprom, 0, sizeof(struct ftdi_eeprom));
	ftdi->eeprom = eeprom;

	/* All fine. Now allocate the readbuffer */
	return ftdi_read_data_set_chunksize (ftdi, 4096);
}

struct ftdi_context *ftdi_new_ex (struct libusb_context *usb_ctx)
{
	struct ftdi_context * ftdi = (struct ftdi_context *) malloc (sizeof (struct ftdi_context));

	if (ftdi == NULL) {
		return NULL;
	}

	memset (ftdi, 0, sizeof (struct ftdi_context));

	if (ftdi_init_ex (ftdi, usb_ctx) != 0) {
		free (ftdi);
		return NULL;
	}

	return ftdi;
}

void ftdi_deinit_ex (struct ftdi_context *ftdi)
{
	if (ftdi == NULL) {
		return;
	}

	ftdi_usb_close_internal (ftdi);

	if (ftdi->readbuffer != NULL) {
		free(ftdi->readbuffer);
		ftdi->readbuffer = NULL;
	}

	if (ftdi->eeprom != NULL) {
		if (ftdi->eeprom->manufacturer != NULL) {
			free (ftdi->eeprom->manufacturer);
			ftdi->eeprom->manufacturer = NULL;
		}

		if (ftdi->eeprom->product != NULL) {
			free (ftdi->eeprom->product);
			ftdi->eeprom->product = NULL;
		}

		if (ftdi->eeprom->serial != NULL) {
			free (ftdi->eeprom->serial);
			ftdi->eeprom->serial = NULL;
		}

		free (ftdi->eeprom);
		ftdi->eeprom = NULL;
	}

	ftdi->usb_ctx = NULL;
}

void ftdi_free_ex (struct ftdi_context *ftdi)
{
	ftdi_deinit_ex (ftdi);
	free (ftdi);
}

int ftdi_usb_get_strings_ex (struct ftdi_context *ftdi, struct libusb_device *dev, char *manufacturer, int mnf_len, char *description, int desc_len, char *serial, int serial_len)
{
	struct libusb_device_descriptor desc;

	if (NULL == ftdi || NULL == dev) {
		return -1;
	}

	const int need_open = (ftdi->usb_dev == NULL);
	if (need_open) {
		if (libusb_open (dev, &ftdi->usb_dev) < 0) {
			ftdi_error_return (-4, "libusb_open() failed");
		}
	}

	if (libusb_get_device_descriptor (dev, &desc) < 0) {
		ftdi_error_return (-11, "libusb_get_device_descriptor() failed");
	}

	if (manufacturer != NULL) {
		if (libusb_get_string_descriptor_ascii (ftdi->usb_dev, desc.iManufacturer, (unsigned char *)manufacturer, mnf_len) < 0) {
			manufacturer[0] = '\0';
		}
	}

	if (description != NULL) {
		if (libusb_get_string_descriptor_ascii (ftdi->usb_dev, desc.iProduct, (unsigned char *)description, desc_len) < 0) {
			description[0] = '\0';
		}
	}

	if (serial != NULL) {
		if (libusb_get_string_descriptor_ascii (ftdi->usb_dev, desc.iSerialNumber, (unsigned char *)serial, serial_len) < 0) {
			serial[0] = '\0';
		}
	}

	if (need_open) {
		ftdi_usb_close_internal (ftdi);
	}

	return 0;
}

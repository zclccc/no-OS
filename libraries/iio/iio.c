/***************************************************************************//**
 *   @file   iio.c
 *   @brief  Implementation of iio.
 *   This module implements read/write ops, required by libtinyiiod and further
 *   calls show/store functions, corresponding to device/channel/attribute.
 *   @author Cristian Pop (cristian.pop@analog.com)
********************************************************************************
 * Copyright 2019(c) Analog Devices, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Analog Devices, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *  - The use of this software may or may not infringe the patent rights
 *    of one or more patent holders.  This license does not release you
 *    from the requirement that you obtain separate licenses from these
 *    patent holders to use this software.
 *  - Use of the software either in source or binary form, must be run
 *    on or directly connected to an Analog Devices Inc. component.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT,
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL ANALOG DEVICES BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, INTELLECTUAL PROPERTY RIGHTS, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

/******************************************************************************/
/***************************** Include Files **********************************/
/******************************************************************************/

#include "iio.h"
#include "ctype.h"
#include "tinyiiod.h"
#include "util.h"
#include "list.h"
#include "error.h"
#include "errno.h"
#include "uart.h"
#include "tcp_socket.h"
#include "circular_buffer.h"

/******************************************************************************/
/********************** Macros and Constants Definitions **********************/
/******************************************************************************/

#define IIOD_PORT		30431
#define MAX_SOCKET_TO_HANDLE	4

/******************************************************************************/
/*************************** Types Declarations *******************************/
/******************************************************************************/

/**
 * @struct iio_interfaces
 * @brief Structure containing all interfaces.
 */
struct iio_interfaces {
	/** List containing all interfaces */
	struct iio_interface **interfaces;
	/** Number of Interfaces */
	uint8_t num_interfaces;
};

/**
 * @struct element_info
 * @brief Structure informations about a specific parameter.
 */
struct element_info {
	/** Device name */
	const char *device_name;
	/** Channel name */
	const char *channel_name;
	/** Attribute name */
	const char *attribute_name;
	/** If set, is an output channel */
	bool ch_out;
};

struct iio_desc {
	struct tinyiiod		*iiod;
	struct tinyiiod_ops	*iiod_ops;
	enum pysical_link_type	phy_type;
	struct uart_desc	*uart_desc;
	struct circular_buffer	*sockets;
	struct tcp_socket_desc	*current_sock;
	/* Phy desc will be the socket with the connected client */
	struct tcp_socket_desc	*server;
	struct list_desc	*interfaces_list;
};

/**
 * iio_read_attr(), iio_write_attr() functions, they need to know about iio_interfaces
 */
static struct iio_interfaces	*iio_interfaces = NULL;
static struct iio_desc		*g_desc;

/******************************************************************************/
/************************ Functions Definitions *******************************/
/******************************************************************************/

static inline int32_t _pop_sock(struct iio_desc *desc,
				struct tcp_socket_desc **sock)
{
	return cb_read(desc->sockets, sock, sizeof(*sock));
}

static inline int32_t _push_sock(struct iio_desc *desc,
				 struct tcp_socket_desc *sock)
{
	return cb_write(desc->sockets, &sock, sizeof(sock));
}

static inline int32_t _nb_active_sockets(struct iio_desc *desc)
{
	int32_t size;

	cb_size(desc->sockets, &size);

	return size / sizeof(struct tcp_socket_desc *);
}

/* Blocking until new socket available.
 * Will iterate through a list of sockets */
static int32_t _get_next_socket(struct iio_desc *desc)
{
	struct tcp_socket_desc	*sock;
	int32_t			ret;
	uint32_t		nb_active_sockets;

	/* Get all new waiting sockets.
	 * If none is available, wait until first connection */
	do {
		ret = socket_accept(desc->server, &sock);
		if (ret == -EAGAIN) {
			nb_active_sockets = _nb_active_sockets(desc);
			if (nb_active_sockets == 0)
				/* Wait until a connection exists */
				continue;
			else
				break;
		} else if (IS_ERR_VALUE(ret)) {
			return ret;
		}
		/* Add socket to queue */
		ret = _push_sock(desc, sock);
		if (IS_ERR_VALUE(ret))
			return ret;
	} while (true);

	ret = _pop_sock(desc, &desc->current_sock);
	if (IS_ERR_VALUE(ret)) {
		desc->current_sock = NULL;
		return ret;
	}

	return SUCCESS;
}

static int32_t network_read(const void *data, uint32_t len)
{
	uint32_t	i;
	int32_t		ret;

	if ((int32_t)g_desc->current_sock == -1)
		return -1;

	if (g_desc->current_sock == NULL) {
		ret = _get_next_socket(g_desc);
		if (IS_ERR_VALUE(ret))
			return ret;
	}

	i = 0;
	do {
		ret = socket_recv(g_desc->current_sock,
				  (void *)((uint8_t *)data + i), len - i);
		if (IS_ERR_VALUE(ret)) {
			*(int8_t *)data = '*';
			break;
		}

		i += ret;
	} while (i < len);

	if (ret == -ENOTCONN) {
		/* A socket connection is disconnected, so we release
		 * the resources and don't add it again in the list */
		socket_remove(g_desc->current_sock);
		g_desc->current_sock = (void *)-1;
	}

	return i;
}

static ssize_t iio_phy_read(char *buf, size_t len)
{
	if (g_desc->phy_type == USE_UART)
		return (ssize_t)uart_read(g_desc->uart_desc,
					  (uint8_t *)buf, (size_t)len);
	else
		return network_read((void *)buf, (uint32_t)len);

	return -EINVAL;
}

/** Write to a peripheral device (UART, USB, NETWORK) */
static ssize_t iio_phy_write(const char *buf, size_t len)
{
	if (g_desc->phy_type == USE_UART)
		return (ssize_t)uart_write(g_desc->uart_desc,
					   (uint8_t *)buf, (size_t)len);
	else
		return socket_send(g_desc->current_sock, buf, len);

	return -EINVAL;
}

/**
 * @brief Get channel number.
 * @param ch - String containing channel name + channel number.
 * @return - Channel number. Ex: for "altvoltage0" return 0, for "voltage2"
 * return 2.
 */
static int32_t iio_get_channel_number(const char *ch)
{
	char *p = (char*)ch;
	int32_t ch_num = FAILURE;

	while (*p) {
		if (isdigit(*p))
			ch_num = strtol(p, &p, 10);
		else
			p++;
	}

	return ch_num;
}

/**
 * @brief Get channel ID from a list of channels.
 * @param channel - Channel name.
 * @param channels - List of channels.
 * @param ch_out - If "true" is output channel, if "false" is input channel.
 * @return Channel ID, or negative value if attribute is not found.
 */
static int16_t iio_get_channel_id(const char *channel,
				  struct iio_channel **channels, bool ch_out)
{
	int16_t i = 0;

	if (!(*channels))
		return -EINVAL;

	while (channels[i]) {
		if (!strcmp(channel, channels[i]->name) && (channels[i]->ch_out == ch_out))
			return i;
		i++;
	}

	return -ENOENT;
}

/**
 * @brief Get attribute ID from a list of attributes.
 * @param attr - Attribute name.
 * @param attributes - List of attributes.
 * @return - Attribute ID, or negative value if attribute is not found.
 */
static int16_t iio_get_attribute_id(const char *attr,
				    struct iio_attribute **attributes)
{
	int16_t i = 0;

	if (!(*attributes))
		return -EINVAL;

	while (attributes[i]) {
		if (!strcmp(attr, attributes[i]->name))
			return i;
		i++;
	}

	return -ENOENT;
}

/**
 * @brief Find interface with "device_name".
 * @param device_name - Device name.
 * @param iio_interfaces - List of interfaces.
 * @return Interface pointer if interface is found, NULL otherwise.
 */
static struct iio_interface *iio_get_interface(const char *device_name,
		struct iio_interfaces *iio_interfaces)
{
	int16_t i;

	if (!iio_interfaces)
		return NULL;

	for (i = 0; i < iio_interfaces->num_interfaces; i++) {
		if (!strcmp(device_name, iio_interfaces->interfaces[i]->name))
			return iio_interfaces->interfaces[i];
	}

	return NULL;
}

/**
 * @brief Read all attributes from an attribute list.
 * @param device - Physical instance of a device.
 * @param buf - Buffer where values are read.
 * @param len - Maximum length of value to be stored in buf.
 * @param channel - Channel properties.
 * @param attributes - List of attributes to be read.
 * @return Number of bytes read or negative value in case of error.
 */
static ssize_t iio_read_all_attr(void *device, char *buf, size_t len,
				 const struct iio_ch_info *channel, struct iio_attribute **attributes)
{
	int16_t i = 0, j = 0;
	char local_buf[256];
	ssize_t attr_length;
	uint32_t *pattr_length;

	if (!attributes)
		return FAILURE;

	if (!buf)
		return FAILURE;

	while (attributes[i]) {
		attr_length = attributes[i]->show(device, local_buf, len, channel);
		pattr_length = (uint32_t *)(buf + j);
		*pattr_length = bswap_constant_32(attr_length);
		j += 4;
		if (attr_length >= 0) {
			sprintf(buf + j, "%s", local_buf);
			if (attr_length & 0x3) /* multiple of 4 */
				attr_length = ((attr_length >> 2) + 1) << 2;
			j += attr_length;
		}
		i++;
	}

	return j;
}

/**
 * @brief Write all attributes from an attribute list.
 * @param device - Physical instance of a device.
 * @param buf - Values to be written.
 * @param len - Length of buf.
 * @param channel - Channel properties.
 * @param attributes - List of attributes to be written.
 * @return Number of written bytes or negative value in case of error.
 */
static ssize_t iio_write_all_attr(void *device, char *buf, size_t len,
				  const struct iio_ch_info *channel, struct iio_attribute **attributes)
{
	int16_t i = 0, j = 0;
	int16_t attr_length;

	if (!attributes)
		return FAILURE;

	if (!buf)
		return FAILURE;

	while (attributes[i]) {
		attr_length = bswap_constant_32((uint32_t)(buf + j));
		j += 4;
		attributes[i]->store(device, (buf + j), attr_length, channel);
		j += attr_length;
		if (j & 0x3)
			j = ((j >> 2) + 1) << 2;
		i++;
	}

	return len;
}

/**
 * @brief Read/write channel attribute.
 * @param el_info - Structure describing element to be written.
 * @param buf - Read/write value.
 * @param len - Length of data in "buf" parameter.
 * @param channel - Structure describing channel attributes.
 * @param is_write -If it has value "1", writes attribute, otherwise reads
 * 		attribute.
 * @return Length of chars written/read or negative value in case of error.
 */
static ssize_t iio_rd_wr_channel_attribute(struct element_info *el_info,
		char *buf, size_t len,
		struct iio_channel *channel, bool is_write)
{
	int16_t attribute_id;
	struct iio_interface *iface = iio_get_interface(el_info->device_name,
				      iio_interfaces);
	const struct iio_ch_info channel_info = {
		iio_get_channel_number(el_info->channel_name),
		el_info->ch_out
	};

	if (!strcmp(el_info->attribute_name, "")) {
		/* read / write all channel attributes */
		if (is_write)
			return iio_write_all_attr(iface->dev_instance, buf, len, &channel_info,
						  channel->attributes);
		else
			return iio_read_all_attr(iface->dev_instance, buf, len, &channel_info,
						 channel->attributes);
	} else {
		/* read / write single channel attribute, if attribute found */
		attribute_id = iio_get_attribute_id(el_info->attribute_name,
						    channel->attributes);
		if (attribute_id >= 0) {
			if (is_write)
				return channel->attributes[attribute_id]->store(iface->dev_instance,
						(char*)buf, len, &channel_info);
			else
				return channel->attributes[attribute_id]->show(iface->dev_instance, (char*)buf,
						len, &channel_info);
		}
	}

	return -ENOENT;
}

/**
 * @brief Read/write attribute.
 * @param el_info - Structure describing element to be written.
 * @param buf - Read/write value.
 * @param len - Length of data in "buf" parameter.
 * @param iio_device - Physical instance of a device.
 * @param is_write -If it has value "1", writes attribute, otherwise reads
 * 		attribute.
 * @return Length of chars written/read or negative value in case of error.
 */
static ssize_t iio_rd_wr_attribute(struct element_info *el_info, char *buf,
				   size_t len,
				   struct iio_device *iio_device, bool is_write)
{
	int16_t channel_id;
	int16_t attribute_id;
	struct iio_interface *iface;

	if (!iio_device)
		return -ENOENT;

	if (!strcmp(el_info->channel_name, "")) {
		/* it is attribute of a device */
		iface = iio_get_interface(el_info->device_name, iio_interfaces);

		if (!strcmp(el_info->attribute_name, "")) {
			/* read / write all device attributes */
			if (is_write)
				return iio_write_all_attr(iface->dev_instance, buf, len, NULL,
							  iio_device->attributes);
			else
				return iio_read_all_attr(iface->dev_instance, buf, len, NULL,
							 iio_device->attributes);
		} else {
			/* read / write single device attribute, if attribute found */
			attribute_id = iio_get_attribute_id(el_info->attribute_name,
							    iio_device->attributes);
			if (attribute_id < 0)
				return -ENOENT;
			if (is_write)
				return iio_device->attributes[attribute_id]->store(iface->dev_instance,
						(char*)buf, len, NULL);
			else
				return iio_device->attributes[attribute_id]->show(iface->dev_instance,
						(char*)buf, len, NULL);
		}
	} else {
		/* it is attribute of a channel */
		channel_id = iio_get_channel_id(el_info->channel_name, iio_device->channels,
						el_info->ch_out);
		return iio_rd_wr_channel_attribute(el_info, buf, len,
						   iio_device->channels[channel_id], is_write);
	}

	return -ENOENT;
}

/**
 * @brief Check if device is supported.
 * @param device - Device name.
 * @return TRUE if device is supported, FALSE otherwise.
 */
static bool iio_supported_dev(const char *device)
{
	return (NULL != iio_get_interface(device, iio_interfaces));
}

/**
 * @brief Read global attribute of a device.
 * @param device - String containing device name.
 * @param attr - String containing attribute name.
 * @param buf - Buffer where value is read.
 * @param len - Maximum length of value to be stored in buf.
 * @param debug - Read raw value if set.
 * @return Number of bytes read.
 */
static ssize_t iio_read_attr(const char *device, const char *attr, char *buf,
			     size_t len, bool debug)
{
	struct iio_interface *iio_device;
	struct element_info el_info;

	if (!iio_supported_dev(device))
		return FAILURE;

	el_info.device_name = device;
	el_info.channel_name = "";	/* there is no channel here */
	el_info.attribute_name = attr;

	iio_device = iio_get_interface(device, iio_interfaces);
	if (!iio_device)
		return FAILURE;

	return iio_rd_wr_attribute(&el_info, buf, len, iio_device->iio, 0);
}

/**
 * @brief Write global attribute of a device.
 * @param device - String containing device name.
 * @param attr - String containing attribute name.
 * @param buf - Value to be written.
 * @param len - Length of data.
 * @param debug - Write raw value if set.
 * @return Number of written bytes.
 */
static ssize_t iio_write_attr(const char *device, const char *attr,
			      const char *buf,
			      size_t len, bool debug)
{
	struct element_info el_info;
	struct iio_interface *iio_interface;

	if (!iio_supported_dev(device))
		return -ENODEV;

	el_info.device_name = device;
	el_info.channel_name = "";	/* there is no channel here */
	el_info.attribute_name = attr;

	iio_interface = iio_get_interface(device, iio_interfaces);
	if (!iio_interface)
		return FAILURE;

	return iio_rd_wr_attribute(&el_info, (char*)buf, len, iio_interface->iio, 1);
}

/**
 * @brief Read channel attribute.
 * @param device - String containing device name.
 * @param channel - String containing channel name.
 * @param ch_out -Channel type input/output.
 * @param attr - String containing attribute name.
 * @param buf - Buffer where value is stored.
 * @param len - Maximum length of value to be stored in buf.
 * @return - Number of bytes read.
 */
static ssize_t iio_ch_read_attr(const char *device, const char *channel,
				bool ch_out, const char *attr, char *buf, size_t len)
{
	struct element_info el_info;
	struct iio_interface *iio_interface;

	if (!iio_supported_dev(device))
		return FAILURE;

	el_info.device_name = device;
	el_info.channel_name = channel;
	el_info.attribute_name = attr;
	el_info.ch_out = ch_out;

	iio_interface = iio_get_interface(device, iio_interfaces);
	if (!device)
		return FAILURE;

	return iio_rd_wr_attribute(&el_info, buf, len, iio_interface->iio, 0);
}

/**
 * @brief Write channel attribute.
 * @param device - String containing device name.
 * @param channel - String containing channel name.
 * @param ch_out - Channel type input/output.
 * @param attr - String containing attribute name.
 * @param buf - Value to be written.
 * @param len - Length of data in "buf" parameter.
 * @return Number of written bytes.
 */
static ssize_t iio_ch_write_attr(const char *device, const char *channel,
				 bool ch_out, const char *attr, const char *buf, size_t len)
{
	struct element_info el_info;
	struct iio_interface *iio_interface;

	if (!iio_supported_dev(device))
		return -ENODEV;

	el_info.device_name = device;
	el_info.channel_name = channel;
	el_info.attribute_name = attr;
	el_info.ch_out = ch_out;
	iio_interface = iio_get_interface(device, iio_interfaces);

	if (!iio_interface)
		return -ENOENT;

	return iio_rd_wr_attribute(&el_info, (char*)buf, len, iio_interface->iio, 1);
}

/**
 * @brief  Open device.
 * @param device - String containing device name.
 * @param sample_size - Sample size.
 * @param mask - Channels to be opened.
 * @return SUCCESS, negative value in case of failure.
 */
static int32_t iio_open_dev(const char *device, size_t sample_size,
			    uint32_t mask)
{
	struct iio_interface *iface;
	uint32_t ch_mask;

	if (!iio_supported_dev(device))
		return -ENODEV;

	iface = iio_get_interface(device, iio_interfaces);
	ch_mask = 0xFFFFFFFF >> (32 - iface->iio->num_ch);

	if (mask & ~ch_mask)
		return -ENOENT;

	iface->ch_mask = mask;

	return SUCCESS;
}

/**
 * @brief Close device.
 * @param device - String containing device name.
 * @return SUCCESS, negative value in case of failure.
 */
static int32_t iio_close_dev(const char *device)
{
	struct iio_interface *iface;

	if (!iio_supported_dev(device))
		return FAILURE;
	iface = iio_get_interface(device, iio_interfaces);
	iface->ch_mask = 0;

	return SUCCESS;
}

/**
 * @brief Get device mask, this specifies the channels that are used.
 * @param device - String containing device name.
 * @param mask - Channels that are opened.
 * @return SUCCESS, negative value in case of failure.
 */
static int32_t iio_get_mask(const char *device, uint32_t *mask)
{
	struct iio_interface *iface;

	if (!iio_supported_dev(device))
		return -ENODEV;

	iface = iio_get_interface(device, iio_interfaces);
	*mask = iface->ch_mask;

	return SUCCESS;
}

/**
 * @brief Transfer data from device into RAM.
 * @param device - String containing device name.
 * @param bytes_count - Number of bytes.
 * @return Bytes_count or negative value in case of error.
 */
static ssize_t iio_transfer_dev_to_mem(const char *device, size_t bytes_count)
{
	struct iio_interface *iio_interface = iio_get_interface(device, iio_interfaces);

	if (iio_interface->transfer_dev_to_mem)
		return iio_interface->transfer_dev_to_mem(iio_interface->dev_instance,
				bytes_count, iio_interface->ch_mask);

	return -ENOENT;
}

/**
 * @brief Read chunk of data from RAM to pbuf. Call
 * "iio_transfer_dev_to_mem()" first.
 * This function is probably called multiple times by libtinyiiod after a
 * "iio_transfer_dev_to_mem" call, since we can only read "bytes_count" bytes.
 * @param device - String containing device name.
 * @param pbuf - Buffer where value is stored.
 * @param offset - Offset to the remaining data after reading n chunks.
 * @param bytes_count - Number of bytes to read.
 * @return: Bytes_count or negative value in case of error.
 */
static ssize_t iio_read_dev(const char *device, char *pbuf, size_t offset,
			    size_t bytes_count)
{
	struct iio_interface *iio_interface = iio_get_interface(device, iio_interfaces);

	if (iio_interface->read_data)
		return iio_interface->read_data(iio_interface->dev_instance, pbuf, offset,
						bytes_count, iio_interface->ch_mask);

	return -ENOENT;
}

/**
 * @brief Transfer memory to device.
 * @param device - String containing device name.
 * @param bytes_count - Number of bytes to transfer.
 * @return Bytes_count or negative value in case of error.
 */
static ssize_t iio_transfer_mem_to_dev(const char *device, size_t bytes_count)
{
	struct iio_interface *iio_interface = iio_get_interface(device, iio_interfaces);

	if (iio_interface->transfer_mem_to_dev)
		return iio_interface->transfer_mem_to_dev(iio_interface->dev_instance,
				bytes_count, iio_interface->ch_mask);

	return -ENOENT;
}

/**
 * @brief Write chunk of data into RAM.
 * This function is probably called multiple times by libtinyiiod before a
 * "iio_transfer_mem_to_dev" call, since we can only write "bytes_count" bytes
 * at a time.
 * @param device - String containing device name.
 * @param buf - Values to write.
 * @param offset - Offset in memory after the nth chunk of data.
 * @param bytes_count - Number of bytes to write.
 * @return Bytes_count or negative value in case of error.
 */
static ssize_t iio_write_dev(const char *device, const char *buf,
			     size_t offset, size_t bytes_count)
{
	struct iio_interface *iio_interface = iio_get_interface(device, iio_interfaces);
	if(iio_interface->write_data)
		return iio_interface->write_data(iio_interface->dev_instance, (char*)buf,
						 offset, bytes_count, iio_interface->ch_mask);

	return -ENOENT;
}

/**
 * @brief Get a merged xml containing all devices.
 * @param outxml - Generated xml.
 * @return SUCCESS in case of success or negative value otherwise.
 */
static ssize_t iio_get_xml(char **outxml)
{
	char *xml, *tmp_xml, *tmp_xml2;
	uint32_t length;
	uint16_t i;
	ssize_t ret;

	char header[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
			"<!DOCTYPE context ["
			"<!ELEMENT context (device | context-attribute)*>"
			"<!ELEMENT context-attribute EMPTY>"
			"<!ELEMENT device (channel | attribute | debug-attribute | buffer-attribute)*>"
			"<!ELEMENT channel (scan-element?, attribute*)>"
			"<!ELEMENT attribute EMPTY>"
			"<!ELEMENT scan-element EMPTY>"
			"<!ELEMENT debug-attribute EMPTY>"
			"<!ELEMENT buffer-attribute EMPTY>"
			"<!ATTLIST context name CDATA #REQUIRED description CDATA #IMPLIED>"
			"<!ATTLIST context-attribute name CDATA #REQUIRED value CDATA #REQUIRED>"
			"<!ATTLIST device id CDATA #REQUIRED name CDATA #IMPLIED>"
			"<!ATTLIST channel id CDATA #REQUIRED type (input|output) #REQUIRED name CDATA #IMPLIED>"
			"<!ATTLIST scan-element index CDATA #REQUIRED format CDATA #REQUIRED scale CDATA #IMPLIED>"
			"<!ATTLIST attribute name CDATA #REQUIRED filename CDATA #IMPLIED>"
			"<!ATTLIST debug-attribute name CDATA #REQUIRED>"
			"<!ATTLIST buffer-attribute name CDATA #REQUIRED>"
			"]>"
			"<context name=\"xml\" description=\"no-OS analog 1.1.0-g0000000 #1 Tue Nov 26 09:52:32 IST 2019 armv7l\" >"
			"<context-attribute name=\"no-OS\" value=\"1.1.0-g0000000\" />";
	char header_end[] = "</context>";

	if (!outxml)
		return FAILURE;

	xml = (char *)calloc(1, strlen(header) + 1);
	if (!xml)
		return FAILURE;

	strcpy(xml, header);
	for (i = 0; i < iio_interfaces->num_interfaces; i++) {
		ret = iio_interfaces->interfaces[i]->get_xml(&tmp_xml,
				iio_interfaces->interfaces[i]->iio);
		if (ret < 0)
			goto error;

		length = strlen(xml);
		tmp_xml2 = (char *)realloc(xml, strlen(xml) + strlen(tmp_xml) + 1);
		if (!tmp_xml2)
			goto error;

		xml = tmp_xml2;
		strcpy((xml + length), tmp_xml);
	}

	length = strlen(xml);
	tmp_xml = (char *)realloc(xml, strlen(xml) + strlen(header_end) + 1);
	if (!tmp_xml)
		goto error;

	xml = tmp_xml;
	strcpy((xml + length), header_end);

	*outxml = xml;

	return SUCCESS;
error:
	free(xml);

	return FAILURE;
}

/**
 * @brief Execute an iio step
 * @param desc - IIo descriptor
 * @return SUCCESS in case of success or negative value otherwise.
 */
ssize_t iio_step(struct iio_desc *desc)
{
	int32_t ret;

	if (desc->current_sock != NULL && (int32_t)desc->current_sock != -1) {
		ret = _push_sock(desc, desc->current_sock);
		if (IS_ERR_VALUE(ret))
			return ret;
	}

	desc->current_sock = NULL;

	return tinyiiod_read_command(desc->iiod);
}


/**
 * @brief Register interface.
 * @param iio_interface - Structure containing physical device instance and
 * device descriptor.
 * @return SUCCESS in case of success or negative value otherwise.
 */
ssize_t iio_register(struct iio_desc *desc, struct iio_interface *iio_interface)
{

	struct iio_interface **temp_interfaces;

	if (!(iio_interfaces)) {
		iio_interfaces = (struct iio_interfaces *)calloc(1,
				 sizeof(struct iio_interfaces));
		if (!iio_interfaces)
			return -ENOMEM;

		iio_interfaces->num_interfaces = 1;
		iio_interfaces->interfaces = (struct iio_interface **)calloc(1,
					     sizeof(struct iio_interface*));
		if (!iio_interfaces->interfaces)
			return -ENOMEM;
	} else {
		iio_interfaces->num_interfaces++;
		temp_interfaces = (struct iio_interface **)realloc(iio_interfaces->interfaces,
				  iio_interfaces->num_interfaces * sizeof(struct iio_interface*));
		if (!temp_interfaces) {
			free(iio_interfaces->interfaces);
			return -ENOMEM;
		}
		iio_interfaces->interfaces = temp_interfaces;
	}

	iio_interfaces->interfaces[iio_interfaces->num_interfaces - 1] = iio_interface;

	return SUCCESS;
}

/**
 * @brief Unregister interface.
 * @param iio_interface - Structure containing physical device instance and
 * device descriptor.
 * @return SUCCESS in case of success or negative value otherwise.
 */
ssize_t iio_unregister(struct iio_desc *desc,
		       struct iio_interface *iio_interface)
{
	struct iio_interfaces *interfaces;
	int16_t i, deleted = 0;

	interfaces = (struct iio_interfaces *)calloc(1, sizeof(struct iio_interfaces));
	if (!interfaces)
		return FAILURE;

	interfaces->interfaces = (struct iio_interface **)calloc(
					 iio_interfaces->num_interfaces - 1,
					 sizeof(struct iio_interface*));
	if (!interfaces->interfaces) {
		free(interfaces);
		return FAILURE;
	}

	for(i = 0; i < iio_interfaces->num_interfaces; i++) {
		if (!strcmp(iio_interface->name, iio_interfaces->interfaces[i]->name)) {
			iio_interface = iio_interfaces->interfaces[i];
			deleted = 1;
			continue;
		}
		interfaces->interfaces[i - deleted] = iio_interfaces->interfaces[i];
	}

	interfaces->num_interfaces = iio_interfaces->num_interfaces - 1;
	free(iio_interfaces);

	return deleted ? SUCCESS : FAILURE;
}

/**
 * @brief Set communication ops and read/write ops that will be called
 * from "libtinyiiod".
 * @param iiod - Structure containing new tinyiiod instance.
 * @param iio_server_ops - Structure containing read/write ops (Ex: read/write to
 * 			UART).
 * @return SUCCESS in case of success or negative value otherwise.
 */
ssize_t iio_init(struct iio_desc **desc, struct iio_init_param *init_param)
{
	int32_t			ret;
	struct iio_desc		*ldesc;
	struct tinyiiod_ops	*ops;

	if (!init_param)
		return -EINVAL;

	ops = (struct tinyiiod_ops *)calloc(1,
					    sizeof(struct tinyiiod_ops));
	if (!ops)
		return FAILURE;

	ldesc = (struct iio_desc *)calloc(1, sizeof(*ldesc));
	if (!ldesc)
		goto free_ops;
	ldesc->iiod_ops = ops;

	/* device operations */
	ops->read_attr = iio_read_attr;
	ops->write_attr = iio_write_attr;
	ops->ch_read_attr = iio_ch_read_attr;
	ops->ch_write_attr = iio_ch_write_attr;
	ops->transfer_dev_to_mem = iio_transfer_dev_to_mem;
	ops->read_data = iio_read_dev;
	ops->transfer_mem_to_dev = iio_transfer_mem_to_dev;
	ops->write_data = iio_write_dev;

	ops->open = iio_open_dev;
	ops->close = iio_close_dev;
	ops->get_mask = iio_get_mask;

	ops->read = iio_phy_read;
	ops->write = iio_phy_write;

	ldesc->phy_type = init_param->phy_type;
	if (init_param->phy_type == USE_UART) {
		ret = uart_init((struct uart_desc **)&ldesc->uart_desc,
				init_param->uart_init_param);
		if (IS_ERR_VALUE(ret))
			goto free_desc;
	} else if (init_param->phy_type == USE_NETWORK) {
		ret = socket_init(&ldesc->server,
				  init_param->tcp_socket_init_param);
		if (IS_ERR_VALUE(ret))
			goto free_desc;
		ret = socket_bind(ldesc->server, IIOD_PORT);
		if (IS_ERR_VALUE(ret))
			goto free_pylink;
		ret = socket_listen(ldesc->server, 0);
		if (IS_ERR_VALUE(ret))
			goto free_pylink;
		ret = cb_init(&ldesc->sockets, sizeof(struct tcp_socket_desc *)
			      * MAX_SOCKET_TO_HANDLE);
		if (IS_ERR_VALUE(ret))
			goto free_pylink;
	} else {
		goto free_desc;
	}

	ops->read = iio_phy_read;
	ops->write = iio_phy_write;

	ops->get_xml = iio_get_xml;

	ret = list_init(&ldesc->interfaces_list, LIST_PRIORITY_LIST,
			(f_cmp)iio_cmp_interfaces);
	if (IS_ERR_VALUE(ret))
		goto free_pylink;

	ldesc->iiod = tinyiiod_create(ops);
	if (!(ldesc->iiod))
		goto free_list;

	*desc = ldesc;
	g_desc = ldesc;

	return SUCCESS;

free_list:
	list_remove(ldesc->interfaces_list);
free_pylink:
	if (ldesc->phy_type == USE_UART)
		uart_remove(ldesc->uart_desc);
	else {
		socket_remove(ldesc->server);
		if (ldesc->sockets)
			cb_remove(ldesc->sockets);
	}
free_desc:
	free(ldesc);
free_ops:
	free(ops);

	return FAILURE;
}

/**
 * @brief Free the resources allocated by "iio_init()".
 * @param iiod: Structure containing tinyiiod instance.
 * @return SUCCESS in case of success or negative value otherwise.
 */
ssize_t iio_remove(struct iio_desc *desc)
{
	uint8_t i;

	for (i = 0; i < iio_interfaces->num_interfaces; i++)
		free(iio_interfaces->interfaces[i]);

	free(iio_interfaces);
	tinyiiod_destroy(desc->iiod);
	//This should be done here but first should be removed from tinyiiod_destroy
	//free(desc->iiod_ops);

	return SUCCESS;
}

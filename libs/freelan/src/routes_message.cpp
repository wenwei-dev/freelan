/*
 * libfreelan - A C++ library to establish peer-to-peer virtual private
 * networks.
 * Copyright (C) 2010-2011 Julien KAUFFMANN <julien.kauffmann@freelan.org>
 *
 * This file is part of libfreelan.
 *
 * libfreelan is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * libfreelan is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 *
 * If you intend to use libfreelan in a commercial software, please
 * contact me : we may arrange this for a small fee or no fee at all,
 * depending on the nature of your project.
 */

/**
 * \file routes_message.cpp
 * \author Julien KAUFFMANN <julien.kauffmann@freelan.org>
 * \brief The routes messages exchanged by the peers.
 */

#include "routes_message.hpp"

#include <cassert>

namespace freelan
{
	namespace
	{
		enum ip_network_address_type
		{
			INAT_IPV4 = 0x01,
			INAT_IPV4_GATEWAY = 0x02,
			INAT_IPV6 = 0x03,
			INAT_IPV6_GATEWAY = 0x04
		};

		template <typename AddressType>
		ip_network_address_type get_address_type(bool has_gateway);

		template <>
		ip_network_address_type get_address_type<boost::asio::ip::address_v4>(bool has_gateway)
		{
			return has_gateway ? INAT_IPV4_GATEWAY : INAT_IPV4;
		}

		template <>
		ip_network_address_type get_address_type<boost::asio::ip::address_v6>(bool has_gateway)
		{
			return has_gateway ? INAT_IPV6_GATEWAY : INAT_IPV6;
		}

		/**
		 * \brief A visitor that writes the representation of a network address to a buffer.
		 */
		template <typename BufferType>
		class ip_network_address_representation : public boost::static_visitor<size_t>
		{
			public:

				/**
				 * \brief Create a new ip_network_address_representation.
				 * \param buf The buffer to write the representation to.
				 * \param buf_len The length of buf.
				 */
				ip_network_address_representation(BufferType buf, size_t buf_len) :
					m_buf(buf),
					m_buf_len(buf_len)
				{}

				/**
				 * \brief Get the representation size of the network address.
				 * \param ir The ip_route.
				 * \return The representation size.
				 */
				template <typename AddressType>
				result_type operator()(const asiotap::base_ip_route<AddressType>& ir) const
				{
					const auto ina = ir.network_address();
					const auto _gateway = ir.gateway();
					const uint8_t prefix_length = static_cast<uint8_t>(ina.prefix_length());
					const auto bytes = ina.address().to_bytes();

					size_t result_size = 2 + bytes.size();

					if (m_buf_len < result_size)
					{
						throw std::runtime_error("buf_len");
					}

					fscp::buffer_tools::set<uint8_t>(m_buf, 0, static_cast<uint8_t>(get_address_type<AddressType>(static_cast<bool>(_gateway))));
					fscp::buffer_tools::set<uint8_t>(m_buf, 1, static_cast<uint8_t>(prefix_length));

					std::copy(bytes.begin(), bytes.end(), m_buf + 2);

					if (_gateway)
					{
						const auto gateway_bytes = _gateway->to_bytes();
						result_size += gateway_bytes.size();

						if (m_buf_len < result_size)
						{
							throw std::runtime_error("buf_len");
						}

						std::copy(gateway_bytes.begin(), gateway_bytes.end(), m_buf + 2 + bytes.size());
					}

					return result_size;
				}

				/**
				 * \brief Read the next ip_route contained in the buffer.
				 * \param ir The route to read.
				 * \param has_gateway Whether the function must read a gateway or not.
				 */
				template <typename AddressType>
				asiotap::base_ip_route<AddressType> read_next_ip_route(bool has_gateway)
				{
					if (m_buf_len == 0)
					{
						throw std::runtime_error("Not enough bytes for the expected prefix length");
					}

					const unsigned int prefix_length = static_cast<uint8_t>(*m_buf);

					++m_buf;
					--m_buf_len;

					typename AddressType::bytes_type bytes;

					if (m_buf_len < bytes.size())
					{
						throw std::runtime_error("Not enough bytes for the expected IP address");
					}

					std::copy(m_buf, m_buf + bytes.size(), bytes.begin());

					m_buf += bytes.size();
					m_buf_len -= bytes.size();

					if (has_gateway)
					{
						typename AddressType::bytes_type gateway_bytes;

						if (m_buf_len < gateway_bytes.size())
						{
							throw std::runtime_error("Not enough bytes for the expected IP address");
						}

						std::copy(m_buf, m_buf + gateway_bytes.size(), gateway_bytes.begin());

						m_buf += gateway_bytes.size();
						m_buf_len -= gateway_bytes.size();

						return asiotap::base_ip_route<AddressType>(asiotap::base_ip_network_address<AddressType>(AddressType(bytes), prefix_length), AddressType(gateway_bytes));
					}
					else
					{
						return asiotap::base_ip_route<AddressType>(asiotap::base_ip_network_address<AddressType>(AddressType(bytes), prefix_length));
					}
				}

				/**
				 * \brief Read the next ip_route contained in the buffer.
				 * \param ir The route to read.
				 * \return True if a route was read.
				 */
				bool read_next_ip_route(asiotap::ip_route& ir)
				{
					if (m_buf_len == 0)
					{
						return false;
					}

					const auto _type = *m_buf;
					++m_buf;
					--m_buf_len;

					switch (_type)
					{
						case INAT_IPV4:
						case INAT_IPV4_GATEWAY:
						{
							ir = read_next_ip_route<boost::asio::ip::address_v4>(_type == INAT_IPV4_GATEWAY);

							break;
						}
						case INAT_IPV6:
						case INAT_IPV6_GATEWAY:
						{
							ir = read_next_ip_route<boost::asio::ip::address_v6>(_type == INAT_IPV4_GATEWAY);

							break;
						}
						default:
							throw std::runtime_error("Unknown route type in message");
					}

					return true;
				}

			private:

				BufferType m_buf;
				size_t m_buf_len;
		};
	}

	size_t routes_message::write(void* buf, size_t buf_len, version_type _version, const asiotap::ip_route_set& routes)
	{
		if (buf_len < HEADER_LENGTH)
		{
			throw std::runtime_error("buf_len");
		}

		size_t required_size = 0;
		uint8_t* pbuf = static_cast<uint8_t*>(buf) + HEADER_LENGTH;
		size_t pbuf_len = buf_len - HEADER_LENGTH;

		fscp::buffer_tools::set<uint32_t>(pbuf, 0, htonl(static_cast<uint32_t>(_version)));

		required_size += sizeof(uint32_t);
		pbuf += sizeof(uint32_t);
		pbuf_len -= sizeof(uint32_t);

		for (auto&& route : routes)
		{
			const size_t count = boost::apply_visitor(ip_network_address_representation<uint8_t*>(pbuf, pbuf_len), route);

			required_size += count;
			pbuf += count;
			pbuf_len -= count;
		}

		return message::write(buf, buf_len, MT_ROUTES, required_size);
	}

	routes_message::version_type routes_message::version() const
	{
		return ntohl(static_cast<version_type>(fscp::buffer_tools::get<uint32_t>(payload(), 0)));
	}

	const asiotap::ip_route_set& routes_message::routes() const
	{
		if (!m_routes_cache)
		{
			asiotap::ip_route_set result;

			const uint8_t* pbuf = payload() + sizeof(uint32_t);
			size_t pbuf_len = length() - sizeof(uint32_t);

			ip_network_address_representation<const uint8_t*> deserializer(pbuf, pbuf_len);

			asiotap::ip_route ir;

			while (deserializer.read_next_ip_route(ir))
			{
				result.insert(ir);
			}

			m_routes_cache = result;
		}

		return *m_routes_cache;
	}

	routes_message::routes_message(const void* buf, size_t buf_len) :
		message(buf, buf_len)
	{
		routes();
	}

	routes_message::routes_message(const message& _message) :
		message(_message)
	{
		routes();
	}
}

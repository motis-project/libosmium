#ifndef OSMIUM_IO_DETAIL_INPUT_FORMAT_HPP
#define OSMIUM_IO_DETAIL_INPUT_FORMAT_HPP

/*

This file is part of Osmium (https://osmcode.org/libosmium).

Copyright 2013-2021 Jochen Topf <jochen@topf.org> and others (see README).

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include <osmium/io/detail/queue_util.hpp>
#include <osmium/io/error.hpp>
#include <osmium/io/file.hpp>
#include <osmium/io/file_format.hpp>
#include <osmium/io/header.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/osm/entity_bits.hpp>
#include <osmium/thread/pool.hpp>

#include <array>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <utility>

namespace osmium {

    namespace io {

        namespace detail {

            struct parser_arguments {
                osmium::thread::Pool& pool;
                future_string_queue_type& input_queue;
                future_buffer_queue_type& output_queue;
                std::promise<osmium::io::Header>& header_promise;
                osmium::osm_entity_bits::type read_which_entities;
                osmium::io::read_meta read_metadata;
                osmium::io::buffers_type buffers_kind;
            };

            class Parser {

                osmium::thread::Pool& m_pool;
                future_buffer_queue_type& m_output_queue;
                std::promise<osmium::io::Header>& m_header_promise;
                queue_wrapper<std::string> m_input_queue;
                osmium::osm_entity_bits::type m_read_which_entities;
                osmium::io::read_meta m_read_metadata;
                bool m_header_is_done;

            protected:

                osmium::thread::Pool& get_pool() {
                    return m_pool;
                }

                osmium::osm_entity_bits::type read_types() const noexcept {
                    return m_read_which_entities;
                }

                osmium::io::read_meta read_metadata() const noexcept {
                    return m_read_metadata;
                }

                bool header_is_done() const noexcept {
                    return m_header_is_done;
                }

                void set_header_value(const osmium::io::Header& header) {
                    if (!m_header_is_done) {
                        m_header_is_done = true;
                        m_header_promise.set_value(header);
                    }
                }

                void set_header_exception(const std::exception_ptr& exception) {
                    if (!m_header_is_done) {
                        m_header_is_done = true;
                        m_header_promise.set_exception(exception);
                    }
                }

                /**
                 * Wrap the buffer into a future and add it to the output queue.
                 */
                void send_to_output_queue(osmium::memory::Buffer&& buffer) {
                    add_to_queue(m_output_queue, std::move(buffer));
                }

                void send_to_output_queue(std::future<osmium::memory::Buffer>&& future) {
                    m_output_queue.push(std::move(future));
                }

            public:

                explicit Parser(parser_arguments& args) :
                    m_pool(args.pool),
                    m_output_queue(args.output_queue),
                    m_header_promise(args.header_promise),
                    m_input_queue(args.input_queue),
                    m_read_which_entities(args.read_which_entities),
                    m_read_metadata(args.read_metadata),
                    m_header_is_done(false) {
                }

                Parser(const Parser&) = delete;
                Parser& operator=(const Parser&) = delete;

                Parser(Parser&&) = delete;
                Parser& operator=(Parser&&) = delete;

                virtual ~Parser() noexcept = default;

                virtual void run() = 0;

                std::string get_input() {
                    return m_input_queue.pop();
                }

                bool input_done() const {
                    return m_input_queue.has_reached_end_of_data();
                }

                void parse() {
                    try {
                        run();
                    } catch (...) {
                        std::exception_ptr exception = std::current_exception();
                        set_header_exception(exception);
                        add_to_queue(m_output_queue, std::move(exception));
                    }

                    add_end_of_data_to_queue(m_output_queue);
                }

            }; // class Parser

            class ParserWithBuffer : public Parser {

                enum {
                    initial_buffer_size = 1024UL * 1024UL
                };

                osmium::memory::Buffer m_buffer{initial_buffer_size,
                                                osmium::memory::Buffer::auto_grow::internal};

                osmium::io::buffers_type m_buffers_kind;
                osmium::item_type m_last_type = osmium::item_type::undefined;

                bool is_different_type(osmium::item_type current_type) noexcept {
                    if (m_last_type == current_type) {
                        return false;
                    }

                    if (m_last_type == osmium::item_type::undefined) {
                        m_last_type = current_type;
                        return false;
                    }

                    m_last_type = current_type;
                    return true;
                }

            protected:

                explicit ParserWithBuffer(parser_arguments& args) :
                    Parser(args),
                    m_buffers_kind(args.buffers_kind) {
                }

                osmium::memory::Buffer& buffer() noexcept {
                    return m_buffer;
                }

                void flush_nested_buffer() {
                    if (m_buffer.has_nested_buffers()) {
                        std::unique_ptr<osmium::memory::Buffer> buffer_ptr{m_buffer.get_last_nested()};
                        send_to_output_queue(std::move(*buffer_ptr));
                    }
                }

                void flush_final_buffer() {
                    if (m_buffer.committed() > 0) {
                        send_to_output_queue(std::move(m_buffer));
                    }
                }

                void maybe_new_buffer(osmium::item_type current_type) {
                    if (m_buffers_kind == buffers_type::any) {
                        return;
                    }

                    if (is_different_type(current_type) && m_buffer.committed() > 0) {
                        osmium::memory::Buffer new_buffer{initial_buffer_size,
                                                          osmium::memory::Buffer::auto_grow::internal};
                        using std::swap;
                        swap(new_buffer, m_buffer);
                        send_to_output_queue(std::move(new_buffer));
                    }
                }

            }; // class ParserWithBuffer

            /**
             * This factory class is used to create objects that decode OSM
             * data written in a specified format.
             *
             * Do not use this class directly. Use the osmium::io::Reader
             * class instead.
             */
            class ParserFactory {

            public:

                using create_parser_type = std::function<std::unique_ptr<Parser>(parser_arguments&)>;

            private:

                std::array<create_parser_type, static_cast<std::size_t>(file_format::last) + 1> m_callbacks;

                ParserFactory() noexcept = default;

                create_parser_type& callbacks(const osmium::io::file_format format) noexcept {
                    return m_callbacks[static_cast<std::size_t>(format)];
                }

                const create_parser_type& callbacks(const osmium::io::file_format format) const noexcept {
                    return m_callbacks[static_cast<std::size_t>(format)];
                }

            public:

                static ParserFactory& instance() noexcept {
                    static ParserFactory factory;
                    return factory;
                }

                bool register_parser(const osmium::io::file_format format, create_parser_type&& create_function) {
                    callbacks(format) = std::forward<create_parser_type>(create_function);
                    return true;
                }

                create_parser_type get_creator_function(const osmium::io::File& file) const {
                    auto func = callbacks(file.format());
                    if (func) {
                        return func;
                    }
                    throw unsupported_file_format_error{
                            std::string{"Can not open file '"} +
                            file.filename() +
                            "' with type '" +
                            as_string(file.format()) +
                            "'. No support for reading this format in this program."};
                }

            }; // class ParserFactory

        } // namespace detail

    } // namespace io

} // namespace osmium

#endif // OSMIUM_IO_DETAIL_INPUT_FORMAT_HPP

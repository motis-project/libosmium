#ifndef OSMIUM_IO_XML_INPUT_HPP
#define OSMIUM_IO_XML_INPUT_HPP

/*

This file is part of Osmium (http://osmcode.org/osmium).

Copyright 2013 Jochen Topf <jochen@topf.org> and others (see README).

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

#define OSMIUM_LINK_WITH_LIBS_EXPAT -lexpat

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <expat.h>

#include <osmium/io/reader.hpp>
#include <osmium/osm/builder.hpp>
#include <osmium/thread/queue.hpp>

namespace osmium {

    namespace io {

        class ParserIsDone : std::exception {
        };

        class XMLParser {

            static constexpr int buffer_size = 10 * 1000 * 1000;

            enum class context {
                root,
                top,
                node,
                way,
                relation,
                changeset,
                ignored_node,
                ignored_way,
                ignored_relation,
                ignored_changeset,
                in_object
            };

            context m_context;
            context m_last_context;

            /**
             * This is used only for change files which contain create, modify,
             * and delete sections.
             */
            bool m_in_delete_section;

            osmium::io::Header m_header;

            osmium::memory::Buffer m_buffer;

            std::unique_ptr<osmium::osm::NodeBuilder>               m_node_builder;
            std::unique_ptr<osmium::osm::WayBuilder>                m_way_builder;
            std::unique_ptr<osmium::osm::RelationBuilder>           m_relation_builder;
            std::unique_ptr<osmium::osm::ChangesetBuilder>          m_changeset_builder;

            std::unique_ptr<osmium::osm::TagListBuilder>            m_tl_builder;
            std::unique_ptr<osmium::osm::WayNodeListBuilder>        m_wnl_builder;
            std::unique_ptr<osmium::osm::RelationMemberListBuilder> m_rml_builder;

            osmium::thread::Queue<std::string>& m_input_queue;
            osmium::thread::Queue<osmium::memory::Buffer>& m_queue;
            std::promise<osmium::io::Header>& m_header_promise;

            bool m_promise_fulfilled;

            osmium::osm_entity::flags m_read_types;

            size_t m_max_queue_size;

            std::atomic<bool>& m_done;

        public:

            XMLParser(osmium::thread::Queue<std::string>& input_queue, osmium::thread::Queue<osmium::memory::Buffer>& queue, std::promise<osmium::io::Header>& header_promise, osmium::osm_entity::flags read_types, std::atomic<bool>& done) :
                m_context(context::root),
                m_last_context(context::root),
                m_in_delete_section(false),
                m_header(),
                m_buffer(buffer_size),
                m_node_builder(),
                m_way_builder(),
                m_relation_builder(),
                m_changeset_builder(),
                m_tl_builder(),
                m_wnl_builder(),
                m_rml_builder(),
                m_input_queue(input_queue),
                m_queue(queue),
                m_header_promise(header_promise),
                m_promise_fulfilled(false),
                m_read_types(read_types),
                m_max_queue_size(100),
                m_done(done) {
            }

            void operator()() {
                XML_Parser parser = XML_ParserCreate(0);
                if (!parser) {
                    throw std::runtime_error("Error creating parser");
                }

                XML_SetUserData(parser, this);

                XML_SetElementHandler(parser, start_element_wrapper, end_element_wrapper);

                try {
                    int done;
                    do {
                        std::string data;
                        m_input_queue.wait_and_pop(data);
                        done = data.empty();
                        if (XML_Parse(parser, data.data(), data.size(), done) == XML_STATUS_ERROR) {
                            XML_Error errorCode = XML_GetErrorCode(parser);
                            long errorLine = XML_GetCurrentLineNumber(parser);
                            long errorCol = XML_GetCurrentColumnNumber(parser);
                            const XML_LChar* errorString = XML_ErrorString(errorCode);

                            std::stringstream errorDesc;
                            errorDesc << "XML parsing error at line " << errorLine << ":" << errorCol;
                            errorDesc << ": " << errorString;
                            throw std::runtime_error(errorDesc.str());
                        }
                    } while (!done && !m_done);
                } catch (ParserIsDone&) {
                    // intentionally left blank
                }
                XML_ParserFree(parser);
            }

        private:

            static void XMLCALL start_element_wrapper(void* data, const XML_Char* element, const XML_Char** attrs) {
                static_cast<XMLParser*>(data)->start_element(element, attrs);
            }

            static void XMLCALL end_element_wrapper(void* data, const XML_Char* element) {
                static_cast<XMLParser*>(data)->end_element(element);
            }

            const char* init_object(osmium::Object& object, const XML_Char** attrs) {
                static const char* empty = "";
                const char* user = empty;

                if (m_in_delete_section) {
                    object.visible(false);
                }
                for (int count = 0; attrs[count]; count += 2) {
                    if (!strcmp(attrs[count], "lon")) {
                        static_cast<osmium::Node&>(object).lon(atof(attrs[count+1])); // XXX
                    } else if (!strcmp(attrs[count], "lat")) {
                        static_cast<osmium::Node&>(object).lat(atof(attrs[count+1])); // XXX
                    } else if (!strcmp(attrs[count], "user")) {
                        user = attrs[count+1];
                    } else {
                        object.set_attribute(attrs[count], attrs[count+1]);
                    }
                }

                return user;
            }

            void init_changeset(osmium::osm::ChangesetBuilder* builder, const XML_Char** attrs) {
                osmium::Changeset& new_changeset = builder->object();
                bool user_set = false;

                osmium::Location min {};
                osmium::Location max {};
                for (int count = 0; attrs[count]; count += 2) {
                    if (!strcmp(attrs[count], "min_lon")) {
                        min.lon(atof(attrs[count+1]));
                    } else if (!strcmp(attrs[count], "min_lat")) {
                        min.lat(atof(attrs[count+1]));
                    } else if (!strcmp(attrs[count], "max_lon")) {
                        max.lon(atof(attrs[count+1]));
                    } else if (!strcmp(attrs[count], "max_lat")) {
                        max.lat(atof(attrs[count+1]));
                    } else if (!strcmp(attrs[count], "user")) {
                        builder->add_user(attrs[count+1]);
                        user_set = true;
                    } else {
                        new_changeset.set_attribute(attrs[count], attrs[count+1]);
                    }
                }

                new_changeset.bounds().extend(min);
                new_changeset.bounds().extend(max);

                if (!user_set) {
                    builder->add_user("");
                }
            }

            void check_tag(osmium::memory::Builder* builder, const XML_Char* element, const XML_Char** attrs) {
                if (!strcmp(element, "tag")) {
                    m_wnl_builder.reset();
                    m_rml_builder.reset();

                    const char* key = "";
                    const char* value = "";
                    for (int count = 0; attrs[count]; count += 2) {
                        if (attrs[count][0] == 'k' && attrs[count][1] == 0) {
                            key = attrs[count+1];
                        }
                        if (attrs[count][0] == 'v' && attrs[count][1] == 0) {
                            value = attrs[count+1];
                        }
                    }
                    if (!m_tl_builder) {
                        m_tl_builder = std::unique_ptr<osmium::osm::TagListBuilder>(new osmium::osm::TagListBuilder(m_buffer, builder));
                    }
                    m_tl_builder->add_tag(key, value);
                }
            }

            void header_is_done() {
                m_header_promise.set_value(m_header);
                if (m_read_types == osmium::osm_entity::flags::nothing) {
                    throw ParserIsDone();
                }
                m_promise_fulfilled = true;
            }

            void start_element(const XML_Char* element, const XML_Char** attrs) {
                try {
                    switch (m_context) {
                        case context::root:
                            if (!strcmp(element, "osm") || !strcmp(element, "osmChange")) {
                                if (!strcmp(element, "osmChange")) {
                                    m_header.has_multiple_object_versions(true);
                                }
                                for (int count = 0; attrs[count]; count += 2) {
                                    if (!strcmp(attrs[count], "version")) {
                                        if (strcmp(attrs[count+1], "0.6")) {
                                            throw std::runtime_error("can only read version 0.6 files");
                                        }
                                    } else if (!strcmp(attrs[count], "generator")) {
                                        m_header.generator(attrs[count+1]);
                                    }
                                }
                            }
                            m_context = context::top;
                            break;
                        case context::top:
                            assert(!m_tl_builder);
                            if (!strcmp(element, "node")) {
                                if (!m_promise_fulfilled) {
                                    header_is_done();
                                }
                                if (m_read_types & osmium::osm_entity::flags::node) {
                                    m_node_builder = std::unique_ptr<osmium::osm::NodeBuilder>(new osmium::osm::NodeBuilder(m_buffer));
                                    m_node_builder->add_user(init_object(m_node_builder->object(), attrs));
                                    m_context = context::node;
                                } else {
                                    m_context = context::ignored_node;
                                }
                            } else if (!strcmp(element, "way")) {
                                if (!m_promise_fulfilled) {
                                    header_is_done();
                                }
                                if (m_read_types & osmium::osm_entity::flags::way) {
                                    m_way_builder = std::unique_ptr<osmium::osm::WayBuilder>(new osmium::osm::WayBuilder(m_buffer));
                                    m_way_builder->add_user(init_object(m_way_builder->object(), attrs));
                                    m_context = context::way;
                                } else {
                                    m_context = context::ignored_way;
                                }
                            } else if (!strcmp(element, "relation")) {
                                if (!m_promise_fulfilled) {
                                    header_is_done();
                                }
                                if (m_read_types & osmium::osm_entity::flags::relation) {
                                    m_relation_builder = std::unique_ptr<osmium::osm::RelationBuilder>(new osmium::osm::RelationBuilder(m_buffer));
                                    m_relation_builder->add_user(init_object(m_relation_builder->object(), attrs));
                                    m_context = context::relation;
                                } else {
                                    m_context = context::ignored_relation;
                                }
                            } else if (!strcmp(element, "changeset")) {
                                if (!m_promise_fulfilled) {
                                    header_is_done();
                                }
                                if (m_read_types & osmium::osm_entity::flags::changeset) {
                                    m_changeset_builder = std::unique_ptr<osmium::osm::ChangesetBuilder>(new osmium::osm::ChangesetBuilder(m_buffer));
                                    init_changeset(m_changeset_builder.get(), attrs);
                                    m_context = context::changeset;
                                } else {
                                    m_context = context::ignored_changeset;
                                }
                            } else if (!strcmp(element, "bounds")) {
                                osmium::Location min;
                                osmium::Location max;
                                for (int count = 0; attrs[count]; count += 2) {
                                    if (!strcmp(attrs[count], "minlon")) {
                                        min.lon(atof(attrs[count+1]));
                                    } else if (!strcmp(attrs[count], "minlat")) {
                                        min.lat(atof(attrs[count+1]));
                                    } else if (!strcmp(attrs[count], "maxlon")) {
                                        max.lon(atof(attrs[count+1]));
                                    } else if (!strcmp(attrs[count], "maxlat")) {
                                        max.lat(atof(attrs[count+1]));
                                    }
                                }
                                m_header.bounds().extend(min).extend(max);
                            } else if (!strcmp(element, "delete")) {
                                m_in_delete_section = true;
                            }
                            break;
                        case context::node:
                            m_last_context = context::node;
                            m_context = context::in_object;
                            check_tag(m_node_builder.get(), element, attrs);
                            break;
                        case context::way:
                            m_last_context = context::way;
                            m_context = context::in_object;
                            if (!strcmp(element, "nd")) {
                                m_tl_builder.reset();

                                if (!m_wnl_builder) {
                                    m_wnl_builder = std::unique_ptr<osmium::osm::WayNodeListBuilder>(new osmium::osm::WayNodeListBuilder(m_buffer, m_way_builder.get()));
                                }

                                for (int count = 0; attrs[count]; count += 2) {
                                    if (!strcmp(attrs[count], "ref")) {
                                        m_wnl_builder->add_way_node(osmium::string_to_object_id(attrs[count+1]));
                                    }
                                }
                            } else {
                                check_tag(m_way_builder.get(), element, attrs);
                            }
                            break;
                        case context::relation:
                            m_last_context = context::relation;
                            m_context = context::in_object;
                            if (!strcmp(element, "member")) {
                                m_tl_builder.reset();

                                if (!m_rml_builder) {
                                    m_rml_builder = std::unique_ptr<osmium::osm::RelationMemberListBuilder>(new osmium::osm::RelationMemberListBuilder(m_buffer, m_relation_builder.get()));
                                }

                                char type = 'x';
                                object_id_type ref  = 0;
                                const char* role = "";
                                for (int count = 0; attrs[count]; count += 2) {
                                    if (!strcmp(attrs[count], "type")) {
                                        type = static_cast<char>(attrs[count+1][0]);
                                    } else if (!strcmp(attrs[count], "ref")) {
                                        ref = osmium::string_to_object_id(attrs[count+1]);
                                    } else if (!strcmp(attrs[count], "role")) {
                                        role = static_cast<const char*>(attrs[count+1]);
                                    }
                                }
                                // XXX assert type, ref, role are set
                                m_rml_builder->add_member(char_to_item_type(type), ref, role);
                            } else {
                                check_tag(m_relation_builder.get(), element, attrs);
                            }
                            break;
                        case context::changeset:
                            m_last_context = context::changeset;
                            m_context = context::in_object;
                            check_tag(m_changeset_builder.get(), element, attrs);
                            break;
                        case context::ignored_node:
                        case context::ignored_way:
                        case context::ignored_relation:
                        case context::ignored_changeset:
                            break;
                        case context::in_object:
                            // fallthrough
                        default:
                            assert(false); // should never be here
                    }
                } catch (osmium::memory::BufferIsFull&) {
                    std::cerr << "BUFFER FULL (start_element)" << std::endl;
                    exit(1);
                }
            }

            void end_element(const XML_Char* element) {
                try {
                    switch (m_context) {
                        case context::root:
                            assert(false); // should never be here
                            break;
                        case context::top:
                            if (!strcmp(element, "osm") || !strcmp(element, "osmChange")) {
                                m_context = context::root;
                                m_queue.push(std::move(m_buffer));
                                m_queue.push(osmium::memory::Buffer()); // empty buffer to signify eof
                            } else if (!strcmp(element, "delete")) {
                                m_in_delete_section = false;
                            }
                            break;
                        case context::node:
                            assert(!strcmp(element, "node"));
                            m_tl_builder.reset();
                            m_node_builder.reset();
                            m_buffer.commit();
                            m_context = context::top;
                            flush_buffer();
                            break;
                        case context::way:
                            assert(!strcmp(element, "way"));
                            m_tl_builder.reset();
                            m_wnl_builder.reset();
                            m_way_builder.reset();
                            m_buffer.commit();
                            m_context = context::top;
                            flush_buffer();
                            break;
                        case context::relation:
                            assert(!strcmp(element, "relation"));
                            m_tl_builder.reset();
                            m_rml_builder.reset();
                            m_relation_builder.reset();
                            m_buffer.commit();
                            m_context = context::top;
                            flush_buffer();
                            break;
                        case context::changeset:
                            assert(!strcmp(element, "changeset"));
                            m_tl_builder.reset();
                            m_changeset_builder.reset();
                            m_buffer.commit();
                            m_context = context::top;
                            flush_buffer();
                            break;
                        case context::in_object:
                            m_context = m_last_context;
                            break;
                        case context::ignored_node:
                            if (!strcmp(element, "node")) {
                                m_context = context::top;
                            }
                            break;
                        case context::ignored_way:
                            if (!strcmp(element, "way")) {
                                m_context = context::top;
                            }
                            break;
                        case context::ignored_relation:
                            if (!strcmp(element, "relation")) {
                                m_context = context::top;
                            }
                            break;
                        case context::ignored_changeset:
                            if (!strcmp(element, "changeset")) {
                                m_context = context::top;
                            }
                            break;
                        default:
                            assert(false); // should never be here
                    }
                } catch (osmium::memory::BufferIsFull&) {
                    std::cerr << "BUFFER FULL (end_element)" << std::endl;
                    exit(1);
                }
            }

            void flush_buffer() {
                if (m_buffer.capacity() - m_buffer.committed() < 1000 * 1000) {
                    m_queue.push(std::move(m_buffer));
                    osmium::memory::Buffer buffer(buffer_size);
                    std::swap(m_buffer, buffer);

                    while (m_queue.size() > m_max_queue_size) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }
            }

        }; // XMLParser

        class XMLInput : public osmium::io::Input {

            static constexpr size_t m_max_queue_size = 100;

            osmium::thread::Queue<osmium::memory::Buffer> m_queue;
            std::atomic<bool> m_done;
            std::thread m_reader;
            std::promise<osmium::io::Header> m_header_promise;

        public:

            /**
             * Instantiate XML Parser
             *
             * @param file osmium::io::File instance.
             */
            XMLInput(const osmium::io::File& file, osmium::osm_entity::flags read_which_entities, osmium::thread::Queue<std::string>& input_queue) :
                osmium::io::Input(file, read_which_entities, input_queue),
                m_queue(),
                m_done(false),
                m_reader() {
            }

            ~XMLInput() {
                m_done = true;
                if (m_reader.joinable()) {
                    m_reader.join();
                }
            }

            void open() override {
                XMLParser parser(m_input_queue, m_queue, m_header_promise, m_read_which_entities, m_done);

                m_reader = std::thread(std::move(parser));

                // wait for header
                m_header = m_header_promise.get_future().get();
            }

            osmium::memory::Buffer read() override {
                osmium::memory::Buffer buffer;

                if (!m_done || !m_queue.empty()) {
                    m_queue.wait_and_pop(buffer);
                }

                return buffer;
            }

        }; // class XMLInput

        namespace {

            const bool registered_xml_input = osmium::io::InputFactory::instance().register_input_format({
                osmium::io::Encoding::XML(),
                osmium::io::Encoding::XMLgz(),
                osmium::io::Encoding::XMLbz2()
            }, [](const osmium::io::File& file, osmium::osm_entity::flags read_which_entities, osmium::thread::Queue<std::string>& input_queue) {
                return new osmium::io::XMLInput(file, read_which_entities, input_queue);
            });

        } // anonymous namespace

    } // namespace io

} // namespace osmium

#endif // OSMIUM_IO_XML_INPUT_HPP

#ifndef OSMIUM_IO_DETAIL_ZLIB_HPP
#define OSMIUM_IO_DETAIL_ZLIB_HPP

/*

This file is part of Osmium (https://osmcode.org/libosmium).

Copyright 2013-2018 Jochen Topf <jochen@topf.org> and others (see README).

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

#include <osmium/io/error.hpp>

#include <protozero/version.hpp>

#if PROTOZERO_VERSION_CODE >= 10600
# include <protozero/data_view.hpp>
#else
# include <protozero/types.hpp>
#endif

#include <zlib.h>

#include <cassert>
#include <limits>
#include <string>

namespace osmium {

    namespace io {

        namespace detail {

            /**
             * Compress data using zlib.
             *
             * Note that this function can not compress data larger than
             * what fits in an unsigned long, on Windows this is usually 32bit.
             *
             * @param input Data to compress.
             * @returns Compressed data.
             */
            inline std::string zlib_compress(const std::string& input) {
                assert(input.size() < std::numeric_limits<unsigned long>::max());
                unsigned long output_size = ::compressBound(static_cast<unsigned long>(input.size())); // NOLINT(google-runtime-int)

                std::string output(output_size, '\0');

                const auto result = ::compress(
                    reinterpret_cast<unsigned char*>(const_cast<char *>(output.data())),
                    &output_size,
                    reinterpret_cast<const unsigned char*>(input.data()),
                    static_cast<unsigned long>(input.size()) // NOLINT(google-runtime-int)
                );

                if (result != Z_OK) {
                    throw io_error{std::string{"failed to compress data: "} + zError(result)};
                }

                output.resize(output_size);

                return output;
            }

            /**
             * Uncompress data using zlib.
             *
             * Note that this function can not uncompress data larger than
             * what fits in an unsigned long, on Windows this is usually 32bit.
             *
             * @param input Compressed input data.
             * @param raw_size Size of uncompressed data.
             * @param output Uncompressed result data.
             * @returns Pointer and size to incompressed data.
             */
            inline protozero::data_view zlib_uncompress_string(const char* input, unsigned long input_size, unsigned long raw_size, std::string& output) { // NOLINT(google-runtime-int)
                output.resize(raw_size);

                const auto result = ::uncompress(
                    reinterpret_cast<unsigned char*>(&*output.begin()),
                    &raw_size,
                    reinterpret_cast<const unsigned char*>(input),
                    input_size
                );

                if (result != Z_OK) {
                    throw io_error{std::string{"failed to uncompress data: "} + zError(result)};
                }

                return protozero::data_view{output.data(), output.size()};
            }

        } // namespace detail

    } // namespace io

} // namespace osmium

#endif // OSMIUM_IO_DETAIL_ZLIB_HPP

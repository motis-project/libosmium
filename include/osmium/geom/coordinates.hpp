#ifndef OSMIUM_GEOM_COORDINATES_HPP
#define OSMIUM_GEOM_COORDINATES_HPP

/*

This file is part of Osmium (http://osmcode.org/libosmium).

Copyright 2013,2014 Jochen Topf <jochen@topf.org> and others (see README).

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

#include <cstddef>
#include <iosfwd>
#include <string>

#include <osmium/osm/location.hpp>
#include <osmium/util/double.hpp>

namespace osmium {

    namespace geom {

        struct Coordinates {

            double x;
            double y;

            explicit Coordinates(double cx, double cy) : x(cx), y(cy) {
            }

            Coordinates(const osmium::Location& location) : x(location.lon()), y(location.lat()) {
            }

            void append_to_string(std::string& s, const char infix, int precision) const {
                osmium::util::double2string(s, x, precision);
                s += infix;
                osmium::util::double2string(s, y, precision);
            }

            void append_to_string(std::string& s, const char prefix, const char infix, const char suffix, int precision) const {
                s += prefix;
                append_to_string(s, infix, precision);
                s += suffix;
            }

        }; // struct coordinates

        /**
         * Compare whether two Coordinates are identical. Might not give the
         * right result if the coordinates have been the result of some
         * calculation that introduced rounding errors.
         */
        inline bool operator==(const Coordinates& lhs, const Coordinates& rhs) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
            return lhs.x == rhs.x && lhs.y == rhs.y;
#pragma GCC diagnostic pop
        }

        inline bool operator!=(const Coordinates& lhs, const Coordinates& rhs) {
            return ! operator==(lhs, rhs);
        }

        template <typename TChar, typename TTraits>
        inline std::basic_ostream<TChar, TTraits>& operator<<(std::basic_ostream<TChar, TTraits>& out, const Coordinates& c) {
            return out << '(' << c.x << ',' << c.y << ')';
        }

    } // namespace geom

} // namespace osmium

#endif // OSMIUM_GEOM_COORDINATES_HPP
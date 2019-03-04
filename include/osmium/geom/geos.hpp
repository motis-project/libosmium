#ifndef OSMIUM_GEOM_GEOS_HPP
#define OSMIUM_GEOM_GEOS_HPP

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

#include <geos/version.h>
#if defined(GEOS_VERSION_MAJOR) && defined(GEOS_VERSION_MINOR) && (GEOS_VERSION_MAJOR < 3 || (GEOS_VERSION_MAJOR == 3 && GEOS_VERSION_MINOR <= 5))

#define OSMIUM_WITH_GEOS

/**
 * @file
 *
 * This file contains code for conversion of OSM geometries into GEOS
 * geometries.
 *
 * Note that everything in this file is deprecated and only works up to
 * GEOS 3.5. It uses the GEOS C++ API which the GEOS project does not consider
 * to be a stable, external API. We probably should have used the GEOS C API
 * instead.
 *
 * @attention If you include this file, you'll need to link with `libgeos`.
 */

#include <osmium/geom/coordinates.hpp>
#include <osmium/geom/factory.hpp>
#include <osmium/util/compatibility.hpp>

#include <geos/geom/Coordinate.h>
#include <geos/geom/CoordinateSequence.h>
#include <geos/geom/CoordinateSequenceFactory.h>
#include <geos/geom/GeometryFactory.h>
#include <geos/geom/LinearRing.h>
#include <geos/geom/MultiPolygon.h>
#include <geos/geom/Point.h>
#include <geos/geom/Polygon.h>
#include <geos/geom/PrecisionModel.h>
#include <geos/util/GEOSException.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// MSVC doesn't support throw_with_nested yet
#ifdef _MSC_VER
# define THROW throw
#else
# include <exception>
# define THROW std::throw_with_nested
#endif

namespace osmium {

    struct geos_geometry_error : public geometry_error {

        explicit geos_geometry_error(const char* message) :
            geometry_error(std::string{"geometry creation failed in GEOS library: "} + message) {
        }

    }; // struct geos_geometry_error

    namespace geom {

        namespace detail {

            /// @deprecated
            class GEOSFactoryImpl {

                std::unique_ptr<const geos::geom::PrecisionModel> m_precision_model;
                std::unique_ptr<geos::geom::GeometryFactory> m_our_geos_factory;
                geos::geom::GeometryFactory* m_geos_factory;

                std::unique_ptr<geos::geom::CoordinateSequence> m_coordinate_sequence;
                std::vector<std::unique_ptr<geos::geom::LinearRing>> m_rings;
                std::vector<std::unique_ptr<geos::geom::Polygon>> m_polygons;

            public:

                using point_type        = std::unique_ptr<geos::geom::Point>;
                using linestring_type   = std::unique_ptr<geos::geom::LineString>;
                using polygon_type      = std::unique_ptr<geos::geom::Polygon>;
                using multipolygon_type = std::unique_ptr<geos::geom::MultiPolygon>;
                using ring_type         = std::unique_ptr<geos::geom::LinearRing>;

                explicit GEOSFactoryImpl(int /* srid */, geos::geom::GeometryFactory& geos_factory) :
                    m_precision_model(nullptr),
                    m_our_geos_factory(nullptr),
                    m_geos_factory(&geos_factory) {
                }

                /**
                 * @deprecated Do not set SRID explicitly. It will be set to the
                 *             correct value automatically.
                 */
                OSMIUM_DEPRECATED explicit GEOSFactoryImpl(int /* srid */, int srid) :
                    m_precision_model(new geos::geom::PrecisionModel),
                    m_our_geos_factory(new geos::geom::GeometryFactory{m_precision_model.get(), srid}),
                    m_geos_factory(m_our_geos_factory.get()) {
                }

                explicit GEOSFactoryImpl(int srid) :
                    m_precision_model(new geos::geom::PrecisionModel),
                    m_our_geos_factory(new geos::geom::GeometryFactory{m_precision_model.get(), srid}),
                    m_geos_factory(m_our_geos_factory.get()) {
                }

                /* Point */

                point_type make_point(const osmium::geom::Coordinates& xy) const {
                    try {
                        return point_type{m_geos_factory->createPoint(geos::geom::Coordinate{xy.x, xy.y})};
                    } catch (const geos::util::GEOSException& e) {
                        THROW(osmium::geos_geometry_error(e.what()));
                    }
                }

                /* LineString */

                void linestring_start() {
                    try {
                        m_coordinate_sequence.reset(m_geos_factory->getCoordinateSequenceFactory()->create(static_cast<std::size_t>(0), 2));
                    } catch (const geos::util::GEOSException& e) {
                        THROW(osmium::geos_geometry_error(e.what()));
                    }
                }

                void linestring_add_location(const osmium::geom::Coordinates& xy) {
                    try {
                        m_coordinate_sequence->add(geos::geom::Coordinate{xy.x, xy.y});
                    } catch (const geos::util::GEOSException& e) {
                        THROW(osmium::geos_geometry_error(e.what()));
                    }
                }

                linestring_type linestring_finish(std::size_t /* num_points */) {
                    try {
                        return linestring_type{m_geos_factory->createLineString(m_coordinate_sequence.release())};
                    } catch (const geos::util::GEOSException& e) {
                        THROW(osmium::geos_geometry_error(e.what()));
                    }
                }

                /* MultiPolygon */

                void multipolygon_start() {
                    m_polygons.clear();
                }

                void multipolygon_polygon_start() {
                    m_rings.clear();
                }

                void multipolygon_polygon_finish() {
                    try {
                        assert(!m_rings.empty());
                        auto inner_rings = new std::vector<geos::geom::Geometry*>;
                        std::transform(std::next(m_rings.begin(), 1), m_rings.end(), std::back_inserter(*inner_rings), [](std::unique_ptr<geos::geom::LinearRing>& r) {
                            return r.release();
                        });
                        m_polygons.emplace_back(m_geos_factory->createPolygon(m_rings[0].release(), inner_rings));
                        m_rings.clear();
                    } catch (const geos::util::GEOSException& e) {
                        THROW(osmium::geos_geometry_error(e.what()));
                    }
                }

                void multipolygon_outer_ring_start() {
                    try {
                        m_coordinate_sequence.reset(m_geos_factory->getCoordinateSequenceFactory()->create(static_cast<std::size_t>(0), 2));
                    } catch (const geos::util::GEOSException& e) {
                        THROW(osmium::geos_geometry_error(e.what()));
                    }
                }

                void multipolygon_outer_ring_finish() {
                    try {
                        m_rings.emplace_back(m_geos_factory->createLinearRing(m_coordinate_sequence.release()));
                    } catch (const geos::util::GEOSException& e) {
                        THROW(osmium::geos_geometry_error(e.what()));
                    }
                }

                void multipolygon_inner_ring_start() {
                    try {
                        m_coordinate_sequence.reset(m_geos_factory->getCoordinateSequenceFactory()->create(static_cast<std::size_t>(0), 2));
                    } catch (const geos::util::GEOSException& e) {
                        THROW(osmium::geos_geometry_error(e.what()));
                    }
                }

                void multipolygon_inner_ring_finish() {
                    try {
                        m_rings.emplace_back(m_geos_factory->createLinearRing(m_coordinate_sequence.release()));
                    } catch (const geos::util::GEOSException& e) {
                        THROW(osmium::geos_geometry_error(e.what()));
                    }
                }

                void multipolygon_add_location(const osmium::geom::Coordinates& xy) {
                    try {
                        m_coordinate_sequence->add(geos::geom::Coordinate{xy.x, xy.y});
                    } catch (const geos::util::GEOSException& e) {
                        THROW(osmium::geos_geometry_error(e.what()));
                    }
                }

                multipolygon_type multipolygon_finish() {
                    try {
                        auto polygons = new std::vector<geos::geom::Geometry*>;
                        std::transform(m_polygons.begin(), m_polygons.end(), std::back_inserter(*polygons), [](std::unique_ptr<geos::geom::Polygon>& p) {
                            return p.release();
                        });
                        m_polygons.clear();
                        return multipolygon_type{m_geos_factory->createMultiPolygon(polygons)};
                    } catch (const geos::util::GEOSException& e) {
                        THROW(osmium::geos_geometry_error(e.what()));
                    }
                }

            }; // class GEOSFactoryImpl

        } // namespace detail

        /// @deprecated
        template <typename TProjection = IdentityProjection>
        using GEOSFactory = GeometryFactory<osmium::geom::detail::GEOSFactoryImpl, TProjection>;

    } // namespace geom

} // namespace osmium

#undef THROW

#endif

#endif // OSMIUM_GEOM_GEOS_HPP

#ifndef OSMIUM_MEMORY_ITEM_HPP
#define OSMIUM_MEMORY_ITEM_HPP

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

#include <cstddef>
#include <cstdint>

namespace osmium {

    // forward declaration, see osmium/osm/item_type.hpp for declaration
    enum class item_type : uint16_t;

    namespace builder {
        class Builder;
    } // namespace builder

    enum class diff_indicator_type {
        none  = 0,
        left  = 1,
        right = 2,
        both  = 3
    }; // diff_indicator_type

    namespace memory {

        using item_size_type = uint32_t;

        // align datastructures to this many bytes
        constexpr const std::size_t align_bytes = 8;

        inline constexpr std::size_t padded_length(std::size_t length) noexcept {
            return (length + align_bytes - 1) & ~(align_bytes - 1);
        }

        /**
         * @brief Namespace for Osmium internal use
         */
        namespace detail {

            /**
            * This class contains only a helper method used in several
            * other classes.
            */
            class ItemHelper {

            protected:

                ItemHelper() noexcept = default;

                ItemHelper(const ItemHelper&) noexcept = default;
                ItemHelper(ItemHelper&&) noexcept = default;

                ItemHelper& operator=(const ItemHelper&) noexcept = default;
                ItemHelper& operator=(ItemHelper&&) noexcept = default;

                ~ItemHelper() noexcept = default;

            public:

                unsigned char* data() noexcept {
                    return reinterpret_cast<unsigned char*>(this);
                }

                const unsigned char* data() const noexcept {
                    return reinterpret_cast<const unsigned char*>(this);
                }

            }; // class ItemHelper

        } // namespace detail

        class Item : public osmium::memory::detail::ItemHelper {

            item_size_type m_size;
            item_type m_type;
            uint16_t m_removed : 1;
            uint16_t m_diff : 2;
            uint16_t m_padding : 13;

            template <typename TMember>
            friend class CollectionIterator;

            template <typename TMember>
            friend class ItemIterator;

            friend class osmium::builder::Builder;

            Item& add_size(const item_size_type size) noexcept {
                m_size += size;
                return *this;
            }

        protected:

            explicit Item(item_size_type size = 0, item_type type = item_type()) noexcept :
                m_size(size),
                m_type(type),
                m_removed(false),
                m_diff(0),
                m_padding(0) {
            }

            Item& set_type(const item_type item_type) noexcept {
                m_type = item_type;
                return *this;
            }

        public:

            Item(const Item&) = delete;
            Item& operator=(const Item&) = delete;

            Item(Item&&) = delete;
            Item& operator=(Item&&) = delete;

            ~Item() noexcept = default;

            constexpr static bool is_compatible_to(osmium::item_type /*t*/) noexcept {
                return true;
            }

            unsigned char* next() noexcept {
                return data() + padded_size();
            }

            const unsigned char* next() const noexcept {
                return data() + padded_size();
            }

            item_size_type byte_size() const noexcept {
                return m_size;
            }

            item_size_type padded_size() const {
                return static_cast<item_size_type>(padded_length(m_size));
            }

            item_type type() const noexcept {
                return m_type;
            }

            bool removed() const noexcept {
                return m_removed;
            }

            void set_removed(bool removed) noexcept {
                m_removed = removed;
            }

            diff_indicator_type diff() const noexcept {
                return diff_indicator_type(m_diff);
            }

            char diff_as_char() const noexcept {
                static constexpr const char* diff_chars = "*-+ ";
                return diff_chars[m_diff];
            }

            void set_diff(diff_indicator_type diff) noexcept {
                m_diff = uint16_t(diff);
            }

        }; // class Item

        static_assert(sizeof(Item) == 8, "Class osmium::Item has wrong size!");
        static_assert(sizeof(Item) % align_bytes == 0, "Class osmium::Item has wrong size to be aligned properly!");

    } // namespace memory

} // namespace osmium

#endif // OSMIUM_MEMORY_ITEM_HPP

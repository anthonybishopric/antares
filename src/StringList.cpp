// Ares, a tactical space combat game.
// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include "StringList.hpp"

#include <assert.h>
#include "Casts.hpp"
#include "Resource.hpp"

void StringList::load(int id) {
    _strings.clear();

    Resource rsrc('STR#', id);
    uint16_t size = (implicit_cast<uint16_t>(rsrc.data()[0]) << 8) + rsrc.data()[1];
    const char* data = rsrc.data() + 2;
    for (size_t i = 0; i < size; ++i) {
        uint8_t len = *data;
        ++data;
        _strings.push_back(std::string(data, len));
        data += len;
    }
}

ssize_t StringList::index_of(std::string& result) const {
    for (size_t i = 0; i < size(); ++i) {
        if (at(i) == result) {
            return i;
        }
    }
    return -1;
}

size_t StringList::size() const {
    return _strings.size();
}

const std::string& StringList::at(size_t index) const {
    return _strings.at(index);
}

void string_to_pstring(const std::string& src, unsigned char* dst) {
    assert(src.size() <= 254);
    *dst = src.size();
    memcpy(dst + 1, src.c_str(), src.size());
}

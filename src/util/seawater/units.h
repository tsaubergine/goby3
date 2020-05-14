// Copyright 2019-2020:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#ifndef UNITS_20190606H
#define UNITS_20190606H

#include <boost/units/base_units/metric/bar.hpp>

namespace goby
{
namespace util
{
namespace seawater
{
static const boost::units::metric::bar_base_unit::unit_type bar;
static const decltype((boost::units::si::milli * boost::units::si::siemen) /
                      (boost::units::si::centi * boost::units::si::meters)) milli_siemens_per_cm;

} // namespace seawater
} // namespace util
} // namespace goby

#endif

// Copyright 2009-2021:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
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

#include <cctype> // for isdigit
#include <cmath>  // for floor
#include <memory> // for allocato...

#include <boost/algorithm/string/case_conv.hpp>             // for to_lower...
#include <boost/bind/bind.hpp>                                   // for bind_t, arg
#include <boost/date_time/posix_time/posix_time_config.hpp> // for time_dur...
#include <boost/date_time/posix_time/ptime.hpp>             // for ptime
#include <boost/date_time/time.hpp>                         // for base_tim...
#include <boost/date_time/time_system_counted.hpp>          // for counted_...
#include <boost/format.hpp>                                 // for basic_al...
#include <boost/math/special_functions/fpclassify.hpp>      // for isnan
#include <boost/units/absolute.hpp>                         // for absolute
#include <boost/units/quantity.hpp>                         // for operator*
#include <boost/units/systems/si/length.hpp>                // for length
#include <boost/units/systems/si/time.hpp>                  // for seconds
#include <boost/units/systems/si/velocity.hpp>              // for meters_p...
#include <boost/units/systems/temperature/celsius.hpp>      // for temperature
#include <boost/units/unit.hpp>                             // for unit

#include "goby/moos/transitional/message_algorithms.h" // for DCCLAlgo...
#include "goby/moos/transitional/message_val.h"        // for DCCLMess...
#include "goby/time/convert.h"                         // for convert
#include "goby/util/debug_logger/flex_ostream.h"       // for operator<<
#include "goby/util/debug_logger/flex_ostreambuf.h"    // for DEBUG1
#include "goby/util/seawater/soundspeed.h"             // for mackenzi...
#include "moos_translator.h"

const double NaN = std::numeric_limits<double>::quiet_NaN();

void goby::moos::MOOSTranslator::update_utm_datum(double lat_origin, double lon_origin)
{
    if (!(boost::math::isnan)(lat_origin) && !(boost::math::isnan)(lon_origin))
    {
        if (geodesy_.Initialise(lat_origin, lon_origin))
        {
            moos::transitional::DCCLAlgorithmPerformer* ap =
                moos::transitional::DCCLAlgorithmPerformer::getInstance();

            ap->add_adv_algorithm("lat2utm_y",
                                  boost::bind(&MOOSTranslator::alg_lat2utm_y, this,
                                              boost::placeholders::_1, boost::placeholders::_2));
            ap->add_adv_algorithm("lon2utm_x",
                                  boost::bind(&MOOSTranslator::alg_lon2utm_x, this,
                                              boost::placeholders::_1, boost::placeholders::_2));
            ap->add_adv_algorithm("utm_x2lon",
                                  boost::bind(&MOOSTranslator::alg_utm_x2lon, this,
                                              boost::placeholders::_1, boost::placeholders::_2));
            ap->add_adv_algorithm("utm_y2lat",
                                  boost::bind(&MOOSTranslator::alg_utm_y2lat, this,
                                              boost::placeholders::_1, boost::placeholders::_2));
        }
    }
}

void goby::moos::MOOSTranslator::initialize(double lat_origin, double lon_origin,
                                            const std::string& modem_id_lookup_path)
{
    moos::transitional::DCCLAlgorithmPerformer* ap =
        moos::transitional::DCCLAlgorithmPerformer::getInstance();

    // set up algorithms
    ap->add_algorithm("power_to_dB", &alg_power_to_dB);
    ap->add_algorithm("dB_to_power", &alg_dB_to_power);
    ap->add_adv_algorithm("TSD_to_soundspeed", &alg_TSD_to_soundspeed);
    ap->add_algorithm("to_lower", &alg_to_lower);
    ap->add_algorithm("to_upper", &alg_to_upper);
    ap->add_algorithm("angle_0_360", &alg_angle_0_360);
    ap->add_algorithm("angle_-180_180", &alg_angle_n180_180);
    ap->add_algorithm("lat2hemisphere_initial", &alg_lat2hemisphere_initial);
    ap->add_algorithm("lon2hemisphere_initial", &alg_lon2hemisphere_initial);

    ap->add_algorithm("lat2nmea_lat", &alg_lat2nmea_lat);
    ap->add_algorithm("lon2nmea_lon", &alg_lon2nmea_lon);

    ap->add_algorithm("unix_time2nmea_time", &alg_unix_time2nmea_time);

    ap->add_algorithm("abs", &alg_abs);

    ap->add_adv_algorithm("add", &alg_add);
    ap->add_adv_algorithm("subtract", &alg_subtract);

    if (!modem_id_lookup_path.empty())
    {
        std::string id_lookup_output = modem_lookup_.read_lookup_file(modem_id_lookup_path);
        goby::glog.is(goby::util::logger::DEBUG1) && goby::glog << id_lookup_output << std::flush;

        ap->add_algorithm("modem_id2name", boost::bind(&MOOSTranslator::alg_modem_id2name, this,
                                                       boost::placeholders::_1));
        ap->add_algorithm("modem_id2type", boost::bind(&MOOSTranslator::alg_modem_id2type, this,
                                                       boost::placeholders::_1));
        ap->add_algorithm("name2modem_id", boost::bind(&MOOSTranslator::alg_name2modem_id, this,
                                                       boost::placeholders::_1));

        // set up conversion for DCCLModemIdConverterCodec
        //for(std::map<int, std::string>::const_iterator it = modem_lookup_.names().begin(),
        //        end = modem_lookup_.names().end(); it != end; ++it)
        //    goby::acomms::DCCLModemIdConverterCodec::add(it->second, it->first);
    }

    update_utm_datum(lat_origin, lon_origin);
}

void goby::moos::alg_power_to_dB(moos::transitional::DCCLMessageVal& val_to_mod)
{
    val_to_mod = 10 * log10(double(val_to_mod));
}

void goby::moos::alg_dB_to_power(moos::transitional::DCCLMessageVal& val_to_mod)
{
    val_to_mod = pow(10.0, double(val_to_mod) / 10.0);
}

// applied to "T" (temperature), references are "S" (salinity), then "D" (depth)
void goby::moos::alg_TSD_to_soundspeed(
    moos::transitional::DCCLMessageVal& val,
    const std::vector<moos::transitional::DCCLMessageVal>& ref_vals)
{
    try
    {
        val.set(goby::util::seawater::mackenzie_soundspeed(
                    static_cast<double>(val) *
                        boost::units::absolute<boost::units::celsius::temperature>(),
                    static_cast<double>(ref_vals[0]),
                    static_cast<double>(ref_vals[1]) * boost::units::si::meters) /
                    (boost::units::si::meters_per_second),
                3);
    }
    catch (std::out_of_range& e)
    {
        glog.is_warn() && glog << "Out of range error calculating soundspeed: " << e.what()
                               << std::endl;
        val.set(std::numeric_limits<double>::quiet_NaN());
    }
}

void goby::moos::alg_angle_0_360(moos::transitional::DCCLMessageVal& angle)
{
    double a = angle;
    while (a < 0) a += 360;
    while (a >= 360) a -= 360;
    angle = a;
}

void goby::moos::alg_angle_n180_180(moos::transitional::DCCLMessageVal& angle)
{
    double a = angle;
    while (a < -180) a += 360;
    while (a >= 180) a -= 360;
    angle = a;
}

void goby::moos::MOOSTranslator::alg_lat2utm_y(
    moos::transitional::DCCLMessageVal& mv,
    const std::vector<moos::transitional::DCCLMessageVal>& ref_vals)
{
    double lat = mv;
    double lon = ref_vals[0];
    double x = NaN;
    double y = NaN;

    if (!(boost::math::isnan)(lat) && !(boost::math::isnan)(lon))
        geodesy_.LatLong2LocalUTM(lat, lon, y, x);
    mv = y;
}

void goby::moos::MOOSTranslator::alg_lon2utm_x(
    moos::transitional::DCCLMessageVal& mv,
    const std::vector<moos::transitional::DCCLMessageVal>& ref_vals)
{
    double lon = mv;
    double lat = ref_vals[0];
    double x = NaN;
    double y = NaN;

    if (!(boost::math::isnan)(lat) && !(boost::math::isnan)(lon))
        geodesy_.LatLong2LocalUTM(lat, lon, y, x);
    mv = x;
}

void goby::moos::MOOSTranslator::alg_utm_x2lon(
    moos::transitional::DCCLMessageVal& mv,
    const std::vector<moos::transitional::DCCLMessageVal>& ref_vals)
{
    double x = mv;
    double y = ref_vals[0];

    double lat = NaN;
    double lon = NaN;
    if (!(boost::math::isnan)(y) && !(boost::math::isnan)(x))
        geodesy_.UTM2LatLong(x, y, lat, lon);

    const int LON_INT_DIGITS = 3;
    lon = dccl::round(lon, std::numeric_limits<double>::digits10 - LON_INT_DIGITS - 1);
    mv = lon;
}

void goby::moos::MOOSTranslator::alg_utm_y2lat(
    moos::transitional::DCCLMessageVal& mv,
    const std::vector<moos::transitional::DCCLMessageVal>& ref_vals)
{
    double y = mv;
    double x = ref_vals[0];

    double lat = NaN;
    double lon = NaN;
    if (!(boost::math::isnan)(x) && !(boost::math::isnan)(y))
        geodesy_.UTM2LatLong(x, y, lat, lon);

    const int LAT_INT_DIGITS = 2;
    lat = dccl::round(lat, std::numeric_limits<double>::digits10 - LAT_INT_DIGITS - 1);
    mv = lat;
}

void goby::moos::MOOSTranslator::alg_modem_id2name(moos::transitional::DCCLMessageVal& in)
{
    bool is_numeric = true;
    for (const char c : std::string(in))
    {
        if (!isdigit(c))
        {
            is_numeric = false;
            break;
        }
    }
    if (is_numeric)
        in = modem_lookup_.get_name_from_id(boost::lexical_cast<unsigned>(std::string(in)));
}

void goby::moos::MOOSTranslator::alg_modem_id2type(moos::transitional::DCCLMessageVal& in)
{
    bool is_numeric = true;
    for (const char c : std::string(in))
    {
        if (!isdigit(c))
        {
            is_numeric = false;
            break;
        }
    }

    if (is_numeric)
        in = modem_lookup_.get_type_from_id(boost::lexical_cast<unsigned>(std::string(in)));
}

void goby::moos::MOOSTranslator::alg_name2modem_id(moos::transitional::DCCLMessageVal& in)
{
    std::stringstream ss;
    ss << modem_lookup_.get_id_from_name(std::string(in));
    in = ss.str();
}

void goby::moos::alg_to_upper(moos::transitional::DCCLMessageVal& val_to_mod)
{
    val_to_mod = boost::algorithm::to_upper_copy(std::string(val_to_mod));
}

void goby::moos::alg_to_lower(moos::transitional::DCCLMessageVal& val_to_mod)
{
    val_to_mod = boost::algorithm::to_lower_copy(std::string(val_to_mod));
}

void goby::moos::alg_lat2hemisphere_initial(moos::transitional::DCCLMessageVal& val_to_mod)
{
    double lat = val_to_mod;
    if (lat < 0)
        val_to_mod = "S";
    else
        val_to_mod = "N";
}

void goby::moos::alg_lon2hemisphere_initial(moos::transitional::DCCLMessageVal& val_to_mod)
{
    double lon = val_to_mod;
    if (lon < 0)
        val_to_mod = "W";
    else
        val_to_mod = "E";
}

void goby::moos::alg_abs(moos::transitional::DCCLMessageVal& val_to_mod)
{
    val_to_mod = std::abs(double(val_to_mod));
}

void goby::moos::alg_unix_time2nmea_time(moos::transitional::DCCLMessageVal& val_to_mod)
{
    double unix_time = val_to_mod;
    boost::posix_time::ptime ptime =
        time::convert<boost::posix_time::ptime>(unix_time * boost::units::si::seconds);

    // HHMMSS.SSSSSS
    boost::format f("%02d%02d%02d.%06d");
    f % ptime.time_of_day().hours() % ptime.time_of_day().minutes() %
        ptime.time_of_day().seconds() %
        (ptime.time_of_day().fractional_seconds() * 1000000 /
         boost::posix_time::time_duration::ticks_per_second());

    val_to_mod = f.str();
}

void goby::moos::alg_lat2nmea_lat(moos::transitional::DCCLMessageVal& val_to_mod)
{
    double lat = val_to_mod;

    // DDMM.MM
    boost::format f("%02d%02d.%04d");

    int degrees = std::floor(lat);
    int minutes = std::floor((lat - degrees) * 60);
    int ten_thousandth_minutes = std::floor(((lat - degrees) * 60 - minutes) * 10000);

    f % degrees % minutes % ten_thousandth_minutes;

    val_to_mod = f.str();
}

void goby::moos::alg_lon2nmea_lon(moos::transitional::DCCLMessageVal& val_to_mod)
{
    double lon = val_to_mod;

    // DDDMM.MM
    boost::format f("%03d%02d.%04d");

    int degrees = std::floor(lon);
    int minutes = std::floor((lon - degrees) * 60);
    int ten_thousandth_minutes = std::floor(((lon - degrees) * 60 - minutes) * 10000);

    f % degrees % minutes % ten_thousandth_minutes;

    val_to_mod = f.str();
}

void goby::moos::alg_subtract(moos::transitional::DCCLMessageVal& val_to_mod,
                              const std::vector<moos::transitional::DCCLMessageVal>& ref_vals)
{
    double diff = val_to_mod;

    for (const auto& ref_val : ref_vals) diff -= static_cast<double>(ref_val);

    val_to_mod = diff;
}

void goby::moos::alg_add(moos::transitional::DCCLMessageVal& val_to_mod,
                         const std::vector<moos::transitional::DCCLMessageVal>& ref_vals)
{
    double sum = val_to_mod;

    for (const auto& ref_val : ref_vals) sum += static_cast<double>(ref_val);

    val_to_mod = sum;
}

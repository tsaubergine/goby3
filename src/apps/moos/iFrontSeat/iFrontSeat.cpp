// Copyright 2013-2022:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Henrik Schmidt <henrik@mit.edu>
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include <cstdlib> // for exit
#include <dlfcn.h>  // for dlopen
#include <iostream> // for endl
#include <list>     // for operat...
#include <map>      // for map
#include <string>   // for string
#include <vector>   // for vector

#include <MOOS/libMOOS/Comms/CommsTypes.h>         // for MOOSMS...
#include <MOOS/libMOOS/Comms/MOOSMsg.h>            // for CMOOSMsg
#include <boost/algorithm/string/predicate.hpp>    // for iequals
#include <boost/algorithm/string/trim.hpp>         // for trim
#include <boost/bind/bind.hpp>                          // for bind_t
#include <boost/program_options/variables_map.hpp> // for variab...
#include <boost/signals2/expired_slot.hpp>         // for expire...
#include <boost/signals2/signal.hpp>               // for signal
#include <boost/smart_ptr/shared_ptr.hpp>          // for shared...
#include <boost/units/quantity.hpp>                // for operator*
#include <boost/units/systems/angle/degrees.hpp>   // for degrees

#include "goby/acomms/connect.h"                              // for connect
#include "goby/middleware/application/configuration_reader.h" // for Config...
#include "goby/middleware/protobuf/frontseat.pb.h"            // for Interf...
#include "goby/middleware/protobuf/frontseat_config.pb.h"     // for Config
#include "goby/middleware/protobuf/frontseat_data.pb.h"       // for NodeSt...
#include "goby/moos/moos_protobuf_helpers.h"                  // for parse_...
#include "goby/moos/protobuf/goby_moos_app.pb.h"              // for GobyMO...
#include "goby/moos/protobuf/iFrontSeat_config.pb.h"          // for iFront...
#include "goby/util/debug_logger.h"

#include "iFrontSeat.h"

using namespace goby::util::logger;
namespace gpb = goby::middleware::frontseat::protobuf;
using goby::glog;

goby::apps::moos::protobuf::iFrontSeatConfig goby::apps::moos::iFrontSeat::cfg_;
goby::apps::moos::iFrontSeat* goby::apps::moos::iFrontSeat::inst_ = nullptr;
void* goby::apps::moos::iFrontSeat::driver_library_handle_ = nullptr;

int main(int argc, char* argv[])
{
    // load plugin driver from environmental variable IFRONTSEAT_DRIVER_LIBRARY
    char* driver_lib_path = getenv("IFRONTSEAT_DRIVER_LIBRARY");
    if (driver_lib_path)
    {
        std::cerr << "Loading iFrontSeat driver library: " << driver_lib_path << std::endl;
        goby::apps::moos::iFrontSeat::driver_library_handle_ = dlopen(driver_lib_path, RTLD_LAZY);
        if (!goby::apps::moos::iFrontSeat::driver_library_handle_)
        {
            std::cerr << "Failed to open library: " << driver_lib_path << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        std::cerr << "Environmental variable IFRONTSEAT_DRIVER_LIBRARY must be set with name of "
                     "the dynamic library containing the specific driver to use."
                  << std::endl;
        exit(EXIT_FAILURE);
    }

    return goby::moos::run<goby::apps::moos::iFrontSeat>(argc, argv);
}

goby::apps::moos::iFrontSeat* goby::apps::moos::iFrontSeat::get_instance()
{
    if (!inst_)
        inst_ = new goby::apps::moos::iFrontSeat();
    return inst_;
}

goby::middleware::frontseat::InterfaceBase*
load_driver(goby::apps::moos::protobuf::iFrontSeatConfig* cfg)
{
    typedef goby::middleware::frontseat::InterfaceBase* (*driver_load_func)(gpb::Config*);
    auto driver_load_ptr = (driver_load_func)dlsym(
        goby::apps::moos::iFrontSeat::driver_library_handle_, "frontseat_driver_load");

    if (!driver_load_ptr)
    {
        glog.is(DIE) && glog << "Function frontseat_driver_load in library defined in "
                                "IFRONTSEAT_DRIVER_LIBRARY does not exist."
                             << std::endl;
        // suppress clang static analyzer false positive
        exit(EXIT_FAILURE);
    }

    cfg->mutable_frontseat_cfg()->set_name(cfg->common().community());
    cfg->mutable_frontseat_cfg()->mutable_origin()->set_lat_with_units(
        cfg->common().lat_origin() * boost::units::degree::degrees);
    cfg->mutable_frontseat_cfg()->mutable_origin()->set_lon_with_units(
        cfg->common().lon_origin() * boost::units::degree::degrees);

    cfg->mutable_frontseat_cfg()->set_sim_warp_factor(cfg->common().time_warp_multiplier());

    goby::middleware::frontseat::InterfaceBase* driver =
        (*driver_load_ptr)(cfg->mutable_frontseat_cfg());

    if (!driver)
    {
        glog.is(DIE) && glog << "Function frontseat_driver_load in library defined in "
                                "IFRONTSEAT_DRIVER_LIBRARY returned a null pointer."
                             << std::endl;
        // suppress clang static analyzer false positive
        exit(EXIT_FAILURE);
    }
    return driver;
}

goby::apps::moos::iFrontSeat::iFrontSeat()
    : goby::moos::GobyMOOSApp(&cfg_),
      frontseat_(load_driver(&cfg_)),
      translator_(this),
      lat_origin_(std::numeric_limits<double>::quiet_NaN()),
      lon_origin_(std::numeric_limits<double>::quiet_NaN()),
      new_origin_(false)
{
    // commands
    subscribe(cfg_.moos_var().prefix() + cfg_.moos_var().command_request(),
              &iFrontSeat::handle_mail_command_request, this);
    goby::acomms::connect(&frontseat_->signal_command_response, this,
                          &iFrontSeat::handle_driver_command_response);

    // data
    subscribe(cfg_.moos_var().prefix() + cfg_.moos_var().data_to_frontseat(),
              &iFrontSeat::handle_mail_data_to_frontseat, this);
    goby::acomms::connect(&frontseat_->signal_data_from_frontseat, this,
                          &iFrontSeat::handle_driver_data_from_frontseat);

    // raw
    subscribe(cfg_.moos_var().prefix() + cfg_.moos_var().raw_out(),
              &iFrontSeat::handle_mail_raw_out, this);
    goby::acomms::connect(&frontseat_->signal_raw_from_frontseat, this,
                          &iFrontSeat::handle_driver_raw_in);

    goby::acomms::connect(&frontseat_->signal_raw_to_frontseat, this,
                          &iFrontSeat::handle_driver_raw_out);

    // IvP Helm State
    subscribe("IVPHELM_STATE", &iFrontSeat::handle_mail_helm_state, this);

    register_timer(cfg_.frontseat_cfg().status_period(),
                   boost::bind(&iFrontSeat::status_loop, this));

    // Dynamic UTM. H. Schmidt 7/30/21
    subscribe("LAT_ORIGIN", &iFrontSeat::handle_lat_origin, this);
    subscribe("LONG_ORIGIN", &iFrontSeat::handle_lon_origin, this);
}

void goby::apps::moos::iFrontSeat::handle_lat_origin(const CMOOSMsg& msg)
{
    double new_lat = msg.GetDouble();
    if (!isnan(new_lat))
    {
        lat_origin_ = new_lat;
        new_origin_ = true;
    }
}

void goby::apps::moos::iFrontSeat::handle_lon_origin(const CMOOSMsg& msg)
{
    double new_lon = msg.GetDouble();
    if (!isnan(new_lon))
    {
        lon_origin_ = new_lon;
        new_origin_ = true;
    }
}

void goby::apps::moos::iFrontSeat::loop()
{
    if (new_origin_ && !isnan(lat_origin_) && !isnan(lon_origin_))
    {
        boost::units::quantity<boost::units::degree::plane_angle> lat =
            lat_origin_ * boost::units::degree::degrees;
        boost::units::quantity<boost::units::degree::plane_angle> lon =
            lon_origin_ * boost::units::degree::degrees;

        frontseat_->update_utm_datum({lat, lon});

        new_origin_ = false;
    }
    frontseat_->do_work();

    if (cfg_.frontseat_cfg().exit_on_error() && (frontseat_->state() == gpb::INTERFACE_FS_ERROR ||
                                                 frontseat_->state() == gpb::INTERFACE_HELM_ERROR))
    {
        glog.is(DIE) &&
            glog << "Error state detected and `exit_on_error` == true, so quitting. Bye!"
                 << std::endl;
    }
}

void goby::apps::moos::iFrontSeat::status_loop()
{
    glog.is(DEBUG1) && glog << "Status: " << frontseat_->status().ShortDebugString() << std::endl;
    publish_pb(cfg_.moos_var().prefix() + cfg_.moos_var().status(), frontseat_->status());
}

void goby::apps::moos::iFrontSeat::handle_mail_command_request(const CMOOSMsg& msg)
{
    if (frontseat_->state() != gpb::INTERFACE_COMMAND)
    {
        glog.is(DEBUG1) &&
            glog << "Not sending command because the interface is not in the command state"
                 << std::endl;
    }
    else
    {
        gpb::CommandRequest command;
        parse_for_moos(msg.GetString(), &command);
        frontseat_->send_command_to_frontseat(command);
    }
}

void goby::apps::moos::iFrontSeat::handle_mail_data_to_frontseat(const CMOOSMsg& msg)
{
    if (frontseat_->state() != gpb::INTERFACE_COMMAND &&
        frontseat_->state() != gpb::INTERFACE_LISTEN)
    {
        glog.is(DEBUG1) &&
            glog << "Not sending data because the interface is not in the command or listen state"
                 << std::endl;
    }
    else
    {
        gpb::InterfaceData data;
        parse_for_moos(msg.GetString(), &data);
        frontseat_->send_data_to_frontseat(data);
    }
}

void goby::apps::moos::iFrontSeat::handle_mail_raw_out(const CMOOSMsg& msg)
{
    // no recursively sending our own messages
    if (msg.GetSource() == GetAppName())
        return;

    if (frontseat_->state() != gpb::INTERFACE_COMMAND &&
        frontseat_->state() != gpb::INTERFACE_LISTEN)
    {
        glog.is(DEBUG1) &&
            glog << "Not sending raw because the interface is not in the command or listen state"
                 << std::endl;
    }
    else
    {
        gpb::Raw raw;
        parse_for_moos(msg.GetString(), &raw);
        frontseat_->send_raw_to_frontseat(raw);
    }
}

void goby::apps::moos::iFrontSeat::handle_mail_helm_state(const CMOOSMsg& msg)
{
    std::string sval = msg.GetString();
    boost::trim(sval);
    std::string src = msg.GetSource();
    boost::trim(src);
    if (boost::iequals(src, "phelmivp"))
    {
        if (boost::iequals(sval, "drive"))
            frontseat_->set_helm_state(gpb::HELM_DRIVE);
        else if (boost::iequals(sval, "park"))
            frontseat_->set_helm_state(gpb::HELM_PARK);
    }
    else
    {
        if (boost::iequals(src, "phelmivp_standby"))
        {
            if (boost::iequals(sval, "drive+"))
                frontseat_->set_helm_state(gpb::HELM_DRIVE);
            else if (boost::iequals(sval, "park+"))
                frontseat_->set_helm_state(gpb::HELM_PARK);
        }
        else
            frontseat_->set_helm_state(gpb::HELM_NOT_RUNNING);
    }
}

void goby::apps::moos::iFrontSeat::handle_driver_command_response(
    const gpb::CommandResponse& response)
{
    publish_pb(cfg_.moos_var().prefix() + cfg_.moos_var().command_response(), response);
}

void goby::apps::moos::iFrontSeat::handle_driver_data_from_frontseat(const gpb::InterfaceData& data)
{
    publish_pb(cfg_.moos_var().prefix() + cfg_.moos_var().data_from_frontseat(), data);
    if (data.has_node_status())
        publish_pb(cfg_.moos_var().prefix() + cfg_.moos_var().node_status(), data.node_status());
}

void goby::apps::moos::iFrontSeat::handle_driver_raw_in(const gpb::Raw& data)
{
    publish_pb(cfg_.moos_var().prefix() + cfg_.moos_var().raw_in(), data);
}

void goby::apps::moos::iFrontSeat::handle_driver_raw_out(const gpb::Raw& data)
{
    publish_pb(cfg_.moos_var().prefix() + cfg_.moos_var().raw_out(), data);
}

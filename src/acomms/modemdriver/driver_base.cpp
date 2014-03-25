// Copyright 2009-2014 Toby Schneider (https://launchpad.net/~tes)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Goby Developers Team (https://launchpad.net/~goby-dev)
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



#include <boost/thread/mutex.hpp>
#include <boost/format.hpp>

#include "goby/acomms/connect.h"
#include "goby/common/logger.h"

#include "driver_base.h"
#include "driver_exception.h"

using namespace goby::common::logger;
using namespace goby::common::logger_lock;


int goby::acomms::ModemDriverBase::count_ = 0;



goby::acomms::ModemDriverBase::ModemDriverBase()
    : modem_(0)
{
    ++count_;

    glog_out_group_ = "goby::acomms::modemdriver::out::" + goby::util::as<std::string>(count_);
    glog_in_group_ = "goby::acomms::modemdriver::in::" + goby::util::as<std::string>(count_);
    
    goby::glog.add_group(glog_out_group_, common::Colors::lt_magenta);
    goby::glog.add_group(glog_in_group_, common::Colors::lt_blue);

}

goby::acomms::ModemDriverBase::~ModemDriverBase()
{
    if(modem_) delete modem_;
}

void goby::acomms::ModemDriverBase::modem_write(const std::string& out)
{
    if(!modem_->active())
    {
        goby::glog.is(DEBUG1, lock) && goby::glog << group(glog_out_group_) << warn << "modem is closed! (check physical connection)" << std::endl << unlock;
    }
    else
    {
        modem_->write(out);
    }
}

bool goby::acomms::ModemDriverBase::modem_read(std::string* in)
{
    if(!modem_->active())
    {
        goby::glog.is(DEBUG1, lock) && goby::glog << group(glog_in_group_) << warn << "modem is closed! (check physical connection)" << std::endl << unlock;
        return false;
    }
    else
    {
        return modem_->readline(in);
    }
}

void goby::acomms::ModemDriverBase::modem_close()
{
    modem_->close();    
}



void goby::acomms::ModemDriverBase::modem_start(const protobuf::DriverConfig& cfg)
{        
    if(!cfg.has_modem_id())
        throw(ModemDriverException("missing modem_id in configuration"));
    
    switch(cfg.connection_type())
    {
        case protobuf::DriverConfig::CONNECTION_SERIAL:
            goby::glog.is(DEBUG1, lock) && goby::glog << group(glog_out_group_) << "opening serial port " << cfg.serial_port() << " @ " << cfg.serial_baud() << std::endl << unlock;

            if(!cfg.has_serial_port())
                throw(ModemDriverException("missing serial port in configuration"));
            if(!cfg.has_serial_baud())
                throw(ModemDriverException("missing serial baud in configuration"));
            
            modem_ = new util::SerialClient(cfg.serial_port(), cfg.serial_baud(), cfg.line_delimiter());
            break;
            
        case protobuf::DriverConfig::CONNECTION_TCP_AS_CLIENT:
            goby::glog.is(DEBUG1, lock) && goby::glog << group(glog_out_group_) << "opening tcp client: " << cfg.tcp_server() << ":" << cfg.tcp_port() << std::endl << unlock;
            if(!cfg.has_tcp_server())
                throw(ModemDriverException("missing tcp server address in configuration"));
            if(!cfg.has_tcp_port())
                throw(ModemDriverException("missing tcp port in configuration"));

            modem_ = new util::TCPClient(cfg.tcp_server(), cfg.tcp_port(), cfg.line_delimiter(), cfg.reconnect_interval());
            break;
            
        case protobuf::DriverConfig::CONNECTION_TCP_AS_SERVER:
            goby::glog.is(DEBUG1, lock) && goby::glog << group(glog_out_group_) << "opening tcp server on port" << cfg.tcp_port() << std::endl << unlock;

            if(!cfg.has_tcp_port())
                throw(ModemDriverException("missing tcp port in configuration"));

            modem_ = new util::TCPServer(cfg.tcp_port(), cfg.line_delimiter());
    }    


    if(cfg.has_raw_log())
    {
        using namespace boost::posix_time;
        boost::format file_format(cfg.raw_log());
        file_format.exceptions( boost::io::all_error_bits ^ ( boost::io::too_many_args_bit | boost::io::too_few_args_bit)); 
        
        std::string file_name = (file_format % to_iso_string(second_clock::universal_time())).str();

        glog.is(DEBUG1, lock) && glog << group(glog_out_group_) << "logging NMEA-0183 output to file: " << file_name << std::endl << unlock;

        raw_fs_.reset(new std::ofstream(file_name.c_str()));

        
        if(raw_fs_->is_open())
        {
            connect(&signal_raw_incoming, boost::bind(&ModemDriverBase::write_raw, this, _1, true));
            connect(&signal_raw_outgoing, boost::bind(&ModemDriverBase::write_raw, this, _1, false));
        }
        else
        {
            glog.is(DEBUG1, lock) && glog << group(glog_out_group_) << warn << "Failed to open log file" << std::endl << unlock;
            raw_fs_.reset();
        }
    }


    
    modem_->start();
}

void goby::acomms::ModemDriverBase::write_raw(const protobuf::ModemRaw& msg, bool rx)
{
    if(rx) *raw_fs_ << "[rx] ";
    else   *raw_fs_ << "[tx] ";
    *raw_fs_ << msg.raw() << std::endl;
}

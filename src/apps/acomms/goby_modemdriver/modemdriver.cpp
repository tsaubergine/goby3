// Copyright 2009-2014 Toby Schneider (https://launchpad.net/~tes)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Goby Developers Team (https://launchpad.net/~goby-dev)
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


#include "goby/common/logger.h"
#include "goby/common/logger/term_color.h"
#include "goby/common/zeromq_service.h"

#include "goby/acomms/modem_driver.h"
#include "goby/acomms/modemdriver/udp_driver.h"
#include "goby/pb/iridium_driver.h"
#include "goby/acomms/connect.h"

#include "goby/pb/application.h"
#include "goby/pb/pb_modem_driver.h"

#include "modemdriver_config.pb.h"

using namespace goby::common::logger;

namespace goby
{
    namespace acomms
    {
        class ModemDriver : public goby::pb::Application
        {
        public:
            ModemDriver();
            ~ModemDriver();

        private:
            void loop();

            void handle_modem_data_request(protobuf::ModemTransmission* msg);
            void handle_modem_receive(const protobuf::ModemTransmission& message);

            void handle_data_response(const protobuf::ModemTransmission& message);
            void handle_initiate_transmission(const protobuf::ModemTransmission& message);

        private:
            static protobuf::ModemDriverConfig cfg_;
            
            // for PBDriver, IridiumDriver
            boost::shared_ptr<goby::common::ZeroMQService> zeromq_service_;
            
            // for UDPDriver
            boost::shared_ptr<boost::asio::io_service> asio_service_;
            
            boost::shared_ptr<goby::acomms::ModemDriverBase> driver_;

            bool data_response_received_;
            protobuf::ModemTransmission data_response_;

            bool initiate_transmit_pending_;
            protobuf::ModemTransmission initiate_transmission_;
            
        };
    }
}

goby::acomms::protobuf::ModemDriverConfig goby::acomms::ModemDriver::cfg_;

int main(int argc, char* argv[])
{
    goby::run<goby::acomms::ModemDriver>(argc, argv);
}


using goby::glog;

goby::acomms::ModemDriver::ModemDriver()
    : goby::pb::Application(&cfg_),
      data_response_received_(false),
      initiate_transmit_pending_(false)
{
    glog.is(DEBUG1) && glog << cfg_.DebugString() << std::endl;

    switch(cfg_.driver_type())
    {
        case goby::acomms::protobuf::DRIVER_WHOI_MICROMODEM:
            driver_.reset(new goby::acomms::MMDriver);
            break;

        case goby::acomms::protobuf::DRIVER_PB_STORE_SERVER:
            zeromq_service_.reset(new goby::common::ZeroMQService);
            driver_.reset(new goby::pb::PBDriver(zeromq_service_.get()));
            break;

        case goby::acomms::protobuf::DRIVER_UDP:
            asio_service_.reset(new boost::asio::io_service);
            driver_.reset(new goby::acomms::UDPDriver(asio_service_.get()));
            break;

        case goby::acomms::protobuf::DRIVER_IRIDIUM:
            zeromq_service_.reset(new goby::common::ZeroMQService); 
            driver_.reset(new goby::acomms::IridiumDriver(zeromq_service_.get()));
            
            break;


        default:
        case goby::acomms::protobuf::DRIVER_NONE:
            throw(goby::Exception("Invalid/unsupported driver specified"));
            break;
    }

    subscribe(&ModemDriver::handle_initiate_transmission, this, "Tx" + goby::util::as<std::string>(cfg_.driver_cfg().modem_id()));

    subscribe(&ModemDriver::handle_data_response, this, "DataResponse" + goby::util::as<std::string>(cfg_.driver_cfg().modem_id()));

    connect(&driver_->signal_receive,
            this, &ModemDriver::handle_modem_receive);
    
    connect(&driver_->signal_data_request,
            this, &ModemDriver::handle_modem_data_request);
    
    driver_->startup(cfg_.driver_cfg());

}


goby::acomms::ModemDriver::~ModemDriver()
{
    if(driver_)
        driver_->shutdown();
}


void goby::acomms::ModemDriver::loop()
{
    if(driver_)
        driver_->do_work();

    if(initiate_transmit_pending_)
    {
        driver_->handle_initiate_transmission(initiate_transmission_);
        initiate_transmit_pending_ = false;
    }
    
}

void goby::acomms::ModemDriver::handle_modem_data_request(protobuf::ModemTransmission* msg)
{
    publish(*msg, "DataRequest" + goby::util::as<std::string>(cfg_.driver_cfg().modem_id()));
    data_response_received_ = false;
    
    double start_time = goby::common::goby_time<double>();
    while(goby::common::goby_time<double>() < start_time + cfg_.data_request_timeout())
    {
        zeromq_service().poll(10000);
        if(data_response_received_)
        {
            *msg = data_response_;
            break;
        }
    }
    if(!data_response_received_)
        glog.is(WARN) && glog << "Timeout waiting for response to data request" << std::endl;
}

void goby::acomms::ModemDriver::handle_modem_receive(const protobuf::ModemTransmission& message)
{
    publish(message, "Rx" + goby::util::as<std::string>(cfg_.driver_cfg().modem_id()));
}


void goby::acomms::ModemDriver::handle_data_response(const protobuf::ModemTransmission& message)
{
    data_response_received_ = true;
    data_response_ = message;
}


void goby::acomms::ModemDriver::handle_initiate_transmission(const protobuf::ModemTransmission& message)
{
    // wait until we enter next loop to initiate the transmission to avoid calling poll() from within poll()
    initiate_transmit_pending_ = true;
    initiate_transmission_ = message;
}


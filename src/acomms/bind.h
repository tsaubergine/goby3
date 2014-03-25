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



// gives functions for binding the goby-acomms libraries together

#ifndef BIND20100120H
#define BIND20100120H

#include <boost/bind.hpp>

#include "goby/acomms/connect.h"
#include "goby/acomms/dccl.h"
#include "goby/acomms/queue.h"
#include "goby/acomms/modem_driver.h"
#include "goby/acomms/amac.h"
#include "goby/acomms/route.h"
#include "goby/common/logger.h"

namespace goby
{
    namespace acomms
    {   
        
        /// binds the driver link-layer callbacks to the QueueManager
        inline void bind(ModemDriverBase& driver, QueueManager& queue_manager)
        {
            connect(&driver.signal_receive,
                    &queue_manager, &QueueManager::handle_modem_receive);
            
            connect(&driver.signal_data_request,
                    &queue_manager, &QueueManager::handle_modem_data_request);
        }
        
        /// binds the MAC initiate transmission callback to the driver and the driver parsed message callback to the MAC
        inline void bind(MACManager& mac, ModemDriverBase& driver)
        {
            connect(&mac.signal_initiate_transmission,
                    &driver, &ModemDriverBase::handle_initiate_transmission);
        }

        /// creates bindings for a RouteManager to control a particular queue (QueueManager)
        inline void bind(QueueManager& queue_manager, RouteManager& route_manager)
        {
            route_manager.add_subnet_queue(&queue_manager);
            connect(&queue_manager.signal_in_route, &route_manager, &RouteManager::handle_in);
            connect(&queue_manager.signal_out_route, &route_manager, &RouteManager::handle_out);
        }        

        /// bind all three (shortcut to calling the other three bind functions)
        inline void bind(ModemDriverBase& driver, QueueManager& queue_manager, MACManager& mac)
        {
            bind(driver, queue_manager);
            bind(mac, driver);
        }
        
        // examples
        /// \example acomms/chat/chat.cpp
        /// chat.proo
        /// \verbinclude chat.proto
        /// chat.cpp

    }

}

#endif

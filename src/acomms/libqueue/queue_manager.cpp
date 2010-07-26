// copyright 2009 t. schneider tes@mit.edu 
//
// this file is part of the Queue Library (libqueue),
// the goby-acomms message queue manager. goby-acomms is a collection of 
// libraries for acoustic underwater networking
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This software is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this software.  If not, see <http://www.gnu.org/licenses/>.

#include <map>
#include <deque>

#include <boost/foreach.hpp>

#include "goby/acomms/xml/xml_parser.h"
#include "goby/util/logger.h"
#include "goby/util/time.h"
#include "goby/util/binary.h"

#include "queue_constants.h"
#include "queue_manager.h"
#include "queue_xml_callbacks.h"

goby::acomms::QueueManager::QueueManager(std::ostream* os /* =0 */)
    : modem_id_(0),
      os_(os),
      packet_ack_(0)
{}
    
goby::acomms::QueueManager::QueueManager(const std::string& file, const std::string schema, std::ostream* os /* =0 */)
    : modem_id_(0),
      os_(os),
      packet_ack_(0)

{
    add_xml_queue_file(file, schema);
}
    
goby::acomms::QueueManager::QueueManager(const std::set<std::string>& files,
                                  const std::string schema, std::ostream* os /* =0 */)
    : modem_id_(0),
      os_(os),
      packet_ack_(0)
{
    BOOST_FOREACH(const std::string& s, files)
        add_xml_queue_file(s, schema);
}

goby::acomms::QueueManager::QueueManager(const QueueConfig& cfg, std::ostream* os /* =0 */)
    : modem_id_(0),
      os_(os),
      packet_ack_(0)
{
    add_queue(cfg);
}

goby::acomms::QueueManager::QueueManager(const std::set<QueueConfig>& cfgs, std::ostream* os /* =0 */)
    : modem_id_(0),
      os_(os),
      packet_ack_(0)
{
    BOOST_FOREACH(const QueueConfig& c, cfgs)
        add_queue(c);    
}

void goby::acomms::QueueManager::add_queue(const QueueConfig& cfg)
{
    QueueKey k(cfg.type(), cfg.id());

    Queue q(cfg, os_, modem_id_);
    
    if(queues_.count(k))
    {
        std::stringstream ss;
        ss << "Queue: duplicate key specified for key: " << k;
        throw std::runtime_error(ss.str());
    }
    else if(q.cfg().id() > MAX_ID && q.cfg().type() != queue_ccl)
    {
        std::stringstream ss;
        ss << "Queue: key (" << k << ") is too large for use with libqueue. Use a id smaller than " << MAX_ID;
        throw std::runtime_error(ss.str());
    }
    else
        queues_.insert(std::pair<QueueKey, Queue>(k, q));

    if(os_) *os_<< group("q_out") << "added new queue: \n" << q << std::endl;
    
}

void goby::acomms::QueueManager::add_xml_queue_file(const std::string& xml_file,
                                             const std::string xml_schema)
{
    std::vector<QueueConfig> cfgs;
    
    // Register handlers for XML parsing
    QueueContentHandler content(cfgs);
    QueueErrorHandler error;
    // instantiate a parser for the xml message files
    XMLParser parser(content, error);
    // parse(file, [schema])
    if(xml_schema != "")
        xml_schema_ = xml_schema;
        
    parser.parse(xml_file, xml_schema_);

    BOOST_FOREACH(const QueueConfig& c, cfgs)
        add_queue(c);
}

void goby::acomms::QueueManager::do_work()
{
    typedef std::pair<const QueueKey, Queue> P;
    for(std::map<QueueKey, Queue>::iterator it = queues_.begin(), n = queues_.end(); it != n; ++it)
    {
        std::vector<ModemMessage> expired_msgs = it->second.expire();
        if(callback_expire)
        {
            BOOST_FOREACH(const ModemMessage& m, expired_msgs)
                callback_expire(it->first, m);    
        }
    }
    
}

void goby::acomms::QueueManager::push_message(QueueKey key, ModemMessage& new_message)
{
    
    // message is to us, auto-loopback
    if(new_message.dest() == modem_id_)
    {
        if(os_) *os_<< group("q_out") << "outgoing message is for us: using loopback, not physical interface" << std::endl;
        
        receive_incoming_modem_data(new_message);
    }
    // we have a queue with this key, so push message for sending
    else if(queues_.count(key))
    {
        queues_[key].push_message(new_message);
        qsize(&queues_[key]);
    }
    else
    {
        std::stringstream ss;
        ss << "no queue for key: " << key;
        throw std::runtime_error(ss.str());
    }    
}

void goby::acomms::QueueManager::push_message(unsigned id, ModemMessage& new_message, QueueType type /* = dccl_queue */)
{ push_message(QueueKey(type, id), new_message); }

void goby::acomms::QueueManager::set_on_demand(QueueKey key)
{
    if(queues_.count(key))
        queues_[key].set_on_demand(true);
    else
    {
        std::stringstream ss;
        ss << "no queue for key: " << key;
        throw std::runtime_error(ss.str());
    }
}

void goby::acomms::QueueManager::set_on_demand(unsigned id, QueueType type /* = dccl_queue */)
{ set_on_demand(QueueKey(type, id)); }


std::string goby::acomms::QueueManager::summary() const
{
    std::string s;
    typedef std::pair<const QueueKey, Queue> P;
    BOOST_FOREACH(const P& p, queues_)
        s += p.second.summary();

    return s;
}

    
std::ostream& goby::acomms::operator<< (std::ostream& out, const QueueManager& d)
{
    out << d.summary();
    return out;
}

goby::acomms::ModemMessage goby::acomms::QueueManager::stitch(std::deque<ModemMessage>& in)
{
    ModemMessage out;
    out.set_ack(packet_ack_);    
    stitch_recursive(out.data_ref(), in); // returns stitched together version
    
    return out;
}

bool goby::acomms::QueueManager::stitch_recursive(std::string& data, std::deque<acomms::ModemMessage>& in)
{
    ModemMessage& message = in.front();
    bool is_last_user_frame = (in.size() == 1);
    
    if(message.empty())
        throw (std::runtime_error("empty message passed to stitch"));        
    
    DCCLHeaderDecoder head_decoder(message.data());

    // don't put the multimessage flag on the last user-frame
    head_decoder[head_multimessage_flag] =
        (!is_last_user_frame) ? true : false;
    head_decoder[head_broadcast_flag] =
        (message.dest() == BROADCAST_ID) ? true : false;
    
    std::string& new_data = message.data_ref();
    
    if(!is_last_user_frame)
    {
        std::string frame_size =
            util::number2hex_string(message.size()-DCCL_NUM_HEADER_BYTES);
        new_data.insert(DCCL_NUM_HEADER_NIBS, frame_size);
    }
    
    DCCLHeaderEncoder head_encoder(head_decoder.get());
    new_data.replace(0, head_encoder.get().size(), head_encoder.get());

    //remove ccl_id
    data += new_data.substr(1*NIBS_IN_BYTE);

    in.pop_front();

    if(is_last_user_frame)
    {
        data.insert(0, util::number2hex_string(DCCL_CCL_HEADER));
        return true;
    }
    else
        return stitch_recursive(data, in);
}
    

void goby::acomms::QueueManager::clear_packet()
{
    typedef std::pair<unsigned, Queue*> P;
    BOOST_FOREACH(const P& p, waiting_for_ack_)
        p.second->clear_ack_queue();
    
    waiting_for_ack_.clear();
    
    packet_ack_ = 0;
}

// finds and publishes outgoing data for the modem driver
// first query every Queue for its priority data using
// priority_values(priority, last_send_time)
// priority_values returns false if that object has no data to give
// (either no data at all, or in blackout interval) 
// thus, from all the priority values that return true, pick the one with the lowest
// priority value, or given a tie, pick the one with the oldest last_send_time
bool goby::acomms::QueueManager::provide_outgoing_modem_data(const ModemMessage& message_in, ModemMessage& message_out)
{    
    ModemMessage modified_message_in = message_in;
    if(modified_message_in.frame() == 1 || modified_message_in.frame() == 0)
        clear_packet();
    else // discipline remaining frames to the first frame ack value
        modified_message_in.set_ack(packet_ack_);


    // first (0th) user-frame
    Queue* winning_var = find_next_sender(modified_message_in, 0);

    // no data at all for this frame ... :(
    if(!winning_var)
    {
        message_out = ModemMessage();
        message_out.set_src(message_in.src());
        message_out.set_dest(message_in.dest());
        message_out.set_ack(packet_ack_);

        if(os_) *os_<< group("q_out") << "no data found. sending blank to firmware" 
                    << ": " << message_out.snip() << std::endl;
        
        return true;
    }    

    // keep filling up the frame with messages until we have nothing small enough to fit...
    std::deque<ModemMessage> user_frames;
    while(winning_var)
    {
        ModemMessage next_message = winning_var->give_data(modified_message_in.frame());
        user_frames.push_back(next_message);

        // if a destination has been set or ack been set, do not unset these
        if (packet_ack_ == false) packet_ack_ = next_message.ack();
        
        if(os_) *os_<< group("q_out") << "sending data to firmware from: "
                    << winning_var->cfg().name() 
                    << ": " << next_message.snip() << std::endl;
        
        if(!packet_ack_)
        {
            winning_var->pop_message(modified_message_in.frame());
            qsize(winning_var);
        }
        else
            waiting_for_ack_.insert(std::pair<unsigned, Queue*>(modified_message_in.frame(), winning_var));
        
        modified_message_in.set_size(modified_message_in.size() - next_message.size());

        // if there's no room for more, don't bother looking
        // also end if the message you have is a CCL message
        if(modified_message_in.size() > DCCL_NUM_HEADER_BYTES && winning_var->cfg().type() != queue_ccl)
            winning_var = find_next_sender(modified_message_in, user_frames.size());
        else
            break;
    }

    message_out = stitch(user_frames);
    message_out.set_src(message_in.src());
    message_out.set_dest(message_in.dest());
    return true;
}


goby::acomms::Queue* goby::acomms::QueueManager::find_next_sender(ModemMessage& message, unsigned user_frame_num)
{   
// competition between variable about who gets to send
    double winning_priority;
    boost::posix_time::ptime winning_last_send_time;

    Queue* winning_var = 0;
    
    if(os_) *os_<< group("priority") << "starting priority contest"
                << "... request: " << message.snip() << std::endl;
    
    for(std::map<QueueKey, Queue>::iterator it = queues_.begin(), n = queues_.end(); it != n; ++it)
    {
        Queue& oq = it->second;
        
        // encode on demand
        if(oq.on_demand() &&
           (!oq.size() || oq.newest_msg_time() + ON_DEMAND_SKEW < util::goby_time())
            )
        {
            if(callback_ondemand)
            {
                ModemMessage new_message;
                callback_ondemand(it->first, message, new_message);
                push_message(it->first, new_message);
            }
        }
        
        double priority;
        boost::posix_time::ptime last_send_time;
        if(oq.priority_values(priority, last_send_time, message))
        {
            // no winner, better winner, or equal & older winner
            // AND not CCL when not the first user-frame
            if((!winning_var || priority > winning_priority ||
                (priority == winning_priority && last_send_time < winning_last_send_time))
               && !(oq.cfg().type() == queue_ccl && user_frame_num > 0))
            {
                winning_priority = priority;
                winning_last_send_time = last_send_time;
                winning_var = &oq;
            }
            if(os_) *os_<< group("priority") << "\t" << oq.cfg().name()
                        << " has priority value"
                        << ": " << priority << std::endl;
        }
    }

    if(os_) *os_<< group("priority") << "\t"
                << "all other queues have no messages" << std::endl;

    if(winning_var)
    {
        if(os_) *os_<< group("priority") << winning_var->cfg().name()
                    << " has highest priority." << std::endl;
    }
    
    return winning_var;
}    


void goby::acomms::QueueManager::handle_modem_ack(const ModemMessage& message)
{
    unsigned dest = message.dest();
    if(dest != modem_id_)
    {
        if(os_) *os_<< group("q_in") << warn
                    << "ignoring ack for modem_id = " << dest << std::endl;
        return;
    }
    else if(!waiting_for_ack_.count(message.frame()))
    {
        if(os_) *os_<< group("q_in")
                    << "got ack but we were not expecting one" << std::endl;
        return;
    }
    else
    {
        
      // got an ack, let's pop this!
        if(os_) *os_<< group("q_in") << "received ack for this id" << std::endl;
        
        std::multimap<unsigned, Queue *>::iterator it = waiting_for_ack_.find(message.frame());
        while(it != waiting_for_ack_.end())
        {
            Queue* oq = it->second;
            
            ModemMessage removed_msg;
            if(!oq->pop_message_ack(message.frame(), removed_msg))
            {
                if(os_) *os_<< group("q_in") << warn
                            << "failed to pop message from "
                            << oq->cfg().name() << std::endl;
            }
            else
            {
                qsize(oq);
                if(callback_ack)
                    callback_ack(QueueKey(oq->cfg().type(), oq->cfg().id()), removed_msg);
            }
            
            waiting_for_ack_.erase(it);
            
            it = waiting_for_ack_.find(message.frame());
        }
    }
    
    return;    
}


// parses and publishes incoming data
// by matching the variableID field with the variable specified
// in a "receive = " line of the configuration file
void goby::acomms::QueueManager::receive_incoming_modem_data(const ModemMessage& message)
{
    if(os_) *os_<< group("q_in") << "received message"
                << ": " << message.snip() << std::endl;

    std::string data = message.data();
    if(data.size() < DCCL_NUM_HEADER_NIBS)
        return;
    
    DCCLHeaderDecoder head_decoder(data);
    int ccl_id = head_decoder[head_ccl_id];

    // check for queue_dccl type
    if(ccl_id == DCCL_CCL_HEADER)
    {
        ModemMessage mod_message = message;
        unstitch_recursive(data, mod_message);
    }
    // check for ccl type
    else
    {
        QueueKey key(queue_ccl, ccl_id);
        std::map<QueueKey, Queue>::iterator it = queues_.find(key);
        
        if (it != queues_.end())
        {
            if(callback_receive_ccl) callback_receive_ccl(key, message);
        }
        else
        {
            if(os_) *os_<< group("q_in") << warn << "incoming data string is not for us (not DCCL or known CCL)." << std::endl;
        }
    }
}

bool goby::acomms::QueueManager::unstitch_recursive(std::string& data, ModemMessage& message)
{
    unsigned original_dest = message.dest();
    DCCLHeaderDecoder head_decoder(data);
    bool multimessage_flag = head_decoder[head_multimessage_flag];
    bool broadcast_flag = head_decoder[head_broadcast_flag];
    unsigned dccl_id = head_decoder[head_dccl_id];
        
    // test multimessage bit
    if(multimessage_flag)
    {
        // extract frame_size
        unsigned frame_size;
        util::hex_string2number(data.substr(DCCL_NUM_HEADER_NIBS, NIBS_IN_BYTE), frame_size);
        
        // erase the frame size byte
        data.erase(DCCL_NUM_HEADER_NIBS, NIBS_IN_BYTE);
        
        // extract the data for this user-frame
        message.set_data(data.substr(0, (frame_size + DCCL_NUM_HEADER_BYTES)*NIBS_IN_BYTE));

        data.erase(1*NIBS_IN_BYTE, (frame_size + DCCL_NUM_HEADER_BYTES-1)*NIBS_IN_BYTE);
    }
    else
    {
        message.set_data(data);
    }
    
    // reset these flags
    head_decoder[head_multimessage_flag] = false;
    head_decoder[head_broadcast_flag] = false;
    
    DCCLHeaderEncoder head_encoder(head_decoder.get());
    message.data_ref().replace(0, DCCL_NUM_HEADER_NIBS,head_encoder.get());
    // overwrite destination as BROADCAST if broadcast bit is set
    message.set_dest(broadcast_flag ? BROADCAST_ID : original_dest);
    publish_incoming_piece(message, dccl_id);        
    
    // put the destination back
    message.set_dest(original_dest);
    
    if(!multimessage_flag)
        return true;    
    else
        return unstitch_recursive(data, message);
}

bool goby::acomms::QueueManager::publish_incoming_piece(ModemMessage message, const unsigned incoming_var_id)
{
    if(message.dest() != BROADCAST_ID && message.dest() != modem_id_)
    {
        if(os_) *os_<< group("q_in") << warn << "ignoring message for modem_id = "
                    << message.dest() << std::endl;
        return false;
    }

    QueueKey dccl_key(queue_dccl, incoming_var_id);
    std::map<QueueKey, Queue>::iterator it_dccl = queues_.find(dccl_key);
    
    if(it_dccl == queues_.end())
    {
        if(os_) *os_<< group("q_in") << warn << "no mapping for this variable ID: "
                    << incoming_var_id << std::endl;
        return false;
    }

    if(callback_receive) callback_receive(dccl_key, message);    

    return true;
}

int goby::acomms::QueueManager::request_next_destination(unsigned size /* = std::numeric_limits<unsigned>::max() */)
{
    clear_packet();

    ModemMessage message;
    message.set_size(size);
    
    Queue* winning_var = find_next_sender(message, 0);

    if(winning_var)
    {
        unsigned dest = winning_var->give_dest();
        if(os_) *os_ << group("q_out") <<  "got dest request for size " << size
                     << ", giving dest: " << dest << std::endl;
        return dest;
    }
    else
    {
        return NO_AVAILABLE_DESTINATION;
    }
    
}


void goby::acomms::QueueManager::add_flex_groups(util::FlexOstream& tout)
{
    tout.add_group("push", "+", "lt_cyan", "stack push - outgoing messages (goby_queue)");
    tout.add_group("pop", "-", "lt_green", "stack pop - outgoing messages (goby_queue)");
    tout.add_group("priority", "<", "yellow", "priority contest (goby_queue)");
    tout.add_group("q_out", "<", "cyan", "outgoing queuing messages (goby_queue)");
    tout.add_group("q_in", ">", "green", "incoming queuing messages (goby_queue)");
}


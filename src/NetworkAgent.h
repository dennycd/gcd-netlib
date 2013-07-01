//
//  Created by Denny C. Dai on 10-10-17.
//
#ifndef LIBGCDNET_ENGINE_NETWORK_ENGINE_H
#define LIBGCDNET_ENGINE_NETWORK_ENGINE_H

#include <iostream>
#include <exception>
#include <string>
#include <list>
#include <dispatch/dispatch.h>

namespace libgcdnet{

        struct NetworkAgentPackageHead
        {
            unsigned char protocol; //u8: 0xbb
            unsigned int payload_size; //u32
            unsigned int source_agent_id; //u32
            unsigned int target_agent_id; //u32
        };
        
        struct NetworkAgentPackage
        {
            struct NetworkAgentPackageHead header;
            //TODO: replace this with a more efficient circular bytes cache!
            std::string payload;
            
            int size() const
            {
                return sizeof(header) + payload.size();
            }
            
            //check if it is a complete protocol content
            bool complete() const
            {
                return header.payload_size == payload.size() && payload.size()> 0;
            }
            
            bool empty() const
            {
                return payload.size()==0;
            }
            
            void reset()
            {
                header.protocol = 0xbb;
                header.payload_size = 0;
                header.source_agent_id = header.target_agent_id = 0;
                payload = "";
            }
            
            NetworkAgentPackage()
            {
                reset();
            }
            
            void print() const
            {
                std::cout << 
                "[payload_size=" << header.payload_size <<
                ", source_agent_id=" << header.source_agent_id <<
                ", target_agent_id=" << header.target_agent_id << 
                ", current_payload_size=" << payload.size() <<
                "]" << std::endl;
            }
        };
        
        class NetworkAgentException : public std::exception
        {    
        public:
            NetworkAgentException(const std::string& msg = "") : message(msg){}
            ~NetworkAgentException() throw(){}
            const char* what() const throw()
            {
                return ("Engine NetworkAgent Exception: " + message).c_str();
            }
        private:
            std::string message;
        };

        /**
         Delegation Interface for A Single Client Agent Handler 
         **/
        class NetworkAgentClientSession;
        class NetworkAgentClientDelegate
        {
        public:
            virtual unsigned int agent_id() const = 0; //identify itself
            virtual void closed() = 0; //remote peer closed the comm
            virtual void connected(NetworkAgentClientSession* request) = 0;
            virtual void data_received() = 0;
            virtual void data_sent() = 0;
        };
        
        //interface for delegates that links 
        //a new connection session to an existing 
        //agent in the current system node
        class NetworkAgentDispatcherDelegate
        {
        public:
            //links the session to a target agent,return false if failed to link
            // virtual bool dispatch(unsigned int target_agent_id, NetworkAgentClientSession* session) = 0;
            //search if a givne target agent exist in the system
            //return handler to the agent if exist, otherwise NULL
            virtual NetworkAgentClientDelegate* search(unsigned int target_agent_id) const = 0;
        };
        
        
        class NetworkAgent;
        //the client agent request 
        class NetworkAgentClientSession
        {
            friend class NetworkAgent;
        protected:
            NetworkAgentPackage w_data; //write data cache
            NetworkAgentPackage r_data; //read data cache
            
            dispatch_queue_t queue; //client worker queue
            dispatch_source_t r_source; //read dispatch source
            
            dispatch_source_t w_source; //write dispatch source
            bool w_source_suspended; //suspension flag for write source   
            
            NetworkAgentClientDelegate* delegate; //delegate agent 
            NetworkAgentDispatcherDelegate *dispatcher; //dynamic lookup for target agent
            
        public:
            void setDispatcher(NetworkAgentDispatcherDelegate *d)
            {
                dispatch_async(queue, ^{
                    dispatcher = d;
                });
            }
            
            void setDelegate(NetworkAgentClientDelegate* d)
            {
                dispatch_async(queue, ^{
                    delegate = d;
                });
            }

            //blocking read of the buffer 
            //@assume the delegate received noti that data trunk has received
            void read_data(std::string& data)
            {
                __block std::string tmp;
                dispatch_sync(queue, ^{
                    tmp = r_data.payload;
                    r_data.reset();
                });
                data = tmp;
            }
            
            //async write of data 
            void write_data(unsigned int source_agent_id, 
                            unsigned int target_agent_id,
                            const std::string& data)
            {
                dispatch_async(queue, ^{
                    
                    //TODO: the w_data might contains some data yet to be written
                    //here we simply override previoys one
                    w_data.reset();
                    w_data.payload = data; 
                    w_data.header.payload_size = data.size();
                    w_data.header.source_agent_id = source_agent_id;
                    w_data.header.target_agent_id = target_agent_id;
                    
                    if(w_source_suspended)
                    {
                        w_source_suspended = false;
                        dispatch_resume(w_source);
                    }
                });
                
            }
            
            NetworkAgentClientSession()
            {
                w_source_suspended = true;
                delegate = NULL;
                dispatcher = NULL;
            }
            
            void cancel_all_sources()
            {
                if(w_source_suspended)
                    dispatch_resume(w_source);
                dispatch_source_cancel(w_source);
                dispatch_source_cancel(r_source);
            }
            
            ~NetworkAgentClientSession()
            {
                //asynchornously send out cacnel signal 
                //to all dispatch resource, release the resources,
                //and eventually realeasing the queue object iself 
                //dispatch_async(queue, ^{
                    //need to resume write source and then cancel it 
                    
                    dispatch_release(w_source);
                    dispatch_release(r_source);
               // });
                
            }
        };
    

        /**
         GCD Based Networking Agent 
         **/
        class NetworkAgent
        {
        public:
            
            //currently limited to this number in BSD spec
            static const int DEFAULT_MAX_SOCK_LISTEN_QUEUE = 128;
            enum Mode{
                SERVER,
                CLIENT
            };
            
            NetworkAgent(Mode mode, NetworkAgentDispatcherDelegate *d);
            ~NetworkAgent();
            
            
        public:
            //activate server agent's network listening on specified port
            void listen(const char *hostname, const char* servname) throw(NetworkAgentException); 
            void connect(const char *hostname, const char* servname) throw(NetworkAgentException);
            
        protected:
            void accept(int listen_sock) throw(NetworkAgentException);//accept a new client connection
            NetworkAgentClientSession* create_client_session(int client_sock) throw(NetworkAgentException);
            
            
        protected:
            static void client_worker_queue_write(struct NetworkAgentClientSession* req) throw(NetworkAgentException);
            static void client_worker_queue_read(struct NetworkAgentClientSession* req) throw(NetworkAgentException);
            static void client_worker_queue_finalizer(char* count);
            
            
        private:
            
            dispatch_queue_t _workqueue;//the worker queue
            Mode _mode; //sever/client mode
            //std::list<dispatch_source_t> _listeners;
            
             
            NetworkAgentDispatcherDelegate * _dispatcher; 
        };

}
#endif
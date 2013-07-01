//
//  NetworkAgent.cpp
//
//  Created by Denny C. Dai on 10-10-17.
//

#include "NetworkAgent.h"
#include <sstream>
#include <sys/socket.h> //socket() 
#include <netdb.h> // ICPROTO_TCP  addrinfo
#include <fcntl.h> // NON_BLOCKING I/O
#include <cassert>
#include <errno.h>

#include <cstring> //memcpy
/**
 Implementation of GCD-based TCP socket level networking
 check the DispatchWebServer sample code 
 http://developer.apple.com/library/mac/#samplecode/DispatchWebServer/Introduction/Intro.html
 and the DispatchSample 
 http://developer.apple.com/library/mac/#samplecode/Dispatch_Samples/Introduction/Intro.html
 
 **/
namespace libgcdnet {

        void NetworkAgent::client_worker_queue_finalizer(char* label)
        {
            //const char* label = dispatch_queue_get_label(dispatch_get_current_queue());
            std::cout << label << " exit." << std::endl;
            delete[] label;
        }
        
        //write all data from the req->w_data to client 
        //invoked from within the client worker queue whenever the underlying socket has space to write
        void NetworkAgent::client_worker_queue_write(NetworkAgentClientSession* req)
        throw(NetworkAgentException)
        {
            //skip if nothing to write 
            if(req->w_data.payload.size()==0)
                return;
            
            int client_sock = dispatch_source_get_handle(req->w_source);
            
            //TODO: should really be getting what ever data that needs to write 
            //copy whole data trunck to local buffer and clean up data cache
            std::cout << "w_data=" << req->w_data.payload << std::endl;
            
            //if first package to send, include header segment,
            //otherwise jsut send the payload
            int totalReadBytes;
            if(req->w_data.complete())
                totalReadBytes = req->w_data.size();
            else 
                totalReadBytes = req->w_data.payload.size();
            char *buf = (char*)malloc(totalReadBytes);
            
            
            //the first package to sent, compile protocol header into buf
            int pos = 0;
            if(req->w_data.complete())
            {
                memcpy(buf, (void*)&(req->w_data.header), sizeof(req->w_data.header));
                pos = sizeof(req->w_data.header);
            }
            
            memcpy(buf + pos, req->w_data.payload.c_str(), req->w_data.payload.size());
 
            //attemp to write all data until socket is flooded 
            ssize_t totalWriteBytes = 0, writeBytes;
            do{
                writeBytes = ::write(client_sock, buf + totalWriteBytes, totalReadBytes - totalWriteBytes);
                if(writeBytes < 0)
                    break;
                
                totalWriteBytes += writeBytes;
                
            }while(totalWriteBytes < totalReadBytes);
            
            std::cout << "total bytes written: " << totalWriteBytes << std::endl;
            
            //if not all bytes written, just get ride of the written part, and wait for next available write
            if(writeBytes < 0){
                std::cout << "warning writeBytes < 0" << std::endl;
                req->w_data.payload = "";
                req->w_data.payload.append(buf + totalWriteBytes, totalReadBytes - totalWriteBytes);
            }
            //all written, clean up the write cache
            //SUSPEND the write source so that its does not repeatedly firing write handler 
            //until the context write buffer has some new data 
            else
            {
                req->w_data.reset(); 
                
                if(!req->w_source_suspended)
                {
                    dispatch_suspend(req->w_source);
                    req->w_source_suspended = true;
                    
                    if(req->delegate)
                        req->delegate->data_sent();
                }
            }
                
            free(buf);
        }
        
        //client socket has available bytes to read
        //invoked from within a client socket working queue 
        //@param request - the associated request object 
        void NetworkAgent::client_worker_queue_read(struct NetworkAgentClientSession* req)
        throw(NetworkAgentException)
        {
            size_t estimated = dispatch_source_get_data(req->r_source);
            int client_sock = dispatch_source_get_handle(req->r_source);
            
            //if previously recieved a pakcage that is not prcossed 
            //by delegates, just clear it
            if(req->r_data.complete())
                req->r_data.reset();
            
            //dynamically allocate buffer 
            char* buf = new char[estimated+1];
            if(!buf)
                throw NetworkAgentException("heap memory allocation failed");
           
            bzero(buf, estimated + 1);
            //attemp to read estimated bytes from the client socket
            ssize_t actual = ::read(client_sock, buf, estimated);
            
            //some bytes are read
            if(actual > 0){
                
                //here we are receiving the very first package
                //which contains the header and a first segment of the payload
                //copy over the protocol header
                int pos = 0;
                if(req->r_data.empty()){
                    memcpy((void*)&(req->r_data.header), buf, sizeof(req->r_data.header));
                    pos = sizeof(req->r_data.header);
                    
                    //we allow dynamic linking between each package and the target agent here
                    //if either link not exist, or link changed, re-link again
                    if( !(req->delegate) || (req->delegate->agent_id() != req->r_data.header.target_agent_id))
                    {
                        //search for target agent, and link to the session delegate
                        if(req->dispatcher)
                        {
                            req->delegate = req->dispatcher->search(req->r_data.header.target_agent_id);
                        }
                    }
                    
                }
                
                //append payloads
                std::cout << "reading " << actual << " bytes " << std::endl;
                req->r_data.payload.append(buf+pos, actual-pos);
                std::cout << req->r_data.payload << std::endl;
                
                //if a complate package is received
                //and delegates exist, notify it !!
                if(req->delegate && req->r_data.complete())
                {
                    req->r_data.print();
                    req->delegate->data_received();
                }
                
            }
            //reading end of the source
            else
                if(actual == 0)
                {
                    //shall inform delegates that the peer has initiated a close 
                    //and that the session object will deconstruct itself afterwards
                    if(req->delegate)
                        req->delegate->closed(); 
                    
                    
                    //first we make sure no more new handler blocks are submitted beyond this point
                    req->cancel_all_sources(); 
                
                    //then we submit the deallocation taks to the same queue
                    //this ensures that any submitted read/write block in the queue still has a valid 
                    //session object to use
                    //and that there's absolutely no more read/write block after this delete block!!!
                    dispatch_async(req->queue, ^{
                        delete req;
                    });
                    
                }
            
            delete[] buf;
            
            if(actual < 0)
                throw NetworkAgentException("error reading socket");
            
        }
        
        //invoked within the worker queue 
        //triggered by the dispatch source (listener socket)'s event 
        //that a client has completed TCP handshake and ready for connection
        void NetworkAgent::accept(int listen_sock) throw(NetworkAgentException)
        {
            if(_mode!=SERVER)
                return;
           
            
            //prevent further accept(2)s across multiple sources
            dispatch_queue_t cur_queue = dispatch_get_current_queue();
            dispatch_retain(cur_queue);
            dispatch_suspend(cur_queue);  
            
            //accept the client connectio and assigns a socket for I/O
            struct sockaddr_in client_addr;
            socklen_t client_addr_size = sizeof(client_addr);
            int client_sock = ::accept(listen_sock, (struct sockaddr *)&client_addr, &client_addr_size);
            if(client_sock < 0)
                throw NetworkAgentException("failed to accept client socket connection");
            
            //create agent session
            NetworkAgentClientSession* new_session = create_client_session(client_sock);
            
            std::cout << "new session from peer " << std::endl;
            
            //there's nothing we can do at this stage
            //since no communication is sent from remote peer yet 
            //therefore there's no way of identify it's identify and intention
            /*
            //TODO: should I inform delegates that a client TCP connection is established
            //and that bindirectional I/O is available
            if(new_session->delegate)
                new_session->delegate->connected(new_session);
            */
            
            //test sending a hell string to client 
            //TODO: somehow the first character gets chuncated arbitrarily 
            new_session->write_data(12, 912, " Hello From Denny !!");
            
            //resume the worker queue
            dispatch_resume(cur_queue);
            dispatch_release(cur_queue);
        }
        
        
        void NetworkAgent::connect(const char *hostname, const char* servname) throw(NetworkAgentException)
        {
            if(_mode!=CLIENT)
                return;
            
            //retrieving a valid listening interface
            struct addrinfo hints, *aires0 = NULL;
            
            bzero(&hints, sizeof(hints));
            hints.ai_flags = AI_PASSIVE;//use bind() to bind addr
            hints.ai_family = PF_INET; // IPv4
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;
            
            int rc = getaddrinfo(hostname, servname, &hints, &aires0);
            if(rc)
                throw NetworkAgentException("error in getaddrinfo");
            if(!aires0)
                throw NetworkAgentException("no addr info available");
            
            //loop through all available network interface and listen to them all
            for(struct addrinfo* aires = aires0; aires; aires = aires->ai_next)
            {
                //create the raw socket
                int s = socket(aires->ai_family, aires->ai_socktype, aires->ai_protocol);
                if(s < 0)
                    throw NetworkAgentException("failed to create socket");
                
                //misc socket options
                int yes = 1;
                rc += setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
                rc += setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
                rc += setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
                if(rc)
                    throw NetworkAgentException("error in setsockopt");
                
                //try connection
                int ret = ::connect(s, aires->ai_addr, aires->ai_addrlen);
                if(ret < 0){
                    close(s);
                    throw NetworkAgentException("socket connect failed.");
                }
            
                //create agent session
                NetworkAgentClientSession* new_session = create_client_session(s);
                
                std::cout << "connected to remote peer " << hostname << ":" << servname << std::endl; 
                
                
                //TODO: should I inform delegates that a client TCP connection is established
                //and that bindirectional I/O is available
                if(new_session->delegate)
                    new_session->delegate->connected(new_session);
                
            }
            
            //free up addr info
            freeaddrinfo(aires0);
        }
        
        
        /**
         1. Create a dedicated client session worker queue 
         2. Create a read dispatch source over the socket, active it
         3. Create a write dispatch source over the socket, in suspended state
         **/
        NetworkAgentClientSession* NetworkAgent::create_client_session(int client_sock) throw(NetworkAgentException)
        {
            NetworkAgentClientSession *new_req = new NetworkAgentClientSession;
            new_req->dispatcher = _dispatcher;
            static int requestID = 0; //a unique request identifier
            
            try{
                
                                //setup non-blocking socket I/O
                fcntl(client_sock, F_SETFL, O_NONBLOCK);//avoid blocking read/write operation
                
                //assign a dispatch queue to handle the async socket read/write I/O 
                std::ostringstream oss; oss<< "com.dennycd.engine.network.session." << "req" << requestID++ << "#" << client_sock;
                dispatch_queue_t myqueue = dispatch_queue_create(oss.str().c_str(), NULL);
                if(!myqueue)
                    throw NetworkAgentException("failed to create dispach queue");
                else 
                    new_req->queue = myqueue;
                
                char* lable = new char[oss.str().length()];
                memcpy(lable, oss.str().c_str(), oss.str().length());
                dispatch_set_context(myqueue, lable);//let the queue know the request contetnt
                dispatch_set_finalizer_f(myqueue, (dispatch_function_t)client_worker_queue_finalizer);
                
                
                //create a dispatch source over the client socket for async I/O 
                dispatch_source_t read_source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, client_sock, 0, myqueue);
                if(!read_source)
                    throw NetworkAgentException("failed to create dispatch read source");
                else
                    new_req->r_source = read_source;
                
                dispatch_source_set_event_handler(read_source, ^{
                    try{
                        client_worker_queue_read(new_req);
                    }catch(NetworkAgentException e)
                    {
                        std::cerr << e.what() << std::endl;
                    }
                });
                dispatch_source_set_cancel_handler(read_source, ^{
                    int ret = close(client_sock); 
                    std::cout << "read source cancedl with code " << ret << std::endl;
                });
                
                //alwasy active the read dispatch source
                dispatch_resume(new_req->r_source);
                
                //create a write dispatch source on the client socket 
                dispatch_source_t write_source = dispatch_source_create(DISPATCH_SOURCE_TYPE_WRITE, client_sock, 0, myqueue);
                if(!write_source)
                    throw NetworkAgentException("failed to create dispatch write source");
                else 
                    new_req->w_source = write_source;
                
                dispatch_source_set_event_handler(write_source, ^{
                    try{
                        client_worker_queue_write(new_req);
                    }catch(NetworkAgentException e)
                    {
                        std::cerr << e.what() << std::endl;
                    }
                });
                dispatch_source_set_cancel_handler(write_source, ^{
                    int ret = close(client_sock);  //might close twice
                    std::cout << "write source cancedl with code " << ret << std::endl;
                });
                
                new_req->w_source_suspended = true; //by default, the write source is suspended until a write request comes from agents that resumes it 
                
                //dispatch source will retain the dispatch queue, we just release it from here
                dispatch_release(new_req->queue);
            }
            catch(NetworkAgentException e)
            {
                close(client_sock);
                delete new_req;
                throw e;
            }

            return new_req;
        }

        void NetworkAgent::listen(const char *hostname, const char* servname) throw(NetworkAgentException)
        {
            if(_mode!=SERVER)
                return;
            
            //retrieving a valid listening interface
            struct addrinfo hints, *aires0 = NULL;
            
            bzero(&hints, sizeof(hints));
            hints.ai_flags =  AI_PASSIVE;//use bind() to bind addr
            hints.ai_family = PF_INET; // IPv4
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;
            
            int rc = getaddrinfo(hostname, servname, &hints, &aires0);
            if(rc)
                throw NetworkAgentException("error in getaddrinfo");
            if(!aires0)
                throw NetworkAgentException("no addr info available");
            
            //loop through all available network interface and listen to them all
            for(struct addrinfo* aires = aires0; aires; aires = aires->ai_next)
            {
                
                //create the listening socket
                int s = socket(aires->ai_family, aires->ai_socktype, aires->ai_protocol);
                if(s < 0)
                    throw NetworkAgentException("failed to create listening socket");
                
                //misc socket options
                int yes = 1;
                rc += setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
                rc += setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
                rc += setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
                if(rc)
                    throw NetworkAgentException("error in setsockopt");
                
                //binding listen socket to a valid interface 
                rc = bind(s, aires->ai_addr, aires->ai_addr->sa_len);
                if(rc < 0){
                    close(s);
                    throw NetworkAgentException("error in bind()");
                }
                
                //start listening for client agent connections 
                rc = ::listen(s,DEFAULT_MAX_SOCK_LISTEN_QUEUE);
                if(rc)
                    throw NetworkAgentException("error in listen");
                
                std::cout << "listening on port " << servname << std::endl;
                
                //create dispatch source over the listening socket
                //whenever an incoming connection appears, it triggers the accept function
                //on this agent's worker queue (dispatch sources from multiple network interfaces )
                // will aggreate events into this worker queue
                dispatch_source_t listener_ds = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, s, 0, _workqueue);
                
                if(!listener_ds)
                    throw NetworkAgentException("failed to create dispatch source");
                
                //hold record for the listenre 
                //WARNING: this somehow breaks the whole listener source ???
                //_listeners.push_back(listener_ds);
                
                //read event handler
                dispatch_source_set_event_handler(listener_ds, ^{ 
                    try{
                        this->accept(s); //s will capture the socket descriptor value for each accept
                    }catch(NetworkAgentException e)
                    {
                        std::cerr << e.what() << std::endl;
                    }
                    
                });
                
                //cancel handler (close listern socket)
                dispatch_source_set_cancel_handler(listener_ds, ^{
                    close(s);
                });
                
                //active the source and begin processing events
                dispatch_resume(listener_ds); 

            }
            
            //free up addr info
            freeaddrinfo(aires0);
        }
        
        
        NetworkAgent::NetworkAgent(Mode mode, NetworkAgentDispatcherDelegate *d) 
        : _mode(mode), _dispatcher(d)
        {
            _workqueue = dispatch_queue_create("com.dennycd.engine.NetworkAgent", NULL);
        }
        
        NetworkAgent::~NetworkAgent()
        {
            
            //cancel all listeners before deconstruct the queue itself and destroy the agent
            //after exectuion, no further event handler are executed,
            //but it does not interrupt a handler that is being executed 
            //but since we dispatch sync to the same linear worker queue, this block must  
            //have been executed AFTER some existing event handler, therefore it is safe !
            //in other words, the NetworkAgent object is never destroyed in accept() function !!
            /*
            dispatch_sync(_workqueue, ^{
                
                //asyncly cancel all listern sources and release the reference count
                std::list<dispatch_source_t>::const_iterator me;
                for(me=_listeners.begin(); me!=_listeners.end();me++){
                    dispatch_source_cancel(*me);
                    dispatch_release(*me);
                }
                
            });
            */
            
            dispatch_release(_workqueue);
        }
        
}
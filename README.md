## A C++ Networking Library Built on Libdispatch GCD

A TCP socket networking library built on top of Apple's grand central dispatch (libdispatch). 
 * internal serial dispatch queue for IO processing
 * listener and delegate callback using blocks
 * non-blocking socket I/O

The library is cross-platform with build system available for Mac OS, Linux, Node.js and iOS.

### Usage

```cpp
        
const char* hostname = "localhost";
const char* servname = "8888";

libgcdnet::NetworkAgent server(NetworkAgent::SERVER, NULL); 
server.listen(NULL, servname);

libgcdnet::NetworkAgent client(NetworkAgent::CLIENT,NULL); 
client.connect(hostname, servname);

```

## Author 
Denny C. Dai <dennycd@me.com> or visit <http://dennycd.me>

## License 
[MIT License](http://opensource.org/licenses/MIT)

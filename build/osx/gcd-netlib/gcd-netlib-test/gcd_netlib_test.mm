//
//  gcd_netlib_test.m
//  gcd-netlib-test
//
//  Created by Denny C. Dai on 2013-06-30.
//  Copyright (c) 2013 Denny C. Dai. All rights reserved.
//

#import <XCTest/XCTest.h>
#import "NetworkAgent.h"



@interface gcd_netlib_test : XCTestCase

@end

@implementation gcd_netlib_test

- (void)setUp
{
    [super setUp];
    
    // Set-up code here.
}

- (void)tearDown
{
    // Tear-down code here.
    
    [super tearDown];
}

- (void)testTCPConn
{
    using namespace libgcdnet;
    
    try{
        
        
         const char* hostname = "localhost";
         const char* servname = "8888";
         
         NetworkAgent server(NetworkAgent::SERVER, NULL); server.listen(NULL, servname);
         NetworkAgent client(NetworkAgent::CLIENT,NULL); client.connect(hostname, servname);
       
        dispatch_main();
    }
    catch(NetworkAgentException e)
    {
        std::cout << e.what() << std::endl;
    }
    
    
}

@end

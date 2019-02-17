/*
 * Copyright (c) 2016, Peter Thorson. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the WebSocket++ Project nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL PETER THORSON BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>

#include <iostream>

typedef websocketpp::client<websocketpp::config::asio_tls_client> client;
typedef std::shared_ptr<boost::asio::ssl::context> context_ptr;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

// pull out the type of messages sent by our config
typedef websocketpp::config::asio_client::message_type::ptr message_ptr;


/*
 * Simple WSS client class, single threaded
 */
 
class simpleWSSClient {
public:
    typedef websocketpp::client<websocketpp::config::asio_tls_client> client;
    typedef websocketpp::lib::lock_guard<websocketpp::lib::mutex> scoped_lock;

    // Constructor --------------------------------------------------------------------------
    simpleWSSClient() : m_open(false),m_done(false) {
		

	}

	// --------------------------------------------------------------------------

    // This method will block until the connection is complete
    void run(const std::string & uri) {
        // Create a new connection to the given URI
        websocketpp::lib::error_code 	ec;
        client::connection_ptr 			con;
        bool							end=false;	
        

        // Initialize the Asio transport policy
        m_client.init_asio();
        
        // Initialize SSL
        m_client.set_tls_init_handler(bind(&simpleWSSClient::on_tls_init, this, ::_1));
        
        // VERY important to reinit in case of failure
        m_client.start_perpetual();
        
        while (!end)	{
			
		  try {
			// Set logging to be pretty verbose (everything except message payloads)
			
			// set up access channels to only log interesting things
			m_client.clear_access_channels(websocketpp::log::alevel::all);
			m_client.set_access_channels(websocketpp::log::alevel::connect);
			m_client.set_access_channels(websocketpp::log::alevel::disconnect);
			m_client.set_access_channels(websocketpp::log::alevel::app);
				
			// 7 seconds ping - pong timeout	
			m_client.set_pong_timeout(7000);
        
			// event handlers
			m_client.set_open_handler(bind(&simpleWSSClient::on_open,this,_1));
			m_client.set_close_handler(bind(&simpleWSSClient::on_close,this,_1));
			m_client.set_fail_handler(bind(&simpleWSSClient::on_fail,this,_1));
			m_client.set_pong_timeout_handler(bind(&simpleWSSClient::on_pongTimeout,this,_1));
			m_client.set_pong_handler(bind(&simpleWSSClient::on_pong,this,_1));
			m_client.set_message_handler(bind(&simpleWSSClient::on_message, this, ::_1, ::_2));
        
			}   catch (websocketpp::exception const & e) {
			std::cout << e.what() << std::endl;
			}
		
			con = m_client.get_connection(uri, ec);
			if (ec) {
				m_client.get_alog().write(websocketpp::log::alevel::app,   "Get Connection Error: "+ec.message());
				return;
			}

			// Grab a handle for this connection so we can talk to it in a thread
			// safe manor after the event loop starts.
			m_hdl = con->get_handle();

			// Queue the connection. No DNS queries or network connections will be
			// made until the io_service event loop is run.
			m_client.connect(con);

			// Set the initial timer for periodic PINGs
			set_timer();
			
			// Start the ASIO io_service run loop
			try {
				m_client.run();
			} catch (websocketpp::exception const & e) {
				std::cout << e.what() << std::endl;
			}
			
			std::cout << "Ending RUN by exception !!!!!!" << std::endl;
			sleep(1);

			std::cout << "> Closing connection " << std::endl;
			
			websocketpp::lib::error_code ec;
			websocketpp::close::status::value code(websocketpp::close::status::normal);
				
			if (con->get_state() == websocketpp::session::state::open)
				m_client.close(con->get_handle(), code, "");
			con.reset(); //reset connection!	
				
			if (ec) {
				std::cout << "> Error closing connection "  << ec.message() << std::endl;
			}			
		}	
    }
    
	// --------------------------------------------------------------------------
   	// establishes a SSL connection

	context_ptr on_tls_init(websocketpp::connection_hdl hdl) {
		context_ptr ctx = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);

		try {
			ctx->set_options(boost::asio::ssl::context::default_workarounds |
							 boost::asio::ssl::context::no_sslv2 |
							 boost::asio::ssl::context::no_sslv3 |
							 boost::asio::ssl::context::single_dh_use);
		} catch (std::exception &e) {
			std::cout << "Error in context pointer: " << e.what() << std::endl;
		}
		return ctx;
	}

    // --------------------------------------------------------------------------
    // The open handler will signal that we are ready to work
    void on_open(websocketpp::connection_hdl hdl) {
        m_client.get_alog().write(websocketpp::log::alevel::app,
            "Connection opened !!!");

		// here we will subscribe  to topic
		std::string myMsg="{  \"event\": \"subscribe\",   \"channel\": \"ticker\",   \"symbol\": \"tBTCUSD\" }";
		m_client.send(hdl, myMsg.c_str(), websocketpp::frame::opcode::text);
		m_client.get_alog().write(websocketpp::log::alevel::app, "Sent Message: " + myMsg);

        scoped_lock guard(m_lock);
        m_open = true;
    }

    // --------------------------------------------------------------------------
    // The close handler will signal that we should stop working
    void on_close(websocketpp::connection_hdl hdl) {
        m_client.get_alog().write(websocketpp::log::alevel::app,
            "Connection closed !!!");

        scoped_lock guard(m_lock);
        m_done = true;
        m_open = false;
    }

    // --------------------------------------------------------------------------
	// message received
    void on_message(websocketpp::connection_hdl hdl, message_ptr msg) {
     std::cout << " **** ON_MESSAGE *** called with hdl: " << hdl.lock().get()
              << " and message: " << msg->get_payload()   << std::endl;

    }

    // --------------------------------------------------------------------------

    // pong handler
    void on_pong(websocketpp::connection_hdl hdl) {
        m_client.get_alog().write(websocketpp::log::alevel::app,
            "PONG (Websocket level) Received !!!!");

        scoped_lock guard(m_lock);
        m_done = true;
		m_open = false;
		}    
    // --------------------------------------------------------------------------

    // The pong timeout handler will signal that we should stop working
    void on_pongTimeout(websocketpp::connection_hdl hdl) {
        m_client.get_alog().write(websocketpp::log::alevel::app,
            "PONG (Websocket level) Timeout !!!!");

        scoped_lock guard(m_lock);
        m_done = true;
		m_open = false;
		}
    // --------------------------------------------------------------------------

    // The fail handler will signal that we should stop working
    void on_fail(websocketpp::connection_hdl hdl) {
        m_client.get_alog().write(websocketpp::log::alevel::app,
            "Connection failed !!!!");

        scoped_lock guard(m_lock);
        m_done = true;
		m_open = false;
		}
		
	// --------------------------------------------------------------------------

	// trigger timer every 3 seconds, for PING
    void set_timer() {
        m_timer = m_client.set_timer(
            3000,
            websocketpp::lib::bind(
                &simpleWSSClient::on_timer,
                this,
                websocketpp::lib::placeholders::_1
            )
        );
    }
    
	// --------------------------------------------------------------------------

    void on_timer(websocketpp::lib::error_code const & ec) {
        if (ec) {
            // there was an error, stop telemetry
            m_client.get_alog().write(websocketpp::log::alevel::app,
                    "Timer Error: "+ec.message());
            return;
        }
        
        m_client.get_alog().write(websocketpp::log::alevel::app,
            "TIMER triggered !!!!");
        
		std::stringstream ss;
		ss << "{\"event\":\"ping\",\"cid\": " 
           << std::to_string(time(NULL))  << "}\r\n";

		std::string myMsg = ss.str();

		// send LOW LEVEL (websocket) PING for connection testing!    
		m_client.ping(m_hdl, myMsg);
		m_client.get_alog().write(websocketpp::log::alevel::app, "Websocket level PING Sent !");    

        // send HIGH LEVEL (application) PING for connection testing!    
		m_client.send(m_hdl, myMsg.c_str(), websocketpp::frame::opcode::text);
		m_client.get_alog().write(websocketpp::log::alevel::app, "Sent Message: " + myMsg);    
        
        // set timer for next telemetry check
        set_timer();
    }

	// --------------------------------------------------------------------------


private:
    client 							m_client;
    websocketpp::connection_hdl 	m_hdl;
    websocketpp::lib::mutex 		m_lock;
	client::timer_ptr 				m_timer;
    bool 							m_open;
    bool 							m_done;
};

// --------------------------------------------------------------------------
// --------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    simpleWSSClient c;    // Create a client endpoint

    std::string uri = "wss://api.bitfinex.com:443/ws/2";

    if (argc == 2) {
        uri = argv[1];
    }

    c.run(uri);
}

// --------------------------------------------------------------------------











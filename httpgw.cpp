#include "swift.h"

using namespace swift;

#define HTTPGW_MAX_CLIENT 128

enum {
    HTTPGW_RANGE=0,
    HTTPGW_MAX_HEADER=1
};
const char * HTTPGW_HEADERS[HTTPGW_MAX_HEADER] = {
    "Range"
};


struct http_gw_t {
    int      id;
    uint64_t offset;
    uint64_t tosend;
    int      transfer;
    SOCKET   sink;
    char*    headers[HTTPGW_MAX_HEADER];
} http_requests[HTTPGW_MAX_CLIENT];


int http_gw_reqs_open = 0;
int http_gw_reqs_count = 0;

void HttpGwNewRequestCallback (SOCKET http_conn);
void HttpGwNewRequestCallback (SOCKET http_conn);

http_gw_t* HttpGwFindRequest (SOCKET sock) {
    for(int i=0; i<http_gw_reqs_open; i++)
        if (http_requests[i].sink==sock)
            return http_requests+i;
    return NULL;
}


void HttpGwCloseConnection (SOCKET sock) {
    http_gw_t* req = HttpGwFindRequest(sock);
    if (req) {
        dprintf("%s @%i closed http connection %i\n",tintstr(),req->id,sock);
        for(int i=0; i<HTTPGW_MAX_HEADER; i++) {
            if (req->headers[i]) {
                free(req->headers[i]);
                req->headers[i] = NULL;
            }
        }
        *req = http_requests[--http_gw_reqs_open];
    }
    swift::close_socket(sock);
    swift::Datagram::Listen3rdPartySocket(sckrwecb_t(sock));
}


void HttpGwMayWriteCallback (SOCKET sink) {
    http_gw_t* req = HttpGwFindRequest(sink);
    /* The number of bytes completed sequentially, i.e. from the beginning of
    the file, uninterrupted. */
    // FIXME Replace this method by another one that returns true if the byte range is in the buffer
    uint64_t complete = swift::SeqComplete(req->transfer);
    // Check if there is still bytes to send
    if (req->tosend && (req->offset < complete)) { // send data
        char buf[1<<12];	// FIXME 4096. Increase this value?
        uint64_t endRange = req->offset+req->tosend;
        // Send at most 2<<12 (4096) bytes
        uint64_t tosend = (endRange <= complete) ? std::min((uint64_t)1<<12, req->tosend): std::min((uint64_t)1<<12, complete - req->offset);
        size_t rd = pread(req->transfer,buf,tosend,req->offset); // FIXME hope it is cached.
        if (rd<0) {
            HttpGwCloseConnection(sink);
            return;
        }
        int wn = send(sink, buf, rd, 0);
        if (wn<0) {
        	/* Probably because the user agent (browser) closed the connection due to
        	 *  it got enough info to request another byte range request.
        	 *  (ex. The browser requests the entire file but when it receives just the metadata
        	 *   at the beginning of the file, it closes the ongoing connection and requests a specific request)
        	 */
            print_error("send fails");
            HttpGwCloseConnection(sink);
            return;
        }
        dprintf("%s @%i sent %ib\n",tintstr(),req->id,(int)wn);
        req->offset += wn;
        req->tosend -= wn;
    } else if (req->offset==complete) {
		dprintf("%s @%i done\n",tintstr(),req->id);
		// Close connection if it reaches EOF
		sckrwecb_t wait_new_req(req->sink,NULL,NULL,NULL);
		HttpGwCloseConnection(sink);
		swift::Datagram::Listen3rdPartySocket (wait_new_req);
	} else { // wait for data
		dprintf("%s @%i waiting for data\n",tintstr(),req->id);
		sckrwecb_t wait_swift_data(req->sink,NULL,NULL,HttpGwCloseConnection);
		swift::Datagram::Listen3rdPartySocket(wait_swift_data);
    }
}


void HttpGwSwiftProgressCallback (int transfer, bin64_t bin) {
    dprintf("%s @A pcb: %s\n",tintstr(),bin.str());
    for (int httpc=0; httpc<http_gw_reqs_open; httpc++) {
        if (http_requests[httpc].transfer==transfer) {
           // if ( (bin.base_offset()<<10) <= http_requests[httpc].offset &&
           //       ((bin.base_offset()+bin.width())<<10) > http_requests[httpc].offset  ) {
           //     dprintf("%s @%i progress: %s\n",tintstr(),http_requests[httpc].id,bin.str());
                sckrwecb_t maywrite_callbacks
                        (http_requests[httpc].sink,NULL,
                         HttpGwMayWriteCallback,HttpGwCloseConnection);
                Datagram::Listen3rdPartySocket (maywrite_callbacks);
           // }
        }
    }
}


void HttpGwFirstProgressCallback (int transfer, bin64_t bin) {
    if (bin!=bin64_t(0,0)) // need the first packet
        return;
    swift::RemoveProgressCallback(transfer,&HttpGwFirstProgressCallback);
    swift::AddProgressCallback(transfer,&HttpGwSwiftProgressCallback,0);
    for (int httpc=0; httpc<http_gw_reqs_open; httpc++) {
        http_gw_t * req = http_requests + httpc;
		if (req->transfer==transfer) {
			uint64_t file_size = swift::Size(transfer);
			char response[1024];

            // Parse parameters of byte range
            if (req->headers[0]) {

            	int end = file_size - 1, params = 0;

            	// Scan the http range header
				params = sscanf(req->headers[0],"bytes=%d-%d",(int*)&req->offset,&end);
				if(params == 0){
					HttpGwCloseConnection(req->sink);
					return;
				}

				// FIXME Check the equal condition for some cases
				if((int)req->offset <= end){
					req->tosend = end - req->offset + 1;
				}

				/*
				 * Optimistic approach: if the user agent (browser) requests the entire file, send the content
				 *  hoping the browser would close the connection, when parsing metadata
				 *  (usually at the beginning of the file). Then, the browser should request a specific range byte request.
				 *  It seems to work in Firefox and Opera. Partially in Chrome depending on the how the codec
				 *  is used.
				 *
				 * Pessimistic approach (commented): if the browser requests the entire file,
				 *  send and initial range from the beginning. Then, the browser should request a range byte request.
				 */
				/*
				if(req->tosend == file_size){
					req->tosend = 20480;	// FIXME Magic number for metadata?
					req->offset = 0;
				}
				*/

				// HTTP Partial Content
				//time_t rawtime;
				//time(&rawtime);
				sprintf(response,
					"HTTP/1.1 206 Partial Content\r\n"\
					"Server: Swift-httpgw\r\n"\
					"ETag: 204f6-bba79a-39256b00\r\n"\
					"Accept-Ranges: bytes\r\n"\
					"Content-Length: %lli\r\n"\
					"Content-Range: bytes %lli-%lli/%lli\r\n"\
					"Connection: Keep-Alive\r\n"\
					/* FIXME Content-Type should be calculated (parsing the content or as an URL param */
					"Content-Type: video/webm\r\n"\
					"\r\n",
					/* FIXME file_size is not the real size of the object */
					req->tosend, // (req->offset == 0) ? file_size : req->tosend,
					req->offset,
					req->offset + req->tosend -1, // (req->offset == 0) ? file_size-1 : req->offset + req->tosend -1,
					file_size);

			} else {
				// The browser does not send a range http header. Request entire file
				req->tosend = file_size;
            	sprintf(response,
					"HTTP/1.1 200 OK\r\n"\
					"Server: Swift-httpgw\r\n"\
					"Accept-Ranges: bytes\r\n"\
					"Connection: keep-alive\r\n"\
					"Content-Type: video/webm\r\n"\
					"Content-Length: %lli\r\n"\
					"\r\n", req->tosend);
			}

			dprintf("%s", response);
            send(req->sink,response,strlen(response),0);
			dprintf("%s @%i headers_sent size %lli\n",tintstr(),req->id,req->tosend);
        }
    }
	HttpGwSwiftProgressCallback(transfer,bin);
}


// Dispatch a request to get/set attributes from http
void HttpGwInfoRequest (int transfer, char* attrR) {

    for (int httpc=0; httpc<http_gw_reqs_open; httpc++) {
        http_gw_t * req = http_requests + httpc;
        // Check if the socket belongs to the current info request.
        // There could be different requests through the same socket concurrently
		if ((req->transfer==transfer) &&
				(req->headers[0] == attrR)) {
			// Restore header for request
			req->headers[0] = NULL;
			// Point to request msg
			char * attr = &attrR[1];

        	Sha1Hash root_hash = FileTransfer::file(transfer)->root_hash();

        	swift::PeerList* peerList;
        	swift::PeerList::iterator it;
        	// Call the API to retrieve the list of peers
        	peerList = swift::GetPeers(root_hash);

        	// Declare the content for the HTTP response
        	char* content = NULL;
        	size_t contentSize = 0;

        	char callback[100];

			// Parse the attribute
			if(strncmp(attr,"info&", 5) == 0){
				// Getter: return the protocol version and the list of peers
				//			for the ongoing stream "ip:port,ip2:port2"

				// MaxListSize id the #channels in the file transfer
				int maxListSize = FileTransfer::file(transfer)->channel_count() * 32;
				char peerListChar[maxListSize];
				strcpy(peerListChar, "\0");

				// FIXME Write a better JSON wrapper
				strcat(peerListChar,"\x5B");
				for (it = peerList->begin(); it < peerList->end(); it++){
					// Add a new peer in the string
					if (it != peerList->begin()) {
						strcat(peerListChar,",");
					}
					strcat(peerListChar, "\"");
					strcat(peerListChar,it->str());
					strcat(peerListChar, "\"");
				}
				strcat(peerListChar,"\x5D");

				// If a callback is given, wrap the list in a function
				if (strncmp(attr,"info&callback=",14) == 0){
					sprintf(callback, "%s({\"protocol\": %s, \"info\": %s});",
							&attr[14], "\"swift\"", peerListChar);
					content = callback;
				} else {
					content = peerListChar;
				}
				dprintf("info: %s\n", content);
				contentSize = strlen(content);
				//contentSize = strlen(peerListChar);

			} else if ((strlen(attr)>5) && strncmp(attr,"info=",5) == 0) {
				// Setter: update the given peers on the file transfer

				swift::PeerList* newPeerList = new PeerList;

				char *peer = strtok(&attr[5],",");
				// Declare the IP parse parameters
				const char* format = "%15[0-9.]:%5[0-9]";
				// FIXME only for IPv4
				char ip[16] = { 0 };
				char portCh[6] = { 0 };
				uint16_t port = 0;
				// Parse the peer address in string format
				while (peer){
					bool added = false;
					// Check if the peers is already added
					for (it = peerList->begin(); it < peerList->end(); it++){
						if (strcmp(it->str(), peer) == 0) {
							added = true;
							dprintf("Peer %s already on the peer list\n", peer);
						}
					}
					if(!added && sscanf(peer, format, ip, portCh) == 2) {
						// Add the peer on the list
						port = atoi(portCh);
						Address* addr = new Address(ip, port);
						newPeerList->push_back(*addr);
						dprintf("%s New peer from http gw: %s:%d\n", tintstr(), ip, port);
					}
					peer = strtok(NULL,",");
				}
				if (!newPeerList->empty()) {
					swift::AddPeers(newPeerList, root_hash);
				}

				// Ack http response
				sprintf(callback, "console.log(\"info ok\")");
				content = callback;
				contentSize = strlen(content);
			}

			// HTTP response header
			char response[1024];
			sprintf(response,
					"HTTP/1.1 200 OK\r\n"\
					"Connection: keep-alive\r\n"\
					"Content-Type: application/javascript\r\n"\
					"\r\n");
			dprintf("%s", response);
			send(req->sink,response,strlen(response),0);

			// HTTP info content
			// FIXME Resource temporally unavailable appears rarely
			if (contentSize > 0) {
				int wn = send(req->sink, content, contentSize, 0);
				if (wn<0) {
					print_error("send fails");
					HttpGwCloseConnection(req->sink);
					return;
				}
			}

			// FIXME Check if Listen3rdPartySocket is needed at the end of this method here
			HttpGwCloseConnection(req->sink);
			return;
        }
    }
}

void HttpGwNewRequestCallback (SOCKET http_conn){

	// Add a new request if it is not already in use
	http_gw_t* req = HttpGwFindRequest(http_conn);
	if(!req){
	    req = http_requests + http_gw_reqs_open++;
	    req->id = ++http_gw_reqs_count;
	    req->sink = http_conn;
	    req->offset = 0;
	    req->tosend = 0;
	    for(int i=0; i<HTTPGW_MAX_HEADER; i++) {
			req->headers[i] = NULL;
	    }
	    dprintf("%s @%i new http request\n",tintstr(),req->id);
	}
    // read headers - the thrilling part
    // we surely do not support pipelining => one request at a time
    #define HTTPGW_MAX_REQ_SIZE 1024
    char buf[HTTPGW_MAX_REQ_SIZE+1];
    int rd = recv(http_conn,buf,HTTPGW_MAX_REQ_SIZE,0);
    if (rd<=0) { // if conn is closed by the peer or no more requests, rd==0
        //sckrwecb_t wait_new_req(http_conn,NULL,NULL,NULL);
		HttpGwCloseConnection(http_conn);
		//swift::Datagram::Listen3rdPartySocket (wait_new_req);
        return;
    }
    buf[rd] = 0;

    // Check HTTP request
    dprintf("%s\n", buf);

    // HTTP request line
    char* reqline = strtok(buf,"\r\n");
    char method[16], url[512], version[16], crlf[5];
    if (3!=sscanf(reqline,"%16s %512s %16s",method,url,version)) {
        HttpGwCloseConnection(http_conn);
        return;
    }

    // HTTP header fields
    char* headerline;
    while (headerline=strtok(NULL,"\r\n")) {
        char header[128], value[256];
        if (2!=sscanf(headerline,"%120[^: ]: %250[^\r\n]",header,value)) {
            HttpGwCloseConnection(http_conn);
            return;
        }

        // Copy the header in req
        for(int i=0; i<HTTPGW_MAX_HEADER; i++) {
            if (0==strcasecmp(HTTPGW_HEADERS[i],header)){ // && !req->headers[i]) {
                req->headers[i] = strdup(value);
            }
        }
    }

    // Check if there is an additional attribute in the URL
	char * attr = strchr(url, '?'); //(url[41]=='?') ? &url[42] : NULL;

    // parse URL
     char * hashch=strtok(url,"/"), hash[41];
     while (hashch && (1!=sscanf(hashch,"%40[0123456789abcdefABCDEF]",hash) || strlen(hash)!=40))
         hashch = strtok(NULL,"/");
     if (strlen(hash)!=40) {
         HttpGwCloseConnection(http_conn);
         return;
     }

     Sha1Hash root_hash = Sha1Hash(true,hash);

     int file = swift::Find(root_hash);
     // FIXME If an attribute is given, the file transfer should be initiated
     if (file==-1 && !attr)
         file = swift::Open(hash,root_hash);
     req->transfer = file;

	if(attr){
		// The request is not interested in content, only metadata
		if(file!=-1){
			// Detect request info in header when parsing at InfoRequest
			req->headers[0] = attr;
			HttpGwInfoRequest(file, attr);
		} else {
			// Attributes are only valid for ongoing file transfers
			HttpGwCloseConnection(http_conn);
			return;
		}
	}else{
		// Perform the range request
		dprintf("%s @%i demands %s\n",tintstr(),req->id,hash);
	    if (swift::Size(file)) {
	        HttpGwFirstProgressCallback(file,bin64_t(0,0));
	    } else {
	        swift::AddProgressCallback(file,&HttpGwFirstProgressCallback,0);
	        sckrwecb_t install (http_conn,NULL,NULL,HttpGwCloseConnection);
	        swift::Datagram::Listen3rdPartySocket(install);
	    }
	}
}


// be liberal in what you do, be conservative in what you accept
void HttpGwNewConnectionCallback (SOCKET serv) {
    Address client_address;
    socklen_t len;
    // FIXME allow http persistent connections
    SOCKET conn = accept (serv, (sockaddr*) & (client_address.addr), &len);
    if (conn==INVALID_SOCKET) {
        print_error("client conn fails");
        return;
    }
    make_socket_nonblocking(conn);
    // submit 3rd party socket to the swift loop
    sckrwecb_t install
        (conn,HttpGwNewRequestCallback,NULL,HttpGwCloseConnection);
    swift::Datagram::Listen3rdPartySocket(install);
}


void HttpGwError (SOCKET s) {
    print_error("httpgw is dead");
    dprintf("%s @0 closed http gateway\n",tintstr());
    close_socket(s);
    swift::Datagram::Listen3rdPartySocket(sckrwecb_t(s));
}


#include <signal.h>
SOCKET InstallHTTPGateway (Address bind_to) {
    SOCKET fd;
    #define gw_ensure(x) { if (!(x)) { \
    print_error("http binding fails"); close_socket(fd); \
    return INVALID_SOCKET; } }
    gw_ensure ( (fd=socket(AF_INET, SOCK_STREAM, 0)) != INVALID_SOCKET );
    int enable = true;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (setsockoptptr_t)&enable, sizeof(int));
    //setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (setsockoptptr_t)&enable, sizeof(int));
    //struct sigaction act;
    //memset(&act,0,sizeof(struct sigaction));
    //act.sa_handler = SIG_IGN;
    //sigaction (SIGPIPE, &act, NULL); // FIXME
    signal( SIGPIPE, SIG_IGN );
    gw_ensure ( 0==bind(fd, (sockaddr*)&(bind_to.addr), sizeof(struct sockaddr_in)) );
    gw_ensure (make_socket_nonblocking(fd));
    gw_ensure ( 0==listen(fd,8) );
    sckrwecb_t install_http(fd,HttpGwNewConnectionCallback,NULL,HttpGwError);
    gw_ensure (swift::Datagram::Listen3rdPartySocket(install_http));
    dprintf("%s @0 installed http gateway on %s\n",tintstr(),bind_to.str());
    return fd;
}

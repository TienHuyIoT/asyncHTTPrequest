#include "asyncHTTPrequest.h"

AsyncConsole AsyncHttpConsole;
#if (1)
#define DEBUG_HTTP_I(f_, ...)  //AsyncHttpConsole.printf_P(PSTR("I [AsyncHTTP] %s(), line %u: " f_ "\r\n"),  __func__, __LINE__, ##__VA_ARGS__)
#define DEBUG_HTTP_E(f_, ...)  //AsyncHttpConsole.printf_P(PSTR("E [AsyncHTTP] %s(), line %u: " f_ "\r\n"),  __func__, __LINE__, ##__VA_ARGS__)
#else
#define DEBUG_HTTP_I(format,...)
#define DEBUG_HTTP_E(format,...)
#endif

//**************************************************************************************************************
asyncHTTPrequest::asyncHTTPrequest()
    : _readyState(readyStateUnsent)
    , _HTTPcode(0)
    , _chunked(false)
    , _debug(DEBUG_IOTA_HTTP_SET)
    , _timeout(DEFAULT_RX_TIMEOUT)
    , _lastActivity(0)
    , _requestStartTime(0)
    , _requestEndTime(0)
    , _URL(nullptr)
    , _connectedHost(nullptr)
    , _connectedPort(-1)
    , _client(nullptr)
    , _contentLength(0)
    , _contentRead(0)
    , _readyStateChangeCB(nullptr)
    , _readyStateChangeCBarg(nullptr)
    , _onDataCB(nullptr)
    , _onDataCBarg(nullptr)
    , _request(nullptr)
    , _response(nullptr)
    , _chunks(nullptr)
    , _headers(nullptr)
{
    DEBUG_HTTP_I("New request.");
#ifdef ESP32
    threadLock = xSemaphoreCreateRecursiveMutex();
#endif
}

//**************************************************************************************************************
asyncHTTPrequest::~asyncHTTPrequest(){
    if(_client) {
        AsyncClient* c = _client;
        // client pointer has to be null to avoid delete AsyncClient from onDisconnect callback
        _client = nullptr;
        delete c;
    }

    if (_URL) {
        delete _URL;
        _URL = nullptr;
    }
    if (_headers) {
        delete _headers;
        _headers = nullptr;
    }
    if (_request) {
        delete _request;
        _request = nullptr;
    }
    if (_response) {
        delete _response;
        _response = nullptr;
    }
    if (_chunks) {
        delete _chunks;
        _chunks = nullptr;
    }
    if (_connectedHost) {
        delete[] _connectedHost;
        _connectedHost = nullptr;
    }
#ifdef ESP32
    vSemaphoreDelete(threadLock);
#endif
}

//**************************************************************************************************************
void    asyncHTTPrequest::setDebug(bool debug){
    if(_debug || debug) {
        _debug = true;
        DEBUG_HTTP_I("setDebug(%s) version %s\r\n", debug ? "on" : "off", asyncHTTPrequest_h);
    }
	_debug = debug;
}

//**************************************************************************************************************
bool    asyncHTTPrequest::debug(){
    return(_debug);
}

//**************************************************************************************************************
bool	asyncHTTPrequest::open(const char* method, const char* URL){
    DEBUG_HTTP_I("open(%s, %.32s)\r\n", method, URL);
    if(_readyState != readyStateUnsent && _readyState != readyStateDone) {return false;}
    _requestStartTime = millis();
    if (_URL) {
        delete _URL;
        _URL = nullptr;
    }
    if (_headers) {
        delete _headers;
        _headers = nullptr;
    }
    if (_request) {
        delete _request;
        _request = nullptr;
    }
    if (_response) {
        delete _response;
        _response = nullptr;
    }
    if (_chunks) {
        delete _chunks;
        _chunks = nullptr;
    }
    _chunked = false;
    _contentRead = 0;
    _readyState = readyStateUnsent;

    if (strcmp(method, "GET") == 0) {
        _HTTPmethod = HTTPmethodGET;
    } else if (strcmp(method, "POST") == 0) {
        _HTTPmethod = HTTPmethodPOST;
    } else {
        return false;
    }

    if (!_parseURL(URL)) {
        return false;
    }
    if( _client && _client->connected()) {
      if ((_connectedHost && strcmp(_URL->host, _connectedHost) != 0) || _URL->port != _connectedPort) {return false;}
    }
    char* hostName = new char[strlen(_URL->host)+10];
    sprintf(hostName,"%s:%d", _URL->host, _URL->port);
    _addHeader("Host",hostName);
    delete[] hostName;
    _lastActivity = millis();
    if (0 == _timeout) {
        _timeout = DEFAULT_RX_TIMEOUT;
    }
	return _connect();
}
//**************************************************************************************************************
void    asyncHTTPrequest::onReadyStateChange(readyStateChangeCB cb, void* arg){
    _readyStateChangeCB = cb;
    _readyStateChangeCBarg = arg;
}

//**************************************************************************************************************
void	asyncHTTPrequest::setTimeout(int seconds){
    DEBUG_HTTP_I("setTimeout(%d)\r\n", seconds);
    _timeout = seconds;
}

//**************************************************************************************************************
bool	asyncHTTPrequest::send(){
    DEBUG_HTTP_I("send()\r\n");
    _seize;
    if( ! _buildRequest()) {
        _release;
        return false;
    }
    _send();
    _release;
    return true;
}

//**************************************************************************************************************
bool    asyncHTTPrequest::send(String body){
    DEBUG_HTTP_I("send(String) %s... (%d)\r\n", body.substring(0,16).c_str(), body.length());
    _seize;
    _addHeader("Content-Length", String(body.length()).c_str());
    if( ! _buildRequest()){
        _release;
        return false;
    }
    _request->write(body);
    _send();
    _release;
    return true;
}

//**************************************************************************************************************
bool	asyncHTTPrequest::send(const char* body){
    DEBUG_HTTP_I("send(char*) %.16s... (%d)\r\n",body, strlen(body));
    _seize;
    _addHeader("Content-Length", String(strlen(body)).c_str());
    if( ! _buildRequest()){
        _release;
        return false;
    } 
    _request->write(body);
    size_t len = _send();
    _release;
    return (len > 0);
}

//**************************************************************************************************************
bool	asyncHTTPrequest::send(const uint8_t* body, size_t len){
    DEBUG_HTTP_I("send(char*) %.16s... (%d)\r\n",(char*)body, len);
    _seize;
    _addHeader("Content-Length", String(len).c_str());
    if( ! _buildRequest()){
        _release;
        return false;
    } 
    _request->write(body, len);
    _send();
    _release;
    return true;
}

//**************************************************************************************************************
bool	asyncHTTPrequest::send(xbuf* body, size_t len){
    DEBUG_HTTP_I("send(char*) %.16s... (%d)\r\n", body->peekString(16).c_str(), len);
    _seize;
    _addHeader("Content-Length", String(len).c_str());
    if( ! _buildRequest()){
        _release;
        return false;
    } 
    _request->write(body, len);
    _send();
    _release;
    return true;
}

//**************************************************************************************************************
void    asyncHTTPrequest::abort(){
    DEBUG_HTTP_I("abort()\r\n");
    _seize;
    if(_client) {
        _client->abort();
    }
    _release;
}
void    asyncHTTPrequest::close(){
    DEBUG_HTTP_I("close()\r\n");
    _seize;
    if(_client) {
        _client->close();
    }
    _release;
}
//**************************************************************************************************************
int		asyncHTTPrequest::readyState(){
    return _readyState;
}

//**************************************************************************************************************
int	asyncHTTPrequest::responseHTTPcode(){
    return _HTTPcode;
}

//**************************************************************************************************************
String	asyncHTTPrequest::responseText(){
    DEBUG_HTTP_I("responseText() ");
    _seize;
    if( ! _response || _readyState < readyStateLoading || ! available()){
        DEBUG_HTTP_I("responseText() no data\r\n");
        _release;
        return String(); 
    }       
    size_t avail = available();
    String localString = _response->readString(avail);
    if(localString.length() < avail) {
        DEBUG_HTTP_E("!responseText() no buffer\r\n");
        _HTTPcode = HTTPCODE_TOO_LESS_RAM;
        if (_client) {
            DEBUG_HTTP_E("aborting client\r\n");
            _client->abort();
        }
        _release;
        return String();
    }
    _contentRead += localString.length();
    DEBUG_HTTP_I("responseText() %s... (%d)\r\n", localString.substring(0,16).c_str() , avail);
    _release;
    return localString;
}

//**************************************************************************************************************
size_t  asyncHTTPrequest::responseRead(uint8_t* buf, size_t len){
    if( ! _response || _readyState < readyStateLoading || ! available()){
        DEBUG_HTTP_I("responseRead() no data\r\n");
        return 0;
    } 
    _seize;
    size_t avail = available() > len ? len : available();
    _response->read(buf, avail);
    DEBUG_HTTP_I("responseRead() %.16s... (%d)\r\n", (char*)buf , avail);
    _contentRead += avail;
    _release;
    return avail;
}

//**************************************************************************************************************
size_t	asyncHTTPrequest::available(){
    if(_readyState < readyStateLoading || !_response) return 0;
    if(_chunked && (_contentLength - _contentRead) < _response->available()){
        return _contentLength - _contentRead;
    }
    return _response->available();
}

//**************************************************************************************************************
size_t	asyncHTTPrequest::responseLength(){
    if(_readyState < readyStateLoading) return 0;
    return _contentLength;
}

//**************************************************************************************************************
void	asyncHTTPrequest::onData(onDataCB cb, void* arg){
    DEBUG_HTTP_I("onData() CB set\r\n");
    _onDataCB = cb;
    _onDataCBarg = arg;
}

//**************************************************************************************************************
uint32_t asyncHTTPrequest::elapsedTime(){
    if(_readyState <= readyStateOpened) return 0;
    if(_readyState != readyStateDone){
        return millis() - _requestStartTime;
    }
    return _requestEndTime - _requestStartTime;
}

//**************************************************************************************************************
String asyncHTTPrequest::version(){
    return String(asyncHTTPrequest_h);
}

/*______________________________________________________________________________________________________________

               PPPP    RRRR     OOO    TTTTT   EEEEE    CCC    TTTTT   EEEEE   DDDD
               P   P   R   R   O   O     T     E       C   C     T     E       D   D
               PPPP    RRRR    O   O     T     EEE     C         T     EEE     D   D
               P       R  R    O   O     T     E       C   C     T     E       D   D
               P       R   R    OOO      T     EEEEE    CCC      T     EEEEE   DDDD
_______________________________________________________________________________________________________________*/

//**************************************************************************************************************
bool  asyncHTTPrequest::_parseURL(const char* url){
    if (_URL) {
        delete _URL;
    }
    _URL = new (std::nothrow) URL;
    if (!_URL) {
        DEBUG_HTTP_E("Failed to allocate URL object\r\n");
        return false;
    }
    _URL->buffer = new (std::nothrow) char[strlen(url) + 8];
    if (!_URL->buffer) {
        DEBUG_HTTP_E("Failed to allocate URL buffer\r\n");
        delete _URL;
        _URL = nullptr;
        return false;
    }
    char *bufptr = _URL->buffer;
    const char *urlptr = url;

        // Find first delimiter

    int seglen = strcspn(urlptr, ":/?");

        // scheme

    _URL->scheme = bufptr;
    if(! memcmp(urlptr+seglen, "://", 3)){
        while(seglen--){
            *bufptr++ = toupper(*urlptr++);
        }
        urlptr += 3;
        seglen = strcspn(urlptr, ":/?");
    }
    else {
        memcpy(bufptr, "HTTP", 4);
        bufptr += 4;
    }
    *bufptr++ = 0;

        // host

    _URL->host = bufptr;
    memcpy(bufptr, urlptr, seglen);
    bufptr += seglen;
    *bufptr++ = 0;
    urlptr += seglen;

        // port 

    if(*urlptr == ':'){
        urlptr++;
        seglen = strcspn(urlptr, "/?");
        char *endptr = 0;
        _URL->port = strtol(urlptr, &endptr, 10);
        if((endptr-urlptr) != seglen){
            return false;
        }
        urlptr = endptr;
    }

        // path 

    _URL->path = bufptr;
    *bufptr++ = '/';
    if(*urlptr == '/'){
        seglen = strcspn(++urlptr, "?");
        memcpy(bufptr, urlptr, seglen);
        bufptr += seglen;
        urlptr += seglen;
    }
    *bufptr++ = 0;

        // query

    _URL->query = bufptr;
    if(*urlptr == '?'){
        seglen = strlen(urlptr);
        memcpy(bufptr, urlptr, seglen);
        bufptr += seglen;
        urlptr += seglen;
    }
    *bufptr++ = 0;

    if(strcmp(_URL->scheme, "HTTP")){
        return false;
    }

    DEBUG_HTTP_I("_parseURL() %s://%s:%d%s%.16s\r\n", _URL->scheme, _URL->host, _URL->port, _URL->path, _URL->query);
    return true;
}

//**************************************************************************************************************
bool  asyncHTTPrequest::_connect(){
    DEBUG_HTTP_I("_connect()\r\n");
    if (_URL == nullptr) {
        DEBUG_HTTP_E("No URL set\r\n");
        return false;
    }
    if(!_client){
        _client = new (std::nothrow) AsyncClient();
        if (!_client) {
            DEBUG_HTTP_E("Failed to allocate AsyncClient object\r\n");
            return false;
        }
        DEBUG_HTTP_I("new client() %u\r\n", _client);
    }
    if (_connectedHost) {
        delete[] _connectedHost;
        _connectedHost = nullptr;
    }
    _connectedHost = new (std::nothrow) char[strlen(_URL->host) + 1];
    if (!_connectedHost) {
        DEBUG_HTTP_E("Failed to allocate connected host buffer\r\n");
        return false;
    }
    strcpy(_connectedHost, _URL->host);
    _connectedPort = _URL->port;
    // _client->setRxTimeout(DEFAULT_RX_TIMEOUT);   // rx timeout is handled by asyncHttp
    _client->onConnect([](void *obj, AsyncClient *client){((asyncHTTPrequest*)(obj))->_onConnect(client);}, this);
    _client->onDisconnect([](void *obj, AsyncClient *client){((asyncHTTPrequest *)(obj))->_onDisconnect(client);}, this);
    _client->onTimeout([](void *obj, AsyncClient *client, uint32_t time){((asyncHTTPrequest*)(obj))->_onTimeout(client, time);}, this);
    _client->onPoll([](void *obj, AsyncClient *client){((asyncHTTPrequest*)(obj))->_onPoll(client);}, this);
    _client->onError([](void *obj, AsyncClient *client, uint32_t error){((asyncHTTPrequest*)(obj))->_onError(client, error);}, this);
    if( ! _client->connected()){
        if( ! _client->connect(_URL->host, _URL->port)) {
            DEBUG_HTTP_E("!client.connect(%s, %d) failed\r\n", _URL->host, _URL->port);
            _HTTPcode = HTTPCODE_NOT_CONNECTED;
            _setReadyState(readyStateDone);
            return false;
        }
    }
    else {
        _onConnect(_client);
    }
    _lastActivity = millis();
    return true;
}

//**************************************************************************************************************
bool   asyncHTTPrequest::_buildRequest(){
    DEBUG_HTTP_I("_buildRequest()\r\n");
    if (_URL == nullptr || _headers == nullptr) {
        DEBUG_HTTP_I("No URL or headers set\r\n");
        return false;
    }
    if( ! _request) {
        _request = new (std::nothrow) xbuf;
        if (_request == nullptr) {
            DEBUG_HTTP_E("Failed to allocate request buffer\r\n");
            return false;
        }
    }
    _request->write(_HTTPmethod == HTTPmethodGET ? "GET " : "POST ");
    _request->write(_URL->path);
    _request->write(_URL->query);
    _request->write(" HTTP/1.1\r\n");
    delete _URL;
    _URL = nullptr;
    header* hdr = _headers;
    while(hdr){
        _request->write(hdr->name);
        _request->write(": ");
        _request->write(hdr->value);
        _request->write("\r\n");
        hdr = hdr->next;
    }
    if (_headers) {
        delete _headers;
        _headers = nullptr;
    }
    _request->write("\r\n");

    return true;
}

//**************************************************************************************************************
size_t  asyncHTTPrequest::_send(){
    if( ! _request) return 0;
    DEBUG_HTTP_I("_send() %d\r\n", _request->available());
    _seize;
    if(!_client || ! _client->connected() || ! _client->canSend() || _readyState < readyStateOpened){
        DEBUG_HTTP_I("*can't send\r\n");
        _release;
        return 0;
    }
    size_t supply = _request->available();
    size_t demand = _client->space();
    if(supply > demand) supply = demand;
    size_t sent = 0;
    uint8_t temp[100];
    while(supply){
        size_t chunk = supply < 100 ? supply : 100;
        supply -= _request->read(temp, chunk);
        sent += _client->add((char*)temp, chunk);
    }
    if(_request->available() == 0){
        delete _request;
        _request = nullptr;
    }
    _client->send();
    DEBUG_HTTP_I("*sent %d\r\n", sent);
    _lastActivity = millis();
    
    _release;
    return sent;
}

//**************************************************************************************************************
void  asyncHTTPrequest::_setReadyState(readyStates newState){
    if(_readyState != newState){
        _readyState = newState;          
        DEBUG_HTTP_I("_setReadyState(%d)\r\n", _readyState);
        if(_readyStateChangeCB){
            _readyStateChangeCB(_readyStateChangeCBarg, this, _readyState);
        }
    } 
}

//**************************************************************************************************************
void  asyncHTTPrequest::_processChunks(){
    while(_chunks->available()){
        DEBUG_HTTP_I("_processChunks() %.16s... (%d)\r\n", _chunks->peekString(16).c_str(), _chunks->available());
        size_t _chunkRemaining = _contentLength - _contentRead - _response->available();
        _chunkRemaining -= _response->write(_chunks, _chunkRemaining);
        if(_chunks->indexOf("\r\n") == -1){
            return;
        }
        String chunkHeader = _chunks->readStringUntil("\r\n");
        DEBUG_HTTP_I("*getChunkHeader %.16s... (%d)\r\n", chunkHeader.c_str(), chunkHeader.length());
        size_t chunkLength = strtol(chunkHeader.c_str(),nullptr,16);
        _contentLength += chunkLength;
        if(chunkLength == 0){
            char* connectionHdr = respHeaderValue("Connection");
            if(connectionHdr && (strcasecmp_P(connectionHdr,PSTR("close")) == 0)){
                DEBUG_HTTP_I("*all chunks received - closing TCP\r\n");
            }
            else {
                DEBUG_HTTP_I("*all chunks received - no header Connection - closing TCP\r\n"); 
            }
            if (_client) {
                _client->close();
            }
            _requestEndTime = millis();
            _lastActivity = 0;
            _setReadyState(readyStateDone);
            return;
        }
    }
}

/*______________________________________________________________________________________________________________

EEEEE   V   V   EEEEE   N   N   TTTTT         H   H    AAA    N   N   DDDD    L       EEEEE   RRRR     SSS
E       V   V   E       NN  N     T           H   H   A   A   NN  N   D   D   L       E       R   R   S 
EEE     V   V   EEE     N N N     T           HHHHH   AAAAA   N N N   D   D   L       EEE     RRRR     SSS
E        V V    E       N  NN     T           H   H   A   A   N  NN   D   D   L       E       R  R        S
EEEEE     V     EEEEE   N   N     T           H   H   A   A   N   N   DDDD    LLLLL   EEEEE   R   R    SSS 
_______________________________________________________________________________________________________________*/

//**************************************************************************************************************
void  asyncHTTPrequest::_onConnect(AsyncClient* client){
    DEBUG_HTTP_I("handler client %u\r\n", client);
    _seize;
    if (!_response) {
        _response = (_client == client) ? new (std::nothrow) xbuf : nullptr ;
        if (_response == nullptr) {
            DEBUG_HTTP_E("Failed to allocate response buffer\r\n");
            _HTTPcode = HTTPCODE_NOT_CONNECTED;
            _setReadyState(readyStateDone);
            _release;
            return;
        }
    }
    _setReadyState(readyStateOpened);
    _contentLength = 0;
    _contentRead = 0;
    _chunked = false;
    _client->onAck([](void* obj, AsyncClient* client, size_t len, uint32_t time){((asyncHTTPrequest*)(obj))->_send();}, this);
    _client->onData([](void* obj, AsyncClient* client, void* data, size_t len){((asyncHTTPrequest*)(obj))->_onData(data, len);}, this);
    DEBUG_HTTP_I("_send()\r\n");
    bool canSend = _send();
    DEBUG_HTTP_I("canSend=%d\r\n", canSend);
    _lastActivity = millis();
    _release;
}

//**************************************************************************************************************
void  asyncHTTPrequest::_onTimeout(AsyncClient* client, uint32_t time){
    DEBUG_HTTP_I("handler client %u, time=%u\r\n", client, time);
    _seize;
    _HTTPcode = HTTPCODE_TIMEOUT;
    if (client) {
        client->close();
    }
    _release;     
}

void  asyncHTTPrequest::_onPoll(AsyncClient* client){
    DEBUG_HTTP_I("handler client %u\r\n", client);
    _seize;
    if(_timeout && (millis() - _lastActivity) > (_timeout * 1000)){
        DEBUG_HTTP_I("_onPoll timeout\r\n");
        _HTTPcode = HTTPCODE_TIMEOUT;
        if (client) {
            client->close();
        }
    }
    if(_onDataCB && available()){
        _onDataCB(_onDataCBarg, this, available());
    } 
    _release;     
}

//**************************************************************************************************************
void  asyncHTTPrequest::_onError(AsyncClient* client, int8_t error){
    DEBUG_HTTP_I("handler client %u error=%d\r\n", client, error);
    _HTTPcode = error;
}

//**************************************************************************************************************
void  asyncHTTPrequest::_onDisconnect(AsyncClient* client){
    DEBUG_HTTP_I("handler client %u\r\n", client);
    _seize;

    _client = nullptr;
    if (client) {
        DEBUG_HTTP_I("delete client() %u\r\n", client);
        delete client;
    }

    if(_readyState < readyStateOpened){
        _HTTPcode = HTTPCODE_NOT_CONNECTED;
    }
    else if (_HTTPcode > 0 && _response &&
            (_readyState < readyStateHdrsRecvd || (_contentRead + _response->available()) < _contentLength)) {
        _HTTPcode = HTTPCODE_CONNECTION_LOST;
    }

    _connectedPort = -1;
    _requestEndTime = millis();
    _lastActivity = 0;
    _setReadyState(readyStateDone);

    if (_connectedHost) {
        delete[] _connectedHost;
        _connectedHost = nullptr;
    }
    if (_URL) {
        delete _URL;
        _URL = nullptr;
    }
    if (_headers) {
        delete _headers;
        _headers = nullptr;
    }
    if (_request) {
        delete _request;
        _request = nullptr;
    }
    if (_response) {
        delete _response;
        _response = nullptr;
    }
    if (_chunks) {
        delete _chunks;
        _chunks = nullptr;
    }
    _release;
}

//**************************************************************************************************************
void  asyncHTTPrequest::_onData(void* Vbuf, size_t len){
    DEBUG_HTTP_I("_onData handler %.16s... (%d)\r\n",(char*) Vbuf, len);
    _seize;
    _lastActivity = millis();

    if (!_response) {
        _release;  
        return;
    }
    
                // Transfer data to xbuf

    if(_chunks){
        _chunks->write((uint8_t*)Vbuf, len);
        _processChunks();
    }
    else {
        _response->write((uint8_t*)Vbuf, len);                
    }

                // if headers not complete, collect them.
                // if still not complete, just return.

    if(_readyState == readyStateOpened){
        if( ! _collectHeaders()) {
            _release;  
            return;
        }
    }

    // If there's data in the buffer and not Done,
    // advance readyState to Loading.
    if(_response->available() && _readyState != readyStateDone){
        DEBUG_HTTP_I("Set readyStateLoading(%u)", readyStateLoading);
        _setReadyState(readyStateLoading);
    }

    // If not chunked and all data read, close it up.
    if( ! _chunked && _contentLength && (_response->available() + _contentRead) >= _contentLength){
        char* connectionHdr = respHeaderValue("Connection");
        if(connectionHdr && (strcasecmp_P(connectionHdr,PSTR("Close")) == 0)){
            DEBUG_HTTP_I("*all data received - closing TCP\r\n");
        }
        else {
            DEBUG_HTTP_I("*all data received - no header Connection - closing TCP\r\n");
        }
        if (_client) {
            _client->close();
        }
        _requestEndTime = millis();
        _lastActivity = 0;
        _setReadyState(readyStateDone);        
    }

                // If onData callback requested, do so.

    if(_onDataCB && available()){
        _onDataCB(_onDataCBarg, this, available());
    }
    _release;            
          
}

//**************************************************************************************************************
bool  asyncHTTPrequest::_collectHeaders(){
    DEBUG_HTTP_I("_collectHeaders()\r\n");

            // Loop to parse off each header line.
            // Drop out and return false if no \r\n (incomplete)

    do {
        String headerLine = _response->readStringUntil("\r\n");

            // If no line, return false.

        if( ! headerLine.length()){
            return false;
        }  

            // If empty line, all headers are in, advance readyState.
           
        if(headerLine.length() == 2){
            DEBUG_HTTP_I("Set readyStateHdrsRecvd(%u)", readyStateHdrsRecvd);
            _setReadyState(readyStateHdrsRecvd);
        }

            // If line is HTTP header, capture HTTPcode.

        else if(headerLine.substring(0,7) == "HTTP/1."){
            _HTTPcode = headerLine.substring(9, headerLine.indexOf(' ', 9)).toInt();
        }

            // Ordinary header, add to header list.

        else {
            int colon = headerLine.indexOf(':');
            if(colon != -1){
                String name = headerLine.substring(0, colon);
                name.trim();
                String value = headerLine.substring(colon+1);
                value.trim();
                _addHeader(name.c_str(), value.c_str());
            }   
        } 
    } while(_readyState == readyStateOpened); 

            // If content-Length header, set _contentLength

    header *hdr = _getHeader("Content-Length");
    if(hdr){
        _contentLength = strtol(hdr->value,nullptr,10);
    }

            // If chunked specified, try to set _contentLength to size of first chunk

    hdr = _getHeader("Transfer-Encoding"); 
    if(hdr && strcasecmp_P(hdr->value, PSTR("chunked")) == 0){
        DEBUG_HTTP_I("*transfer-encoding: chunked\r\n");
        _chunked = true;
        _contentLength = 0;
        _chunks = new (std::nothrow) xbuf;
        if (!_chunks) {
            DEBUG_HTTP_E("Failed to allocate chunk buffer\r\n");
            return false;
        }
        _chunks->write(_response, _response->available());
        _processChunks();
    }         

    
    return true;
}      
        

/*_____________________________________________________________________________________________________________

                        H   H  EEEEE   AAA   DDDD   EEEEE  RRRR    SSS
                        H   H  E      A   A  D   D  E      R   R  S   
                        HHHHH  EEE    AAAAA  D   D  EEE    RRRR    SSS
                        H   H  E      A   A  D   D  E      R  R       S
                        H   H  EEEEE  A   A  DDDD   EEEEE  R   R   SSS
______________________________________________________________________________________________________________*/

//**************************************************************************************************************
void	asyncHTTPrequest::setReqHeader(const char* name, const char* value){
    if(_readyState <= readyStateOpened && _headers){
        _addHeader(name, value);
    }
}

//**************************************************************************************************************
void	asyncHTTPrequest::setReqHeader(const char* name, const __FlashStringHelper* value){
    if(_readyState <= readyStateOpened && _headers){
        char* _value = _charstar(value);
        if (_value == nullptr) {
            DEBUG_HTTP_E("Failed to convert FlashString to char*\r\n");
            return;
        }
        _addHeader(name, _value);
        delete[] _value;
    }
}

//**************************************************************************************************************
void	asyncHTTPrequest::setReqHeader(const __FlashStringHelper *name, const char* value){
    if(_readyState <= readyStateOpened && _headers){
        char* _name = _charstar(name);
        if (_name == nullptr) {
            DEBUG_HTTP_E("Failed to convert FlashString to char*\r\n");
            return;
        }
        _addHeader(_name, value);
        delete[] _name;
    }
}

//**************************************************************************************************************
void	asyncHTTPrequest::setReqHeader(const __FlashStringHelper *name, const __FlashStringHelper* value){
    if(_readyState <= readyStateOpened && _headers){
        char* _name = _charstar(name);
        char* _value = _charstar(value);
        if (_name == nullptr || _value == nullptr) {
            DEBUG_HTTP_E("Failed to convert FlashString to char*\r\n");
            if (_name) delete[] _name;
            if (_value) delete[] _value;
            return;
        }
        _addHeader(_name, _value);
        delete[] _name;
        delete[] _value;
    }
}

//**************************************************************************************************************
void	asyncHTTPrequest::setReqHeader(const char* name, int32_t value){
    if(_readyState <= readyStateOpened && _headers){
        setReqHeader(name, String(value).c_str());
    }
}

//**************************************************************************************************************
void	asyncHTTPrequest::setReqHeader(const __FlashStringHelper *name, int32_t value){
    if(_readyState <= readyStateOpened && _headers){
        char* _name = _charstar(name);
        if (_name == nullptr) {
            DEBUG_HTTP_E("Failed to convert FlashString to char*\r\n");
            return;
        }
        setReqHeader(_name, String(value).c_str());
        delete[] _name;
    }
}

//**************************************************************************************************************
int		asyncHTTPrequest::respHeaderCount(){
    if(_readyState < readyStateHdrsRecvd) return 0;                                            
    int count = 0;
    header* hdr = _headers;
    while(hdr){
        count++;
        hdr = hdr->next;
    }
    return count;
}

//**************************************************************************************************************
char*   asyncHTTPrequest::respHeaderName(int ndx){
    if(_readyState < readyStateHdrsRecvd) return nullptr;      
    header* hdr = _getHeader(ndx);
    if ( ! hdr) return nullptr;
    return hdr->name;
}

//**************************************************************************************************************
char*   asyncHTTPrequest::respHeaderValue(const char* name){
    if(_readyState < readyStateHdrsRecvd) return nullptr;      
    header* hdr = _getHeader(name);
    if( ! hdr) return nullptr;
    return hdr->value;
}

//**************************************************************************************************************
char*   asyncHTTPrequest::respHeaderValue(const __FlashStringHelper *name){
    if(_readyState < readyStateHdrsRecvd) return nullptr;
    char* _name = _charstar(name);
    if (_name == nullptr) {
        DEBUG_HTTP_E("Failed to convert FlashString to char*\r\n");
        return nullptr;
    }   
    header* hdr = _getHeader(_name);
    delete[] _name;
    if( ! hdr) return nullptr;
    return hdr->value;
}

//**************************************************************************************************************
char*   asyncHTTPrequest::respHeaderValue(int ndx){
    if(_readyState < readyStateHdrsRecvd) return nullptr;      
    header* hdr = _getHeader(ndx);
    if ( ! hdr) return nullptr;
    return hdr->value;
}

//**************************************************************************************************************
bool	asyncHTTPrequest::respHeaderExists(const char* name){
    if(_readyState < readyStateHdrsRecvd) return false;      
    header* hdr = _getHeader(name);
    if ( ! hdr) return false;
    return true;
}

//**************************************************************************************************************
bool	asyncHTTPrequest::respHeaderExists(const __FlashStringHelper *name){
    if(_readyState < readyStateHdrsRecvd) return false;
    char* _name = _charstar(name);   
    if (_name == nullptr) {
        DEBUG_HTTP_E("Failed to convert FlashString to char*\r\n");
        return false;
    }   
    header* hdr = _getHeader(_name);
    delete[] _name;
    if ( ! hdr) return false;
    return true;
}

//**************************************************************************************************************
String  asyncHTTPrequest::headers(){
    _seize;
    String respHeader = "";
    header* hdr = _headers;
    while(hdr){
        respHeader += hdr->name;
        respHeader += ':';
        respHeader += hdr->value;
        respHeader += "\r\n";
        hdr = hdr->next;
    }
    respHeader += "\r\n";
    _release;
    return respHeader;
}

//**************************************************************************************************************
asyncHTTPrequest::header*  asyncHTTPrequest::_addHeader(const char* name, const char* value){
	DEBUG_HTTP_I("_addHeader %s, %s\r\n", name, value);
    if (value == nullptr || name == nullptr || strlen(name) == 0 || strlen(value) == 0) {
        DEBUG_HTTP_I("Invalid header name or value\r\n");
        return nullptr;
    }
    header** hdr = &_headers;
    _seize;
	while (*hdr) {
		if (strcasecmp(name, (*hdr)->name) == 0) {
			header* oldHdr = *hdr;
			*hdr = (*hdr)->next;
			oldHdr->next = nullptr; // Avoid recursive delete to free entire list
			delete oldHdr;
		} else {
			hdr = &(*hdr)->next;
		}
	}

	*hdr = new (std::nothrow) header;
    if (!*hdr) {
        DEBUG_HTTP_E("Failed to allocate header object\r\n");
        _release;
        return nullptr;
    }
	(*hdr)->name = new (std::nothrow) char[strlen(name) + 1];
    if (!(*hdr)->name) {
        DEBUG_HTTP_E("Failed to allocate header name buffer\r\n");
        delete *hdr;
        *hdr = nullptr;
        _release;
        return nullptr;
    };
	strcpy((*hdr)->name, name);
	(*hdr)->value = new (std::nothrow) char[strlen(value) + 1];
    if (!(*hdr)->value) {
        DEBUG_HTTP_E("Failed to allocate header value buffer\r\n");
        delete *hdr;
        *hdr = nullptr;
        _release;
        return nullptr;
    }
	strcpy((*hdr)->value, value);
    _release;
    return *hdr;
}

//**************************************************************************************************************
asyncHTTPrequest::header* asyncHTTPrequest::_getHeader(const char* name){
    _seize;
    header* hdr = _headers;
    while (hdr) {
        if(strcasecmp(name, hdr->name) == 0) break;
        hdr = hdr->next;
    }
    _release;
    return hdr;
}

//**************************************************************************************************************
asyncHTTPrequest::header* asyncHTTPrequest::_getHeader(int ndx){
    _seize;
    header* hdr = _headers;
    while (hdr) {
        if( ! ndx--) break;
        hdr = hdr->next; 
    }
    _release;
    return hdr;
}

//**************************************************************************************************************
char* asyncHTTPrequest::_charstar(const __FlashStringHelper * str){
  if( ! str) return nullptr;
  char* ptr = new (std::nothrow) char[strlen_P((PGM_P)str)+1];
  if (!ptr) {
      DEBUG_HTTP_E("Failed to allocate char buffer\r\n");
      return nullptr;
  }
  strcpy_P(ptr, (PGM_P)str);
  return ptr;
}

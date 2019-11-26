#include <string>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <map>
#include <regex>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <netinet/tcp.h>

using namespace std;

// URI 데이터 구조체
// RFC 문서 3986번을 기준으로 하여 구조체를 만듬.
struct URI{
	string scheme; // http://, ftp:// 같은 것
	string authority; // username:password 형식으로 구성됨
	string url; // URL (domain 주소)
	string path; // html과 같은 Content의 파일경로
	string param; // HTTP Parameter (GET MODE)
	string hash; // hash tag, (#asdasdas 과 같은 형식으로 이루어져있음)
	string errorCode; // Error Checking용 (프로그램 제작시 편의로 임의로 생성한 필드)
	int len; // URI 총 길이
};

// HTTP Request Packet 구조체
/* 
   브라우저로 테스트 시에, HTTP/1.1 환경에서 동작하기 때문에
   HTTP/1.1 을 기준으로 하였음
   Method 는 GET, POST 만 지원함
   나머지 Method를전송할시 501(Not Implement)에러를 띄움
   잘못된 Request의 경우 400(Bad Request)에러를 띄움

   HTTP Method 앞에 공백이나, tab이 오는 경우를 허용(일부 웹 서버 프로그램(Apache)이 이를 지원함..)
   실제로 2015~2016년 기준으로 warning.or.kr이 HTTP Request Method 앞에 공백이 들어간 Request를 처리하지 못하여, 검열을 하지 못한 사례가 있음
   (Dotge Chrome이 대표적 예시..)
*/   
struct HTTPRequest{
	string method; // GET,POST, etc..
	URI uri; // HTTP REQUEST URI 
	string version; // HTTP Version
	map<string,string> attr; // HTTP Request Header Field, "key: value" 파싱 하여 map에 저장
	string body; // Request body
	string errorCode; // Error Checking용 (프로그램 제작시 편의로 임의로 생성한 필드)
	int len; // HTTP Request 총 길이..
};


// HTTP Response Packet 구조체
/* 
   브라우저로 테스트 시에, HTTP/1.1 환경에서 동작하기 때문에
   HTTP/1.1 을 기준으로 하였음
   지원하는 Status는 아래와 같음.
   
   200 OK
   206 Partial Content
   400 Bad Request
   404 Not Found
   411 Length Required
   414 URI Too Long
   416 Range Not Satisfiable
   501 Not Implemented

   지원하는 MIME TYPE은 아래와 같음
   content_type[".css"] = "text/css";
   content_type[".js"] = "text/javascript";
   content_type[".html"] = "text/html";
   content_type[".mp3"] = "audio/mp3";
   content_type[".wav"] = "audio/wav";
   content_type[".mp4"] = "video/mp4;";
   content_type[".pdf"] = "application/pdf";
   content_type[".png"] = "image/png";
   content_type[".jpg"] = "image/jpeg";
   content_type[".gif"] = "image/gif";
   content_type["etc"] = "text/html";
*/
struct HTTPResponse{
	string version;
	string code;
	string status;
	map<string,string> attr;
	string body;
	string errorCode;

	string serialize();
};

// 파싱된 HTTP Request를 받아 Response를 생성하는 함수
pair<HTTPResponse,int> makeResponse(HTTPRequest req,int k_key);

// HTTP Request를 Parsing하는 함수
HTTPRequest parseHeader(string buffer);

// URI 파싱함수, 정규표현식을 사용함
URI parsing_uri(string url);

// Split함수, Auxilary(Utility Function)
vector<string> split(string H,string N);

// make range 함수: range field 생성용
vector<pair<int,int>> make_range(string input,int end);

// Filesize를 구하는 함수(stat system call 이용 Aux func)
int getFileSize(string filename);

// Space skip function (Auxilary Function)
string consumeSpace(string &str);

// error Message 출력
void errorHandling(char *message);

// Client 연결이 들어올 경우에 대한 event handler
void clientConnection(int arg);

// 실제클라이언트에 대한 Request를 처리하는 함수
int processRequest(int arg);


// socket table 관리 함수
unordered_map<int,int> socktable;

// keep-alive count를 저장하는 배열
unordered_map<int,pair<int,int>> keep_alive_table;

// 공유데이터의 동기화를 위한 mutex 변수
mutex mtx_lock;

const int BUFSIZE = 1024;
vector<const char *> methods = {"GET","POST"};

// status code와 그에대한 message를 대응시킨 table
vector<const char *> status(600); 

// content type과 그에대한 message를 대응시킨 table
map<string,string> content_type;


int main(int argc,char **argv)
{
	int serv_sock,clnt_sock;
	struct sockaddr_in serv_addr;
	struct sockaddr_in clnt_addr;
	int clnt_addr_size;

	thread routine;

	if(argc != 2){
		printf("Usage : %s <port>\n",argv[0]);
		exit(1);
	}
	
	/*
	 	소켓 초기화 루틴
	 */

	serv_sock = socket(PF_INET,SOCK_STREAM,0);
	
	memset(&serv_addr,0,sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(atoi(argv[1]));

	if(bind(serv_sock,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) == -1)
		errorHandling("bind() Error!");
	
	if(listen(serv_sock,100) == -1)
		errorHandling("listen() Error!");

	int cnt=0;

	// status table과 content_type에 대한 table 초기화
	// 구현의 편의성을 위해 확장자를 기준으로 MIME TYPE을 결정
	// 크롬과 같은 브라우저의 경우 Accept에 요구할 MIME TYPE을 제대로 표기하지 않는 경우가 있음 (.mp4,mp3)
	status[200] = "OK";
	status[206] = "Partial Content";
	status[400] = "Bad Request";
	status[404] = "NOT FOUND";
	status[411] = "Length Required";
	status[414] = "URI Too Long";
	status[416] = "Range Not Satisfiable";
	status[501] = "Not Implemented";

	content_type[".css"] = "text/css";
	content_type[".js"] = "text/javascript";
	content_type[".html"] = "text/html";
	content_type[".mp3"] = "audio/mp3";
	content_type[".wav"] = "audio/wav";
	content_type[".mp4"] = "video/mp4;";
	content_type[".pdf"] = "application/pdf";
	content_type[".png"] = "image/png";
	content_type[".jpg"] = "image/jpeg";
	content_type[".gif"] = "image/gif";
	content_type["etc"] = "text/html";

	// 대량의 data 전송시에 Broken Pipe 신호가발생하여 서버가 강제 종료되는 경우를 방지하기 위해 SIGPIPE signal을무시 
	signal(SIGPIPE, SIG_IGN);

	/*
	   Client Connection 연결을 처리하는 부분
	   동시에 여러개의 host가 연결하는 경우를 처리하기 위해
	   multi thread 기반의 서버를 작성하였음.
	   이때 각thread 마다1개의 socket descriptor를 할당하고 이를 관리하기 위한 socket table을 unordered_map을 이용하여 구현
	   따로 Thread pool 생성과 같은 optimization은 수행하지 않음.
	*/   
	while(true)
	{
		// client connection accept
		clnt_addr_size = sizeof(clnt_addr);
		clnt_sock = accept(serv_sock,(struct sockaddr *)&clnt_addr,(unsigned int *)&clnt_addr_size);
		
		// debugging stuff
		printf("SOCK : %d\n",clnt_sock);

		// 각각의 thread에 할당 될socket table을 관리
		mtx_lock.lock();
		socktable.insert({clnt_sock,clnt_sock});
		mtx_lock.unlock();

		// thread 생성 : clientConnection 함수가 event handler함수이다
		routine = thread(clientConnection,clnt_sock);
		
		// thread를 detach시켜서 독립적인 thread로 만들어버림.
		// 따로 thread pool을 구현하지 않아 이렇게 구현함
		if(routine.joinable())
			routine.detach();
	}

	return 0;
}

// thread event handler
void clientConnection(int arg)
{

	// timeout을 5초로 설정
	int timeout_value = 5;
	// 한 connection당 100개의request를 보낼때 까지 연결을 유지
	int conn_max = 100;
			
	mtx_lock.lock();
	keep_alive_table.insert({arg,make_pair(timeout_value,conn_max)});
	mtx_lock.unlock();
	
	while(conn_max > 0){

		// HTTP Response Code 가 200, 206 인 경우, 연결을 계속 유지
		mtx_lock.lock();
		if(keep_alive_table.count(arg)>0){
			keep_alive_table.erase(arg);
			keep_alive_table.insert({arg,make_pair(timeout_value,conn_max)});
		}
		mtx_lock.unlock();
		int res = processRequest(arg);

		// 200이나 206이 아닌 Error Message의 경우 곧바로 연결을 close 시켜벼럼
		if(res != 200 && res != 206){
			break;
		}
		conn_max--;
	}

	// 연결 해제전에 keep_alive_table을 정리하고  socket_table에서 해당 handler에 할당된 socket descriptor를 제거함
	// socket table은 공유 자원이기 때문에 mutex를 이용하여 동기화 진행
	mtx_lock.lock();
	if(socktable.count(arg)>0){
		socktable.erase(arg);
		if(keep_alive_table.count(arg)>0)
			keep_alive_table.erase(arg);
	}
	mtx_lock.unlock();

	// 연결종료
	close(arg);
}

int processRequest(int arg)
{
	int clnt_sock = arg;
	int length = 0;
	int total_length;
	int remain = 0;
	char chunk[BUFSIZE];

	string buffer = "";

	// HTTP Request 의 End-marker 문자가 올때까지 버퍼에 계속해서 저장
	// \r\n\r\n 이후의 데이터가 버퍼에 쌓이는 경우도 처리하기 위해 remain 변수에 \r\n\r\n이후에 몇 byte를 받았는지를 저장
	while( (length = read(clnt_sock,chunk,BUFSIZE)) )
	{
		if(length > 0)
			buffer += string(chunk,length);
		
		if((remain = buffer.find("\r\n\r\n")) != string::npos)
		{
			cout << buffer.size() << " " << remain << endl;
			remain = buffer.size() - remain - 4;
			break;
		}
	}

	
	// buffer에 있는Header를recognize하여 parsing하는함수를 호출하여 HTTPRequest 구조체에 저장
	// C++11 auto keyword를 사용하여 동적 타이핑으로 변수 할당
	auto req = parseHeader(buffer);
	req.body = "";
	
	// buffer에서 HTTP Request header 부분을 제외한 body 부분을 읽어들여 HTTPRequest body field에 저장
	if(buffer.size() != 0)
		req.body += buffer.substr(buffer.size()-remain);
	
	// Content Length Field가 존재하는 경우에만 request body를 읽어들임
	if(req.attr.count("Content-Length") == 1)
	{
		length = 0;
		size_t total = stoi(req.attr["Content-Length"]) >= remain ? stoi(req.attr["Content-Length"]) - remain : 0;
		for(size_t size_to_recv = total; size_to_recv > 0; )
		{
			length = read(clnt_sock,chunk,BUFSIZE);
			
			if(length != -1){
				req.body += string(chunk,length);
				buffer += string(chunk,length);
			}
			size_to_recv -= length;
		}
	}
	
	// HTTP Request Header 구조체를 입력받아 HTTP Response Header와 uri path 경로에 존재하는 file descriptor를 반환하는 함수를 호출
	// uri path에 없는경우 descriptor field는 -1이 된다.
	auto tmp = makeResponse(req,clnt_sock);

	auto res = tmp.first;
	auto fd_send = tmp.second;

	// HTTP response header serialization 함수
	string header = res.serialize();

	// HTTP response header부분 먼저 전송
	write(clnt_sock,header.c_str(),header.size());
	
	// error Message의 경우 지정된HTML형식으로 이루어진 Error Message를 전송한다
	if(res.code != "200" && res.code != "206")
	{
		string data = "";
		data += "<h1>ERROR!</h1>";
		write(clnt_sock,data.c_str(),data.size());
		return stoi(res.code);
	}
	else
	{
		// Content-Length 만큼 파일을 읽어들여서 전송하는 부분
		off_t offset = 0;
		size_t total_size = stoi(res.attr["Content-Length"]);
		
		// Partial-Content 를 이용하여 전송하는 경우 Range Field를 HTTP Response Header에 추가 해준다
		// 시작범위-시작범위+ 요청 크기
		if(res.code == "206")
		{
			// range Header 파싱
			pair<int,int> range = make_range(req.attr["Range"],total_size).back();
			offset = range.first;
			total_size = range.second;
		}
		
		// 실제로 파일을 전송하는 부분
		// sendfile함수를 이용하여 간편하게 구현 가능함
		for(size_t size_to_send = total_size; size_to_send > 0; )
		{
			ssize_t sent = sendfile(clnt_sock,fd_send,&offset,total_size);
			if(sent <= 0)
			{
				//if(sent !=0){
				//	flag = true;
				//	close(clnt_sock);
				//}
				
				break;
			}
			offset += sent;
			size_to_send -= sent;
		}
		close(fd_send);
	}


	// Print HTTP Request
	cout << "method" << " => " << req.method << endl;
	cout << "scheme => " << req.uri.scheme << endl;
	cout << "authority => " << req.uri.authority << endl;
	cout << "url => " << req.uri.url << endl;
	cout << "path => " << req.uri.path << endl;
	cout << "param => " << req.uri.param << endl;
	cout << "hash => " << req.uri.hash << endl;
	cout << "errorCode => " << req.uri.errorCode << endl;
	cout << "version" << " => " << req.version << endl;

	for(auto &p : req.attr)
		cout << p.first << " => " << p.second << endl;
	

	// return status code
	return stoi(res.code);

}


// HTTP Request Header 파싱함수
HTTPRequest parseHeader(string buffer)
{
	HTTPRequest req;
	string header = "",body = "";
	int pos;

	req.len = buffer.size();

	// Buffer에서 Request End-marker(\r\n\r\n)부분까지 잘라냄
	pos = buffer.find("\r\n\r\n");
	header = buffer.substr(0,pos+2);

	// HTTP Request 첫번째line([Method][sp][uri][sp][version][\r\n\r\n]) 만 잘라내서 가져옴
	pos = header.find("\r\n");
	auto line = header.substr(0,pos);

	// method 부분 파싱
	auto matched = string::npos;
	for(auto &method : methods)
	{
		if((matched = line.find(method) ) != string::npos)
		{
			req.method = method;
			line = line.substr(matched + strlen(method));
			break;
		}
	}

	if(matched == string::npos){
		req.errorCode = "ERROR";
		return req;
	}


	// [Method] 다음 문자가 공백 문자인지 판단하여 그렇지 않은 경우 error를 리턴하게 함
	// RFC 규정상 Method 다음에는 공백문자가 와야함
	if(line[0] != ' '){
		req.errorCode = "ERROR";
		return req;
	}

	// skip space
	line = line.substr(1);

	// skip space
	line = consumeSpace(line);
	
	// URI Parsing routine
	string uri = "";
	for(int i =0;i<line.size();i++)
	{
		if(line[i] == ' '){
			line = line.substr(i+1);
			break;
		}
		uri += line[i];
	}
	req.uri = parsing_uri(uri);
	

	// HTTP Request version parsing
	auto version = line.substr(0,8);

	// HTTP/2.0을 미지원함
	// 에러 발생시 req.errorCode 에 ERROR 문자열 할당
	if(version != "HTTP/1.1" && version != "HTTP/1.0")
	{
		cout << "version : " << version << endl;
		req.errorCode = "ERROR";
		return req;
	}

	req.version = version;

	// HTTP Request Header Field 파싱하는 부분
	// Field를 1개씩 순차적으로 parsing
	header = header.substr(pos+2);
	while((pos = header.find("\r\n")) != string::npos)
	{
		// HTTP Request Header Field의key 부분을 파싱
		auto attr = header.substr(0,pos);
		auto key_pos = attr.find(": ");
		auto key = attr.substr(0,key_pos);

		// value 부분 파싱
		auto value = attr.substr(key_pos+2);

		req.attr[key] = value;
		
		// 다음 필드를 가리키게 함
		header = header.substr(pos+2);
	}

	// 에러가 없는 경우 OK값을 저장
	req.errorCode = "OK";
	return req;
}

// URI 파싱 함수
// 정규표현식을 이용하여 parsing
URI parsing_uri(string url)
{
	URI uri = {"http","","","","","",""};
	regex pattern("(\\w+:)?(\\/)+[^\\s]+");
	smatch matched;
	string parsed_url;
	int pos = 0;

	// 정규표현식을 이용하여 URI를 parsing 함
	// URI 형식에 맞지 않으면 error Return
	regex_match(url,matched,pattern);

	auto it = matched.begin();
	if(it != matched.end()){
		parsed_url = *it;
		uri.len = parsed_url.size();
	}
	else{
		uri.errorCode = "ERROR";
		return uri;
	}

	// scheme parsing (http,https, etc.. 여기서는 http 만 지원함..)
	pos = parsed_url.find("://");
	if(pos != string::npos){
		uri.scheme = parsed_url.substr(0,pos);
		parsed_url = parsed_url.substr(pos+3);
	}
	
	// authority parsing (username, password)
	pos = parsed_url.find("@");
	if(pos != string::npos){
		uri.authority = parsed_url.substr(0,pos);
		parsed_url = parsed_url.substr(pos+1);
	}

	// hash tag 파싱
	pos = parsed_url.find("#");
	if(pos != string::npos){
		uri.hash = parsed_url.substr(pos+1);
		parsed_url = parsed_url.substr(0,pos);
	}
	
	// parameter 파싱
	pos = parsed_url.find("?");
	if(pos != string::npos){
		uri.param = parsed_url.substr(pos+1);
		parsed_url = parsed_url.substr(0,pos);
	}

	// URI path를 파싱한다
	pos = parsed_url.find("/");
	if(pos != string::npos){
		uri.path = parsed_url.substr(pos);
		parsed_url = parsed_url.substr(0,pos);
		uri.url = parsed_url;
	}
	else{
		uri.errorCode = "ERROR";
		return uri;
	}

	uri.errorCode = "OK";
	return uri;
}

// HTTP Response Header 생성하는 함수
pair<HTTPResponse,int> makeResponse(HTTPRequest req,int k_key)
{
	HTTPResponse res;
	int fd=-1,length;
	int timeout_value,conn_max;
	string unit;
	vector<pair<int,int>> range;

	// HTTP version, status code, status message 생성
	res.version = "HTTP/1.1";
	res.code = "200";
	res.status = status[stoi(res.code)];

	mtx_lock.lock();
	
	// keep-alive header를 추가. thread 별로 관리해야 되서 connection_max 속성과 timeout 속성을keep_alive table을 이용하여 관리 
	timeout_value = keep_alive_table[k_key].first;
	conn_max = keep_alive_table[k_key].second;
	
	res.attr["Connection"] = "Keep-Alive";
	res.attr["Keep-Alive"] = "timeout=" + to_string(timeout_value) + ", " + "max=" + to_string(conn_max);
	
	mtx_lock.unlock();


	// Partial content 요청을 하는 경우 range를 추가함.
	// range 형식에 맞지 않는 경우 그냥 무시
	if(req.attr.count("Range") > 0)
	{
		res.code = "206";
		res.status = status[stoi(res.code)];
	}
	
	// HTTP Request가정상적이지 않는 경우 Bad request error으로 응답.
	if(req.errorCode == "ERROR" || req.uri.scheme != "http")
	{
		res.code = "400";
		res.status = status[stoi(res.code)];
		res.attr["Content-Length"] = "15";
	}

	// request body가 존재하나 Content length Field 가 없는경우에도 bad request error로 응답.
	if(req.body.size() > 0 && req.attr.count("Content-Length") == 0)
	{
		res.code = "400";
		res.status = status[stoi(res.code)];
		res.attr["Content-Length"] = "15";
	}

	// Content length Field와 request body 길이가 일치하지 않는경우에도bad request error 에러 로 응답.
	if(req.attr.count("Content-Length") != 0 && req.body.size() > stoi(req.attr["Content-Length"]))
	{
		res.code = "400";
		res.status = status[stoi(res.code)];
		res.attr["Content-Length"] = "15";
	}

	// URI 가 너무 긴 경우(여기서는 1024자 이상) 414(URL TOO LONG)에러로 응답
	if(req.uri.len > 1024)
	{
		res.code = "414";
		res.status = status[stoi(res.code)];
	}

	// GET,POST Method가 아니면 501에러 리턴
	if(!(req.method == "GET" || req.method == "POST"))
	{
		res.code = "501";
		res.status = status[stoi(res.code)];
		res.attr["Content-Length"] = "15";
	}

	// uri path에 있는 파일을 open 하고 그 길이를 구해옴
	if(req.uri.path.size() > 1 && access(req.uri.path.substr(1).c_str(),R_OK) == 0){
		fd = open(req.uri.path.substr(1).c_str(),O_RDONLY);
		length = getFileSize(req.uri.path.substr(1));
	}
	
	// 기본 path 의 경우 index.html리턴
	if(req.uri.path == ""){
		fd = open("./index.html",O_RDONLY);
		length = getFileSize("./index.html");
	}

	// 파일이 존재하지 않으면 404 리턴
	if(fd < 0)
	{
		res.code = "404";
		res.status = status[stoi(res.code)];
	}

	// server 이름 추가
	res.attr["Server"] = "Semtax 0.1";

	// 파일 size를 Content-Length 필드에 추가
	if(res.code == "200" || res.code == "206")
		res.attr["Content-Length"] = to_string(length);
	
	
	// partial-content processing part
	// Partial Content 와 같은 경우 Content-Range헤더에 시작위치-끝 위치를 지정해 주어야 함
	// 이를 위해 make_range함수를 생성하여 해당부분을 처리하였음
	// start-,와 start-end 의 case만 처리함 (start-end, start-end, start- 와 같은 경우는에러로 처리)
	if(res.code == "206")
	{
		string r = req.attr["Range"];
		auto range = make_range(r,1024);
		if(range.size() == 1)
		{
			res.attr["Content-Range"] = "bytes " + to_string(range[0].first) + "-" + to_string(range[0].second) + "/" + res.attr["Content-Length"];
		}
		else
		{
			res.code = "200";
			res.status = status[stoi(res.code)];
		}
	}
	
	// content-type decision
	// 확장자를 기준으로 판정함..
	// File Signature를 기준으로 검사하는 방식으로 개선 가능하다고 판단하였지만, 시간부족으로 구현하지 못함

	if(res.code == "200" || res.code == "206")
	{
		if(req.uri.path.find(".css")!=string::npos)
			res.attr["Content-Type"] = content_type[".css"];
		else if(req.uri.path.find(".js")!=string::npos)
			res.attr["Content-Type"] = content_type[".js"];
		else if(req.uri.path.find(".html")!=string::npos)
			res.attr["Content-Type"] = content_type[".html"];
		else if(req.uri.path.find(".mp3")!=string::npos)
			res.attr["Content-Type"] = content_type[".mp3"];
		else if(req.uri.path.find(".mp4")!=string::npos)
			res.attr["Content-Type"] = content_type[".mp4"];
		else if(req.uri.path.find(".pdf")!=string::npos)
			res.attr["Content-Type"] = content_type[".pdf"];
		else if(req.uri.path.find(".png")!=string::npos)
			res.attr["Content-Type"] = content_type[".png"];
		else if(req.uri.path.find(".jpg")!=string::npos)
			res.attr["Content-Type"] = content_type[".jpg"];
		else if(req.uri.path.find(".gif")!=string::npos)
			res.attr["Content-Type"] = content_type[".gif"];
		else
			res.attr["Content-Type"] = content_type["etc"];
	}
	else
		res.attr["Content-Type"] = content_type[".html"];

	return {res,fd};
}


// HTTP Response를구조체를 소켓으로 보내기 위한 직렬화 함수
string HTTPResponse::serialize()
{
	string header = "";
	
	header += version;
	header += " ";
	header += code;
	header += " ";
	header += status;
	header += "\r\n";

	for(auto &attribute : attr){
		header += attribute.first + ": " + attribute.second + "\r\n";
	}

	header += "\r\n";

	return header;
}

int getFileSize(string filename)
{
	struct stat st;
	stat(filename.c_str(),&st);
	return st.st_size;
}

string consumeSpace(string &str)
{
	int pos,len;
	len = str.size();
	for(pos = 0;pos<len;pos++)
		if(str[pos] != '\t' && str[pos] != ' ')
			break;
	
	return str.substr(pos);
}

vector<string> split(string H,string N)
{
	vector<string> container;
	int pos = -1;
	int count = 0;

	while( (pos = H.find(N)) != string::npos)
	{
		container.push_back(H.substr(0,pos));
		H = H.substr(pos+N.size());
		count++;
	}

	if(count > 0)
		container.push_back(H);

	return container;
}

// range field 생성함수
vector<pair<int,int>> make_range(string input,int end)
{
	string unit,value;
	vector<string> range_container;
	vector<pair<int,int>> result;
	
	auto range_pair = split(input,"=");
	
	if(range_pair.size() != 2)
		return result;

	unit = range_pair[0];
	value = range_pair[1];

	range_container = split(value,", ");

	if(range_container.size()==0)
	{
		auto t = split(value,"-");

		if(t.size()==2){
			if(t[1] == "")
				result.push_back({stoi(t[0]),stoi(t[0])+end});
			else
				result.push_back({stoi(t[0]),stoi(t[1])});
		}
	}
	else
	{
		for(auto &it : range_container)
		{
			auto t2 = split(it,"-");
			if(t2.size()==2)
			{
				if(t2[1] == "")
					result.push_back({stoi(t2[0]),stoi(t2[0])+end});
				else
					result.push_back({stoi(t2[0]),stoi(t2[1])});
			}
		}
	}

	return result;
}


void errorHandling(char *message)
{
	fputs(message,stderr);
	fputc('\n',stderr);
	exit(1);
}

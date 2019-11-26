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

struct URI{
	string scheme;
	string authority;
	string url;
	string path;
	string param;
	string hash;
	string errorCode;
	int len;
};

struct HTTPRequest{
	string method;
	URI uri;
	string version;
	map<string,string> attr;
	string body;
	string errorCode;
	int len;
};

struct HTTPResponse{
	string version;
	string code;
	string status;
	map<string,string> attr;
	string body;
	string errorCode;

	string serialize();
};

pair<HTTPResponse,int> makeResponse(HTTPRequest req);
HTTPRequest parseHeader(string buffer);
URI parsing_uri(string url);

int getFileSize(string filename);
string consumeSpace(string &str);

void errorHandling(char *message);
void clientConnection(int arg);

unordered_map<int,int> socktable;
mutex mtx_lock;

const int BUFSIZE = 1024;
vector<const char *> methods = {"GET","POST"};
vector<const char *> status(600); 
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
	content_type[".mp4"] = "video/mp4";
	content_type[".pdf"] = "application/pdf";
	content_type[".png"] = "image/png";
	content_type[".jpg"] = "image/jpeg";
	content_type[".gif"] = "image/gif";
	content_type["etc"] = "text/html";

	signal(SIGPIPE, SIG_IGN);

	while(true)
	{
		clnt_addr_size = sizeof(clnt_addr);
		clnt_sock = accept(serv_sock,(struct sockaddr *)&clnt_addr,(unsigned int *)&clnt_addr_size);
		
		printf("SOCK : %d\n",clnt_sock);

		mtx_lock.lock();
		socktable.insert({clnt_sock,clnt_sock});
		mtx_lock.unlock();

		routine = thread(clientConnection,clnt_sock);
		
		if(routine.joinable())
			routine.detach();
	}

	return 0;
}


void clientConnection(int arg)
{
	int clnt_sock = arg;
	int length = 0;
	int total_length;
	int remain = 0;
	char chunk[BUFSIZE];
	bool flag = false;

	string buffer = "";


	while( (length = read(clnt_sock,chunk,BUFSIZE)) )
	{
		buffer += string(chunk,length);
		if((remain = buffer.find("\r\n\r\n")) != string::npos)
		{
			cout << buffer.size() << " " << remain << endl;
			remain = buffer.size() - remain - 4;
			break;
		}
	}

	auto req = parseHeader(buffer);
	
	req.body = "";
	req.body += buffer.substr(buffer.size()-remain);

	
	if(req.attr.count("Content-Length") == 1)
	{
		length = 0;
		size_t total = stoi(req.attr["Content-Length"]) >= remain ? stoi(req.attr["Content-Length"]) - remain : 0;
		for(size_t size_to_recv = total; size_to_recv > 0; )
		{
			length = read(clnt_sock,chunk,BUFSIZE);
			req.body += string(chunk,length);
			buffer += string(chunk,length);
			size_to_recv -= length;
		}
	}

	auto tmp = makeResponse(req);

	auto res = tmp.first;
	auto fd_send = tmp.second;

	string header = res.serialize();

	write(clnt_sock,header.c_str(),header.size());
	
	if(res.code != "200")
	{
		string data = "";
		data += "<h1>ERROR!</h1>";
		write(clnt_sock,data.c_str(),data.size());
	}
	else
	{
		off_t offset = 0;
		size_t total_size = stoi(res.attr["Content-Length"]);
		for(size_t size_to_send = total_size; size_to_send > 0; )
		{
			ssize_t sent = sendfile(clnt_sock,fd_send,&offset,total_size);
			if(sent <= 0)
			{
				if(sent !=0){
					flag = true;
					close(clnt_sock);
				}
				break;
			}
			offset += sent;
			size_to_send -= sent;
		}
		close(fd_send);
	}

	/*	
	cout << "method" << " => " << req.method << endl;
	cout << "scheme => " << req.uri.scheme << endl;
	cout << "authority => " << req.uri.authority << endl;
	cout << "url => " << req.uri.url << endl;
	cout << "path => " << req.uri.path << endl;
	cout << "param => " << req.uri.param << endl;
	cout << "hash => " << req.uri.hash << endl;
	cout << "errorCode => " << req.uri.errorCode << endl;
	cout << "version" << " => " << req.version << endl;
	*/

	//for(auto &p : req.attr)
	//	cout << p.first << " => " << p.second << endl;
	

	// test routine :D

	mtx_lock.lock();
	if(socktable.count(clnt_sock)>0)
		socktable.erase(clnt_sock);
	mtx_lock.unlock();

	if(flag == false)
		close(clnt_sock);
}


HTTPRequest parseHeader(string buffer)
{
	HTTPRequest req;
	string header = "",body = "";
	int pos;

	req.len = buffer.size();

	pos = buffer.find("\r\n\r\n");
	header = buffer.substr(0,pos+2);

	pos = header.find("\r\n");
	auto line = header.substr(0,pos);

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


	if(line[0] != ' '){
		req.errorCode = "ERROR";
		return req;
	}

	line = line.substr(1);


	// TODO : replace this part uri parser 
	line = consumeSpace(line);
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
	
	auto version = line.substr(0,8);

	if(version != "HTTP/1.1" && version != "HTTP/1.0")
	{
		cout << "version : " << version << endl;
		req.errorCode = "ERROR";
		return req;
	}

	req.version = version;

	header = header.substr(pos+2);
	while((pos = header.find("\r\n")) != string::npos)
	{
		auto attr = header.substr(0,pos);
		auto key_pos = attr.find(": ");
		
		auto key = attr.substr(0,key_pos);
		auto value = attr.substr(key_pos+2);

		req.attr[key] = value;

		header = header.substr(pos+2);
	}


	/*
	cout << "method" << " => " << req.method << endl;
	cout << "uri" << " => " << req.uri << endl;
	cout << "version" << " => " << req.version << endl;
	
	for(auto &p : req.attr)
		cout << p.first << " => " << p.second << endl;
	*/


	req.errorCode = "OK";
	return req;
}

URI parsing_uri(string url)
{
	URI uri = {"http","","","","","",""};
	regex pattern("(\\w+:)?(\\/)+[^\\s]+");
	smatch matched;
	string parsed_url;
	int pos = 0;

	regex_match(url,matched,pattern);

	auto it = matched.begin();
	if(it != matched.end()){
		parsed_url = *it;
		uri.len = parsed_url.size();
	}
	else{
		uri.errorCode = "ERROR";
		//cout << "PARSED_URL : " << parsed_url << endl;
		return uri;
	}


	// TODO : refactoring following part :D

	pos = parsed_url.find("://");
	if(pos != string::npos){
		uri.scheme = parsed_url.substr(0,pos);
		parsed_url = parsed_url.substr(pos+3);
	}
	
	pos = parsed_url.find("@");
	if(pos != string::npos){
		uri.authority = parsed_url.substr(0,pos);
		parsed_url = parsed_url.substr(pos+1);
	}

	pos = parsed_url.find("#");
	if(pos != string::npos){
		uri.hash = parsed_url.substr(pos+1);
		parsed_url = parsed_url.substr(0,pos);
	}
	
	pos = parsed_url.find("?");
	if(pos != string::npos){
		uri.param = parsed_url.substr(pos+1);
		parsed_url = parsed_url.substr(0,pos);
	}

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

pair<HTTPResponse,int> makeResponse(HTTPRequest req)
{
	HTTPResponse res;
	int fd=-1,length;
	string unit,range;
	vector<pair<int,int>> range_container;
	string line;

	res.version = "HTTP/1.1";
	res.code = "200";
	res.status = status[stoi(res.code)];

	if(req.errorCode == "ERROR" || req.uri.scheme != "http")
	{
		res.code = "400";
		res.status = status[stoi(res.code)];
	}

	if(req.body.size() > 0 && req.attr.count("Content-Length") == 0)
	{
		res.code = "400";
		res.status = status[stoi(res.code)];
	}

	if(req.attr.count("Content-Length") != 0 && req.body.size() > stoi(req.attr["Content-Length"]))
	{
		res.code = "400";
		res.status = status[stoi(res.code)];
	}

	if(req.uri.len > 1024)
	{
		res.code = "414";
		res.status = status[stoi(res.code)];
	}

	if(!(req.method == "GET" || req.method == "POST"))
	{
		res.code = "501";
		res.status = status[stoi(res.code)];
	}

	if(req.uri.path.size() > 1 && access(req.uri.path.substr(1).c_str(),R_OK) == 0){
		fd = open(req.uri.path.substr(1).c_str(),O_RDONLY);
		length = getFileSize(req.uri.path.substr(1));
	}
	
	if(req.uri.path == "/"){
		fd = open("./index.html",O_RDONLY);
		length = getFileSize("./index.html");
	}

	if(fd < 0)
	{
		res.code = "404";
		res.status = status[stoi(res.code)];
	}

	res.attr["Server"] = "Semtax 0.1";
	if(res.code == "200" || res.code == "206")
		res.attr["Content-Length"] = to_string(length);
	
	res.attr["Connection"] = "keep-alive";



	// content-type decision

	if(res.code == "200" || res.code = "206")
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

	//cout << "Serialized : " << header << endl;

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

void errorHandling(char *message)
{
	fputs(message,stderr);
	fputc('\n',stderr);
	exit(1);
}

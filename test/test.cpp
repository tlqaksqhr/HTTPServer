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
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
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
};

vector<string> split(string H,string N);

URI parsing_uri(string url)
{
	URI uri = {"http","","","","","",""};
	regex pattern("(\\w+:)?(\\/)+[^\\s]+");
	smatch matched;
	string parsed_url;
	int pos = 0;

	regex_match(url,matched,pattern);

	auto it = matched.begin();
	if(it != matched.end())
		parsed_url = *it;
	else{
		uri.errorCode = "ERROR";
		cout << "PARSED_URL : " << parsed_url << endl;
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

vector<pair<int,int>> make_range(string input,int max_value)
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
				result.push_back({stoi(t[0]),max_value});
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
					result.push_back({stoi(t2[0]),max_value});
				else
					result.push_back({stoi(t2[0]),stoi(t2[1])});
			}
		}
	}

	return result;
}


int main(int argc,char **argv)
{
	string input;
	getline(cin,input);

	auto cont = make_range(input,1000);

	for(auto &p : cont)
		cout << p.first << " " << p.second << endl;

	/*
	int fd;
	char buf[1024];

	URI uri;
	string test;
	cin >> test

	uri = parsing_uri(test);

	cout << "scheme : " << uri.scheme << endl;
	cout << "authority : " << uri.authority << endl;
	cout << "url : " << uri.url << endl;
	cout << "path : " << uri.path << endl;
	cout << "param : " << uri.param << endl;
	cout << "hash : " << uri.hash << endl;
	cout << "errorCode : " << uri.errorCode << endl;
	*/

	return 0;
}

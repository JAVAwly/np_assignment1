#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>
/* You will to add includes here */
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <vector>
#include <map>
#include <algorithm>
#include <netdb.h>
#include<iostream>
// Enable if you want debugging to be printed, see examble below.
// Alternative, pass CFLAGS=-DDEBUG to make, make CFLAGS=-DDEBUG
#define DNS_SERVER_PORT		53
#define DNS_SERVER_IP		"114.114.114.114"

#define DNS_HOST			0x01
#define DNS_CNAME			0x05
#define DEBUG

// Included to get the support library
#include "calcLib.h"
using std::count;
using std::map;
using std::string;
using std::vector;
using std::cout;
using std::endl;

struct dns_header {

	unsigned short id;			//会话标识
	unsigned short flags;		//标志

	unsigned short questions;	//问题数
	unsigned short answer;		//回答 资源记录数

	unsigned short authority;	//授权 资源记录数
	unsigned short additional;	//附加 资源记录数

};


struct dns_queries {

	int length;
	unsigned short qtype;
	unsigned short qclass;
	unsigned char* name;

};


struct dns_item {

	char* domain;
	char* ip;

};


//client sendto dns server


int dns_create_header(struct dns_header* header) {

	if (header == NULL)return -1;
	memset(header, 0, sizeof(struct dns_header));

	//random
	srandom(time(NULL));
	header->id = random();

	header->flags = htons(0x0100);//转化成网络字节序
	header->questions = htons(1);

}


//hostname:  www.baidu.com

//name:		3www5baidu3com0

int dns_create_queries(struct dns_queries* question, const char* hostname) {

	if (question == NULL || hostname == NULL)return -1;
	memset(question, 0, sizeof(struct dns_queries));

	question->name = (unsigned char*)malloc(strlen(hostname) + 2);
	if (question->name == NULL) {
		return -2;
	}

	question->length = strlen(hostname) + 2;
	
	question->qtype = htons(1);
	question->qclass = htons(1);


	const char delim[2] = ".";
	char* qname = (char*)question->name;

	char* hostname_dup = strdup(hostname); //strdup -->malloc
	char* token = strtok(hostname_dup, delim);

	while (token != NULL) {

		size_t len = strlen(token);

		*qname = len;
		qname++;

		strncpy(qname, token, len + 1);
		qname += len;

		token = strtok(NULL, delim);

	}

	free(hostname_dup);

}


int dns_build_request(struct dns_header* header, struct dns_queries* question, char* request,int rlen) {

	if (header == NULL || question == NULL || request == NULL)return -1;

	int offset = 0;

	memset(request, 0, rlen);

	memcpy(request, header, sizeof(struct dns_header));
	offset = sizeof(struct dns_header);

	memcpy(request + offset, question->name, question->length);
	offset += question->length;

	memcpy(request + offset, &question->qtype, sizeof(question->qtype));
	offset += sizeof(question->qtype);

	memcpy(request + offset, &question->qclass, sizeof(question->qclass));
	offset += sizeof(question->qclass);

	return offset;

}


static int is_pointer(int in) {
	return ((in & 0xC0) == 0xC0);
}


static void dns_parse_name(unsigned char* chunk, unsigned char* ptr, char* out, int* len) {

	int flag = 0, n = 0, alen = 0;
	char* pos = out + (*len);

	while (1) {

		flag = (int)ptr[0];
		if (flag == 0) break;

		if (is_pointer(flag)) {

			n = (int)ptr[1];
			ptr = chunk + n;
			dns_parse_name(chunk, ptr, out, len);
			break;

		}
		else {

			ptr++;
			memcpy(pos, ptr, flag);
			pos += flag;
			ptr += flag;

			*len += flag;
			if ((int)ptr[0] != 0) {
				memcpy(pos, ".", 1);
				pos += 1;
				(*len) += 1;
			}
		}

	}

}


static int dns_parse_response(char* buffer, struct dns_item** domains) {

	int i = 0;
	unsigned char* ptr = (unsigned char*)buffer;

	ptr += 4;
	int querys = ntohs(*(unsigned short*)ptr);

	ptr += 2;
	int answers = ntohs(*(unsigned short*)ptr);

	ptr += 6;
	for (i = 0; i < querys; i++) {
		while (1) {
			int flag = (int)ptr[0];
			ptr += (flag + 1);

			if (flag == 0) break;
		}
		ptr += 4;
	}

	char cname[128], aname[128], ip[20], netip[4];
	int len, type, ttl, datalen;

	int cnt = 0;
	struct dns_item* list = (struct dns_item*)calloc(answers, sizeof(struct dns_item));
	if (list == NULL) {
		return -1;
	}

	for (i = 0; i < answers; i++) {

		bzero(aname, sizeof(aname));
		len = 0;

		dns_parse_name((unsigned char*)buffer, ptr, aname, &len);
		ptr += 2;

		type = htons(*(unsigned short*)ptr);
		ptr += 4;

		ttl = htons(*(unsigned short*)ptr);
		ptr += 4;

		datalen = ntohs(*(unsigned short*)ptr);
		ptr += 2;

		if (type == DNS_CNAME) {

			bzero(cname, sizeof(cname));
			len = 0;
			dns_parse_name((unsigned char*)buffer, ptr, cname, &len);
			ptr += datalen;

		}
		else if (type == DNS_HOST) {

			bzero(ip, sizeof(ip));

			if (datalen == 4) {
				memcpy(netip, ptr, datalen);
				inet_ntop(AF_INET, netip, ip, sizeof(struct sockaddr));

				printf("%s has address %s\n", aname, ip);
				printf("\tTime to live: %d minutes , %d seconds\n", ttl / 60, ttl % 60);

				list[cnt].domain = (char*)calloc(strlen(aname) + 1, 1);
				memcpy(list[cnt].domain, aname, strlen(aname));

				list[cnt].ip = (char*)calloc(strlen(ip) + 1, 1);
				memcpy(list[cnt].ip, ip, strlen(ip));

				cnt++;
			}

			ptr += datalen;
		}
	}

	*domains = list;
	ptr += 2;

	return cnt;

}


char* dns_client_commit(const char* domain) {

	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		return NULL;
	}

	struct sockaddr_in servaddr = { 0 };
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(DNS_SERVER_PORT);
	servaddr.sin_addr.s_addr = inet_addr(DNS_SERVER_IP);

	int ret = connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
	//printf("cooect: %d\n", ret);

	struct dns_header header = { 0 };
	dns_create_header(&header);

	struct dns_queries question = { 0 };
	dns_create_queries(&question, domain);


	char request[1024] = { 0 };
	int length = dns_build_request(&header, &question, request,1024);

	//request
	int slen = sendto(sockfd, request, length, 0, (struct sockaddr*)&servaddr, sizeof(struct sockaddr));

	//recvfrom
	char response[1024] = { 0 };
	struct sockaddr_in addr = { 0 };
	size_t addr_len = sizeof(struct sockaddr_in);


	int n = recvfrom(sockfd, response, sizeof(response), 0, (struct sockaddr*)&addr, (socklen_t*)&addr_len);

	//printf("recvfrom: %d,%s\n", n, response);

	struct dns_item* dns_domain = NULL;
	dns_parse_response(response, &dns_domain);
  if(dns_domain->ip == NULL){
    printf("Connected to:127.0.0.1\n");
  }else{
    printf("Connected to:%s\n",dns_domain->ip);
  }
  
	free(dns_domain);
  return dns_domain->ip;
	

	//return n;
}

int main(int argc, char *argv[])
{
  setbuf(stdout, NULL); // present the printf content

  /*
    Read first input, assumes <ip>:<port> syntax, convert into one string (Desthost) and one integer (port).
     Atm, works only on dotted notation, i.e. IPv4 and DNS. IPv6 does not work if its using ':'.
  */
  char delim[] = ":";
  char *Desthost = strtok(argv[1], delim);
  char *Destport = strtok(NULL, delim);
  // *Desthost now points to a sting holding whatever came before the delimiter, ':'.
  // *Dstport points to whatever string came after the delimiter.

  /* Do magic */
  int port = atoi(Destport);
#ifdef DEBUG
  printf("Host %s, and port %d.\n", Desthost, port);
#endif
  //DNS parse
  char* ip = dns_client_commit(Desthost);
  // the former is to output the host and port
  // begin to connect to the server and continue the task
  int socket_id;                      // the socket id
  struct sockaddr_in server;          // the server struct
  memset(&server, 0, sizeof(server)); // init the server
  // create the socket
  if ((socket_id = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    printf("create socket failed:\n", strerror(errno));
    return 1;
  }
  // assign the server
  server.sin_family = AF_INET;
  server.sin_port = htons(port);
  server.sin_addr.s_addr = inet_addr(ip);

  printf("the client tries to connect the server...\n");

  // begin to connect
  if (connect(socket_id, (struct sockaddr *)&server, sizeof(server)) < 0)
  {
    printf("connect failed!\n");
    return 1;
  }
  socklen_t addrlen = sizeof(server);
    if (getsockname(socket_id, reinterpret_cast<sockaddr*>(&server), &addrlen) == -1) {
        perror("getsockname error");
        close(socket_id);
        return 1;
    }

  cout << "Local port: " << ntohs(server.sin_port) << endl;

  // begin to start the task
  char sendbf[1024]; // send buffer to send data to server
  char recvbf[1024]; // receive buffer to get data from server

  // get the protocol supported from server and verify acceptance
  int first_recv_len;
  if ((first_recv_len = recv(socket_id, recvbf, 1024, 0)) < 0)
  {
    printf("recv error!\n");
    return 1;
  }
  printf("%s", recvbf);
  recvbf[first_recv_len] = '\0'; // the symbol of string end
  // printf("Received:%s", recvbf);

  // 1. dispose the protocol info
  // 1.1 add the supported protocol of client
  vector<string> support_proto;
  support_proto.push_back("TCP");
  support_proto.push_back("UDP");
  // for(int i = 0; i<support_proto.size();i++){
  //   printf("%s\n",support_proto[i].c_str());
  // }
  // 1.2 strtok the protocol info from server
  // support_proto.push_back("SSS");
  bool is_support_all = true;
  const char *token = strtok(recvbf, "\n");
  while (token != NULL)
  {
    char protocol[3];
    memcpy(protocol, token + 5, 3);
    if (count(support_proto.begin(), support_proto.end(), string(protocol)))
    {
      token = strtok(NULL, "\n");
    }
    else
    {
      is_support_all = false;
      break;
    }
  }

  memset(recvbf, (int)'\0', 1024); // clear the recvbf
  // if the client supports all the protocols
  if (is_support_all)
  {
    // 1.3 send the response ok\n
    printf("OK\n");
    strcpy(sendbf, "OK\n");
    send(socket_id, sendbf, 3, 0);
    // printf("----------------------\n");
    //  2. calculate the next two number
    // printf("-----the second recv data----\n");
    int second_recv_len = 0;
    second_recv_len = recv(socket_id, recvbf, 1024, 0);
    recvbf[second_recv_len] = '\0';
    // 2.1 strtok the operation
    string operation(recvbf);
    operation = operation.substr(0, operation.size() - 1);
    printf("%s\n", operation.c_str());
    const char *operand = strtok((char *)operation.c_str(), " ");
    char *value1_str = strtok(NULL, " ");
    char *value2_str = strtok(NULL, " ");

    // clear the last send buffer
    memset(sendbf, (int)'\0', 1024);
    // use map to finish the swtich case statement
    // add/div/mul/sub
    map<string, int> integer_map;
    integer_map["add"] = 1;
    integer_map["div"] = 2;
    integer_map["mul"] = 3;
    integer_map["sub"] = 4;

    map<string, int> float_map;
    float_map["fadd"] = 1;
    float_map["fdiv"] = 2;
    float_map["fmul"] = 3;
    float_map["fsub"] = 4;
    // For floating-point use "%8.8g "for the formatting
    if (operand[0] == 'f')
    {

      float value1 = (float)atof(value1_str);
      float value2 = (float)atof(value2_str);
      float result = 0;
      int operand_hash = float_map[string(operand)];
      switch (operand_hash)
      {
      case 1:
        result = value1 + value2;
        break;
      case 2:
        result = value1 / value2;
        break;
      case 3:
        result = value1 * value2;
        break;
      case 4:
        result = value1 - value2;
        break;
      }
      printf("%8.8g\n", result);
      sprintf(sendbf, "%8.8g\n", result);
      send(socket_id, sendbf, strlen(sendbf), 0);
    }
    // •	add/div/mul/sub
    else
    {
      int value1 = atoi(value1_str);
      int value2 = atoi(value2_str);
      int result = 0;
      int operand_hash = integer_map[string(operand)];
      switch (operand_hash)
      {
      case 1:
        result = value1 + value2;
        break;
      case 2:
        result = value1 / value2;
        break;
      case 3:
        result = value1 * value2;
        break;
      case 4:
        result = value1 - value2;
        break;
      }
      printf("%d\n", result);
      sprintf(sendbf, "%d\n", result);
      send(socket_id, sendbf, strlen(sendbf), 0);
    }

    // 3.the third recv--present the server response to the result
    memset(recvbf, (int)'\0', 1024); // clear the recvbf
    recv(socket_id, recvbf, 1024, 0);
    printf("%s\n", recvbf);
  }
  else
  {
    printf("NOT OK\n");
    close(socket_id);
    return 0;
  }
  close(socket_id);

  return 0;
}
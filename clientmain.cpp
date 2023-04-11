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
// Enable if you want debugging to be printed, see examble below.
// Alternative, pass CFLAGS=-DDEBUG to make, make CFLAGS=-DDEBUG
#define DEBUG

// Included to get the support library
#include "calcLib.h"
using std::count;
using std::string;
using std::vector;
using std::map;


int main(int argc, char *argv[])
{
  setbuf(stdout, NULL);//present the printf content

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
  server.sin_addr.s_addr = inet_addr(Desthost);

  printf("the client tries to connect the server...\n");

  // begin to connect
  if (connect(socket_id, (struct sockaddr *)&server, sizeof(server)) < 0)
  {
    printf("connect failed!\n");
    return 1;
  }

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
  printf("%s",recvbf);
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
    //printf("----------------------\n");
    // 2. calculate the next two number
    //printf("-----the second recv data----\n");
    int second_recv_len = 0;
    second_recv_len = recv(socket_id, recvbf, 1024, 0);
    recvbf[second_recv_len] = '\0';
    // 2.1 strtok the operation
    string operation(recvbf);
    operation = operation.substr(0, operation.size() - 1);
    printf("%s\n",operation.c_str());
    const char *operand = strtok((char *)operation.c_str(), " ");
    char *value1_str = strtok(NULL, " ");
    char *value2_str = strtok(NULL, " ");

    //clear the last send buffer
    memset(sendbf,(int)'\0',1024);
    //use map to finish the swtich case statement
    //add/div/mul/sub
    map<string,int> integer_map;
    integer_map["add"] = 1;
    integer_map["div"] = 2;
    integer_map["mul"] = 3;
    integer_map["sub"] = 4;

    map<string,int> float_map;
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
      printf("%8.8g\n",result);
      sprintf(sendbf,"%8.8g\n",result);
      send(socket_id,sendbf,strlen(sendbf),0);
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
      printf("%d\n",result);
      sprintf(sendbf,"%d\n",result);
      send(socket_id,sendbf,strlen(sendbf),0);
    }

    //3.the third recv--present the server response to the result
    memset(recvbf, (int)'\0', 1024); // clear the recvbf
    recv(socket_id, recvbf, 1024, 0);
    printf("%s\n",recvbf);
  }
  else
  {
    close(socket_id);
    return 0;
  }
  close(socket_id);

  return 0;
}

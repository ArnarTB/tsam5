//
// Simple chat server for TSAM-409
//
// Command line: ./chat_server 4000 
//
// Author: Jacky Mallett (jacky@ru.is)
//
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <vector>
#include <list>
#include <string>

#include <iostream>
#include <sstream>
#include <thread>
#include <map>

#include <unistd.h>

// fix SOCK_NONBLOCK for OSX
#ifndef SOCK_NONBLOCK
#include <fcntl.h>
#define SOCK_NONBLOCK O_NONBLOCK
#endif

#define BACKLOG  5          // Allowed length of queue of waiting connections

// Simple class for handling connections from clients.
//
// Client(int socket) - socket to send/receive traffic from client.
class Client
{
  public:
    int sock;              // socket of client connection
    std::string name;           // Limit length of name of client's user

    Client(int socket) : sock(socket){} 

    ~Client(){}            // Virtual destructor defined for base class
};

class Server
{
  public:
    int sock;              // socket of client connection
    std::string name;           // Limit length of name of client's user
    std::string ip;
    int port;

    Server(int socket) : sock(socket){} 

    ~Server(){}            // Virtual destructor defined for base class
};

struct Message {
    std::string message;
    std::string senderGroupID;
};

// Note: map is not necessarily the most efficient method to use here,
// especially for a server with large numbers of simulataneous connections,
// where performance is also expected to be an issue.
//
// Quite often a simple array can be used as a lookup table, 
// (indexed on socket no.) sacrificing memory for speed.

// list of Messages
std::map<std::string, std::list<Message>> messages;

std::map<int, Client*> clients; // Lookup table for per Client information

std::map<int, Server*> servers; // Lookup table for per Client information

// Get IP address
std::string ipAddress; // Declare a global variable to store the IP address
int clientPort;
int serverPort;

std::string getSpecificIPAddress(const std::string& interfaceName) {
    struct ifaddrs *myaddrs, *ifa;
    void *in_addr;
    char buf[64];

    if (getifaddrs(&myaddrs) != 0) {
        perror("getifaddrs");
        exit(1);
    }

    for (ifa = myaddrs; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr)
            continue;
        if (!(ifa->ifa_flags & IFF_UP))
            continue;

        if (ifa->ifa_name == interfaceName && ifa->ifa_addr->sa_family == AF_INET) {
            in_addr = &((struct sockaddr_in *) ifa->ifa_addr)->sin_addr;
            if (inet_ntop(ifa->ifa_addr->sa_family, in_addr, buf, sizeof(buf))) {
                freeifaddrs(myaddrs);
                return std::string(buf);
            }
        }
    }

    freeifaddrs(myaddrs);
    return ""; // Return empty string if specified interface or IPv4 address not found
}



// Open socket for specified port.
//
// Returns -1 if unable to create the socket for any reason.

int open_socket(int portno, fd_set *openSockets, int *maxfds)
{
   struct sockaddr_in sk_addr;   // address settings for bind()
   int sock;                     // socket opened for this port
   int set = 1;                  // for setsockopt

   // Create socket for connection. Set to be non-blocking, so recv will
   // return immediately if there isn't anything waiting to be read.
    
   if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
   {
      perror("Failed to open socket");
      return(-1);
   }


   // Turn on SO_REUSEADDR to allow socket to be quickly reused after 
   // program exit.


   set = 1;

   if(setsockopt(sock, SOL_SOCKET, SOCK_NONBLOCK, &set, sizeof(set)) < 0)
   {
     perror("Failed to set SOCK_NOBBLOCK");
   }

   memset(&sk_addr, 0, sizeof(sk_addr));

   sk_addr.sin_family      = AF_INET;
   sk_addr.sin_addr.s_addr = INADDR_ANY;
   sk_addr.sin_port        = htons(portno);

   // Bind to socket to listen for connections from clients

   if(bind(sock, (struct sockaddr *)&sk_addr, sizeof(sk_addr)) < 0)
   {
      perror("Failed to bind to socket:");
      return(-1);
   }
   else
   {
    if(listen(sock, BACKLOG) < 0)
    {
        printf("Listen failed on port %i\n", portno);
        exit(0);
    }
        FD_ZERO(openSockets);
        FD_SET(sock, openSockets);
        *maxfds = sock;
    
      return(sock);
   }
}

// Close a client's connection, remove it from the client list, and
// tidy up select sockets afterwards.

void printFDS(const fd_set *set, int maxfds)
{
    std::cout << "Sockets in fd_set: ";
    for(int i = 0; i <= maxfds; ++i)
    {
        if(FD_ISSET(i, set))
        {
            std::cout << i << " ";
        }
    }
    std::cout << std::endl;
}


void closeClient(int clientSocket, fd_set *openClientSockets, int *maxfds)
{

     printf("Client closed connection: %d\n", clientSocket);

     // If this client's socket is maxfds then the next lowest
     // one has to be determined. Socket fd's can be reused by the Kernel,
     // so there aren't any nice ways to do this.

    printFDS(openClientSockets, *maxfds);
     close(clientSocket);      

     if(*maxfds == clientSocket)
     {
        for(auto const& p : clients)
        {
            *maxfds = std::max(*maxfds, p.second->sock);
        }
     }

     // And remove from the list of open sockets.

     FD_CLR(clientSocket, openClientSockets);
         printFDS(openClientSockets, *maxfds);


}
std::string packageMessage(std::string &message)
{   
    const char STX = 0x02; // Start of Text
    const char ETX = 0x03; // End of Text
    const char DLE = 0x10; // Data Link Escape for byte-stuffing

    std::string packagedMessage;
    packagedMessage += STX; // Start of message

    for(char c : message) {
        if(c == STX || c == ETX || c == DLE) {
            packagedMessage += DLE; // Byte-stuffing
        }
        packagedMessage += c;
    }

    packagedMessage += ETX; // End of message

    return packagedMessage;
}

std::vector<std::string> decodeMessage(const std::string &packagedMessage)
{
    // if (packagedMessage.size() < 2) // To ensure there's at least STX and ETX
    //     return "";
    
    // return packagedMessage.substr(1, packagedMessage.size() - 2);

        // char buffer1[5000];  // Ensure the buffer size is adequate
        // strcpy(buffer1, booja.c_str());

        // //std::cout << buffer << std::endl;
        // serverCommand(server->sock, &openClientSockets, &openServerSockets, &clientMaxfds, &serverMaxfds, buffer1);

    const char STX = 0x02; // Start of Text
    const char ETX = 0x03; // End of Text
    
    std::vector<std::string> messages;
    size_t start = 0, end = 0;

    while(start < packagedMessage.size())
    {
        // Find the start and end of a message
        start = packagedMessage.find(STX, start);
        end = packagedMessage.find(ETX, start);

        // If we can't find both delimiters, break out of the loop
        if(start == std::string::npos || end == std::string::npos) 
            break;

        // Extract the message between the delimiters and add to the messages vector
        messages.push_back(packagedMessage.substr(start + 1, end - start - 1));

        // Move start past the current message for the next iteration
        start = end + 1;
    }

    //return packagedMessage.substr(1, packagedMessage.size() - 2);
    return messages;
}

void serverMessage(int socket, std::string message){
    message = packageMessage(message);
    std::cout << std::endl;
    std::cout << "sending server message" << std::endl;
    std::cout << message << std::endl;
    send(socket, message.c_str(), message.length(), 0);
}

std::string queryServerString()
{
    // get ip and port from serve
    return "QUERYSERVERS,P3_GROUP_90," + ipAddress + "," + std::to_string(serverPort) + ";";
}
std::string serverString(std::string msg)
{   
    //std::string msg = "SERVERS,P3_GROUP_57,127.0.0.1,4070;";
    for(auto const& pair : servers)
    {
        msg += pair.second->name + "," + pair.second->ip + "," + std::to_string(pair.second->port) + ";";
    }
    return msg;
}

void serverCommand(int serverSocket, fd_set *openClientSockets, fd_set *openServerSockets, int *clientMaxfds, int *serverMaxfds,
                  char *buffer) 
 {
//   std::vector<std::string> tokens;
//   std::string token;

//   // Split command from client into tokens for parsing
//   std::stringstream stream(buffer);

    std::vector<std::string> tokens;
    std::string token;
    std::cout <<std::endl;
    std::cout << "received server command" << std::endl;
    std::cout << buffer << std::endl;
    // Split command from client into tokens using comma as delimiter
    std::stringstream stream(buffer);

    while(std::getline(stream, token, ',')){
        tokens.push_back(token);
    }

      if((tokens[0].compare("SERVERS") == 0))
  {     
        servers[serverSocket]->name = tokens[1];
        servers[serverSocket]->ip = tokens[2];
        servers[serverSocket]->port = std::stoi(tokens[3]);
  }
  else if(tokens[0].compare("QUERYSERVERS") == 0)
  {
        std::string msg = "SERVERS,P3_GROUP_90," + ipAddress + "," + std::to_string(serverPort) + ";";
        msg = serverString(msg);
        serverMessage(serverSocket, msg.c_str());
  }
  else if (tokens[0].compare("SEND_MSG") == 0) 
  {
    std::string recipientGroupID = tokens[1];
    std::string senderGroupID = tokens[2];
    std::string message = tokens[3];
    
    std::cout << "Message: " << message << std::endl;
    
    // store message in list of messages
    Message msg;
    msg.message = message;
    msg.senderGroupID = senderGroupID;
    messages[recipientGroupID].push_back(msg);
    std::cout << "Message stored!" << std::endl;

  }

  else if (tokens[0].compare("FETCH_MSGS") == 0) 
  {
    std::string recipientGroupID = tokens[1];
    

    for (auto const& message : messages[recipientGroupID]) {
        std::string msg = "SEND_MSG," + recipientGroupID + "," + message.senderGroupID + "," + message.message + ";";
        serverMessage(serverSocket, msg.c_str());
    }
    messages[recipientGroupID].clear();
    std::cout << "Messages sent!" << std::endl;

  }
}
// Process command from client on the server

void clientCommand(int clientSocket, fd_set *openClientSockets, fd_set *openServerSockets, int *clientMaxfds, int *serverMaxfds,
                  char *buffer) 
{
  std::vector<std::string> tokens;
  std::string token;
  char boofer[2000];

  // Split command from client into tokens for parsing
  std::stringstream stream(buffer);

  // get first token
    std::getline(stream, token, ',');
    tokens.push_back(token);

  if ((tokens[0].compare("CONNECT") == 0))
  {
     //clients[clientSocket]->name = tokens[1];
        std::getline(stream, token, ',');
        tokens.push_back(token);
        std::getline(stream, token, ',');
        tokens.push_back(token);
        std::string ip = tokens[1];
        int port = std::stoi(tokens[2]);

        // Create a socket for the new server connection
        int newServerSocket = socket(AF_INET, SOCK_STREAM, 0);
        if(newServerSocket < 0)
        {
            perror("Failed to create new socket for server connection");
            return;
        }

        struct sockaddr_in serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        inet_pton(AF_INET, ip.c_str(), &(serverAddr.sin_addr));
        serverAddr.sin_port = htons(port);

        if(connect(newServerSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
        {
            perror("Failed to connect to the specified server");
            close(newServerSocket);
            return;
        }

        // Store this new connection in the clients map

        FD_SET(newServerSocket, openServerSockets);
        *serverMaxfds = std::max(*serverMaxfds, newServerSocket);
        //sleep(1);
        //recv(newServerSocket, boofer, sizeof(boofer), MSG_DONTWAIT);
        //std::cout << boofer << std::endl;

        std::string queries = queryServerString();
        // Inform the client that the connection was successful
        serverMessage(newServerSocket, queries.c_str());
        //std::string msg = "SERVERS,P3_GROUP_57,127.0.0.1,4070;";
        //msg = serverString(msg);
        //serverMessage(newServerSocket, msg.c_str());
        servers[newServerSocket] = new Server(newServerSocket);  // Þarf að færa þetta til að laga það að server skilaboðin séu ekki með tómum upplýsingum
    

  }
  else if(tokens[0].compare("LEAVE") == 0)
  {
      // Close the socket, and leave the socket handling
      // code to deal with tidying up clients etc. when
      // select() detects the OS has torn down the connection.
 
      closeClient(clientSocket, openClientSockets, clientMaxfds);
  }
  else if(tokens[0].compare("SENDMSG") == 0)
  {
        // next token is group id
        std::string groupid;
        std::getline(stream, groupid, ',');
        // rest of the stream is the message
        std::string message;
        std::getline(stream, message);
        
        // store message in list of messages
        Message msg;
        msg.message = message;
        msg.senderGroupID = "P3_GROUP_90";
        messages[groupid].push_back(msg);
        std::cout << "Message stored!" << std::endl;

        // create message to send
        std::string msgToSend = "SEND_MSG," + groupid + ",P3_GROUP_90," + message + ";";
        // find server socket to send message to
        for(auto const& pair : servers)
        {
            if (pair.second->name.compare(groupid) == 0)
            {
                serverMessage(pair.second->sock, msgToSend.c_str());
            } else {
                std::cout << "Server not found! Not connected" << std::endl;
            }
        }
        
  }
  // This is slightly fragile, since it's relying on the order
  // of evaluation of the if statement.
  else if((tokens[0].compare("LISTSERVERS") == 0))
  {
      std::string msg = "SERVERS: ";
      
      msg = serverString(msg);
      send(clientSocket, msg.c_str(), msg.length(), 0);
   }
      
      //send(pair.second->sock, msg.c_str(), msg.length(),0);
  
  else if(tokens[0].compare("MSG") == 0)
  {
      for(auto const& pair : clients)
      {
          if(pair.second->name.compare(tokens[1]) == 0)
          {
              std::string msg;
              for(auto i = tokens.begin()+2;i != tokens.end();i++) 
              {
                  msg += *i + " ";
              }
              send(pair.second->sock, msg.c_str(), msg.length(),0);
          }
      }
  }
  else
  {
      std::cout << "Unknown command from client:" << buffer << std::endl;
  }
     
}

int main(int argc, char* argv[])
{
    std::string interfaceName = "ens192";
    ipAddress = getSpecificIPAddress(interfaceName);
    if (ipAddress.empty()) {
        ipAddress = getSpecificIPAddress("lo0");
    }
    bool finished;
    int listenClientSock;                 // Socket for connections to server
    int listenServerSock;                 // Socket for connections to server
    int clientSock;                 // Socket of connecting client
    int serverSock;                 // Socket of connecting server

    fd_set openServerSockets;             // Current open sockets 
    fd_set readServerSockets;             // Socket list for select()        
    fd_set exceptServerSockets;           // Exception socket list

    fd_set openClientSockets;             // Current open sockets 
    fd_set readClientSockets;             // Socket list for select()        
    fd_set exceptClientSockets;           // Exception socket list

    int serverMaxfds;                     // Passed to select() as max fd in set
    int clientMaxfds;                     // Passed to select() as max fd in set
    struct sockaddr_in client;
    struct sockaddr_in server;

    socklen_t clientLen;
    socklen_t serverLen;

    char buffer[20000];              // buffer for reading from clients

    if(argc != 2)
    {
        printf("Usage: chat_server <ip port>\n");
        exit(0);
    }

    // Setup socket for server to listen to
    listenClientSock = open_socket(atoi(argv[1]), &openClientSockets, &clientMaxfds);
    listenServerSock = open_socket(atoi(argv[1])+1, &openServerSockets, &serverMaxfds);
    

    printf("Listening on port: %d\n", atoi(argv[1]));

    clientPort = atoi(argv[1]);
    serverPort = atoi(argv[1])+1;

    std::cout << "clientPort: " << clientPort << std::endl;
    std::cout << "serverPort: " << serverPort << std::endl;
    std::cout << "ip address: " << ipAddress << std::endl;
    finished = false;

    while(!finished)
    {
        // Get modifiable copy of readServerSockets
        readServerSockets = exceptServerSockets = openServerSockets;
        readClientSockets = exceptClientSockets = openClientSockets;

        memset(buffer, 0, sizeof(buffer));
        // Look at sockets and see which ones have something to be read()
        struct timeval timeout;
        timeout.tv_sec = 1;  // 0 seconds
        timeout.tv_usec = 0; // 0 microseconds
        int m = select(clientMaxfds + 1, &readClientSockets, NULL, &exceptClientSockets, &timeout);
        int n = select(serverMaxfds + 1, &readServerSockets, NULL, &exceptServerSockets, &timeout);
        if(m < 0)
        {
            perror("select failed - closing down\n");
            finished = true;
        }
        else
        {
            // First, accept  any new connections to the server on the listening socket

            if(FD_ISSET(listenClientSock, &readClientSockets))
            {   
                //printFDS(&openClientSockets, clientMaxfds);
                //clientSock = newConnections(listenClientSock, &clientMaxfds, clientSock, &openClientSockets, &client, &clientLen);
                clientSock = accept(listenClientSock, (struct sockaddr *)&client,&clientLen);

                printf("accept***\n");

                FD_SET(clientSock, &openClientSockets);


        // And update the maximum file descriptor
                clientMaxfds = std::max(clientMaxfds, clientSock);

                if(clientSock > 0)
                {
                    clients[clientSock] = new Client(clientSock);// create a new client to store information.
                    printf("Client connected on server: %d\n", clientSock);
                }
                else{
                    printf("Client connection failed\n");
                }
                m--;   // Decrement the number of sockets waiting to be dealt with
            }
            // Now check for commands from clients
            std::list<Client *> disconnectedClients;  
            while(m-- > 0)
            {
               for(auto const& pair : clients)
               {
                  Client *client = pair.second;

                  if(FD_ISSET(client->sock, &readClientSockets))
                  {
                      // recv() == 0 means client has closed connection
                      if(recv(client->sock, buffer, sizeof(buffer), MSG_DONTWAIT) == 0)
                      {
                          disconnectedClients.push_back(client);
                          closeClient(client->sock, &openClientSockets, &clientMaxfds);

                      }
                      // We don't check for -1 (nothing received) because select()
                      // only triggers if there is something on the socket for us.
                      else
                      {
                        std::cout << std::endl;
                        std::cout << "received client command" << std::endl;
                        std::cout << buffer << std::endl;

                        clientCommand(client->sock, &openClientSockets, &openServerSockets, &clientMaxfds, &serverMaxfds, buffer);
                      }
                  }
               }
               // Remove client from the clients list
               for(auto const& c : disconnectedClients)
                  clients.erase(c->sock);
            }
        }
        memset(buffer, 0, sizeof(buffer));

        if(n < 0) //SERVERSTUFF
        {
            perror("select failed - closing down\n");
            finished = true;
        }
        else
        {
            // First, accept  any new connections to the server on the listening socket
            if(FD_ISSET(listenServerSock, &readServerSockets))
            {   
                std::cout << "server connection" << std::endl;
                serverSock = accept(listenServerSock, (struct sockaddr *)&server,&serverLen); //checka að þetta tengist rétt ekki -1
                serverMaxfds = std::max(serverMaxfds, serverSock);

                FD_SET(serverSock, &openServerSockets);
                //serverSock = newConnections(listenServerSock, &serverMaxfds, serverSock, &openServerSockets, &server, &serverLen);
                if(serverSock > 0)
                {
                    servers[serverSock] = new Server(serverSock);// create a new client to store information.
                    printf("server connected on server: %d\n", serverSock);
                    std::string queries = queryServerString();
                    serverMessage(serverSock, queries.c_str()); // Inform the client that the connection was successful

                }
                else{
                    printf("Server connection failed\n");
                }

               n--;// Decrement the number of sockets waiting to be dealt with

            }
            // Now check for commands from clients
            std::list<Server *> disconnectedServers;  
            while(n-- > 0)
            {
               for(auto const& pair : servers)
               {
                  Server *server = pair.second;

                  if(FD_ISSET(server->sock, &readServerSockets))
                  {
                      // recv() == 0 means client has closed connection
                      if(recv(server->sock, buffer, sizeof(buffer), MSG_DONTWAIT) == 0)
                      {
                          disconnectedServers.push_back(server);
                          closeClient(server->sock, &openServerSockets, &serverMaxfds);

                      }
                      // We don't check for -1 (nothing received) because select()
                      // only triggers if there is something on the socket for us.
                      else
                      {   
                        
                            std::vector<std::string> decodedMessages = decodeMessage(std::string(buffer));

                            for(const std::string& msg : decodedMessages)
                            {
                                char buffer1[5000];  // Ensure the buffer size is adequate
                                strcpy(buffer1, msg.c_str());
                                serverCommand(server->sock, &openClientSockets, &openServerSockets, &clientMaxfds, &serverMaxfds, buffer1);
                            }
                            // If you need to copy the content into a separate char array:
                        //     char buffer1[5000];  // Ensure the buffer size is adequate
                        //     strcpy(buffer1, booja.c_str());

                        //   //std::cout << buffer << std::endl;
                        //   serverCommand(server->sock, &openClientSockets, &openServerSockets, &clientMaxfds, &serverMaxfds, buffer1);



                          //clientCommand(server->sock, &openServerSockets, &openServerSockets, &serverMaxfds, &serverMaxfds, buffer);
                      }
                  }
               }
               // Remove client from the clients list
               for(auto const& c : disconnectedServers)
                  servers.erase(c->sock);
            }
        }




    }
}
//CONNECT,130.208.243.61,4002
//CONNECT,130.208.243.61,4003;
//CONNECT,130.208.243.61,4001;
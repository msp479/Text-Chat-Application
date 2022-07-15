/**
 * @text_chat_appl
 * @author  Sai Mahesh <spullagu@buffalo.edu>
 * @version 1.0
 *
 * @section LICENSE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details at
 * http://www.gnu.org/copyleft/gpl.html
 *
 * @section DESCRIPTION
 *
 * This contains the main function. Add further description here....
 */
#include <iostream>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <vector>
#include <sstream>

#include <algorithm>
#include <errno.h>
#include <list>

#include "../include/global.h"
#include "../include/logger.h"

using namespace std;

#define BACKLOG 5
#define STDIN 0
#define TRUE 1
#define CMD_SIZE 300
#define BUFFER_SIZE 256
#define UBIT_NAME "spullagu"
#define HOST_NAME_MAX_SIZE 50
#define PENDING_QUEUE 20

struct message
{
	char text[BUFFER_SIZE];
	char from_ip[INET_ADDRSTRLEN];
	bool is_broadcast;
} messageList;

struct host
{
	char hostname[HOST_NAME_MAX_SIZE];
	char ip_addr[INET_ADDRSTRLEN];
	int port_num;
	int num_msg_sent;
	int num_msg_rcv;
	char status[20];
	int fd;
	char blocked[100];
	struct host *next_host;
	bool is_logged_in;
	bool is_server;
	struct message queued_messages[PENDING_QUEUE];
	int queue_size;
} *hostlisthead = NULL;

// internal communication structure - from client to server
struct internalctos
{
	// send -1 , refresh -2, broadcast -3
	// block - 4, unblock -5, logout -6
	int commandid;
	char ip[INET_ADDRSTRLEN];
	int port;
	char msg[BUFFER_SIZE];
};

struct listitem
{
	char hostname[HOST_NAME_MAX_SIZE];
	char ip[INET_ADDRSTRLEN];
	int port;
};

// internal communication structure - from server to client
struct internalstoc
{
	char message_type[20];
	listitem clientlist[5];
	message pending_msgs[PENDING_QUEUE];
	int queue_size;
	int clientcount;
	char message_text[BUFFER_SIZE];
	char from_ip[INET_ADDRSTRLEN];
};

vector<string> c_blocked_ips;

void OperateInServerMode(int);
void OperateInClientMode(int);
void HandleIPCommand();
void HandlePORTCommand(int);
void HandleBlockedCommand(string);
bool ValidateIPaddress(string);
bool ValidatePortNumber(string);
vector<string> string_split(const string);
void InsertIntoHostLL(host *);
void deleteFromHostLL(string);
int connect_to_host(string, int);
void printHostList();
void SendCommandServerSide(string to_ip, string text, int client_socket);
void BroadcastCommandServerSide(string text, int client_socket);
bool IPCurrentlyInClientList(string ip);
int GetCurrentlyLoggedinList(struct listitem *array, int clientfd);
void OldClientLoginServerSide(string client_ip, int client_socket);
void HandleListCommand();
listitem MapHostObjectToListItemObject(host input);
host *MapListItemObjectToHost(listitem input);
bool IPCurrentlyBlockedByClient(string ip);
void BlockingServerSide(string blockee_ip, int client_socket, bool block);
void HandleStatisticsCommand();

/**
 * main function
 *
 * @param  argc Number of arguments
 * @param  argv The argument list
 * @return 0 EXIT_SUCCESS
 */
int main(int argc, char **argv)
{
	/*Init. Logger*/
	cse4589_init_log(argv[2]);

	/* Clear LOGFILE*/
	fclose(fopen(LOGFILE, "w"));

	/*Start Here*/

	if (!((std::string(argv[1]) == "s" || std::string(argv[1]) == "c") && argc == 3))
	{
		cout << "Wrong Input format. Correct format is <s/c> <port number> ";
		exit(-1);
	}

	if (std::string(argv[1]) == "s")
	{
		OperateInServerMode(atoi(argv[2]));
	}
	else
	{
		OperateInClientMode(atoi(argv[2]));
	}

	return 0;
}

void OperateInServerMode(int port)
{
	int server_socket, head_socket, selret, sock_index, fdaccept = 0, caddr_len;
	struct sockaddr_in client_addr, server_addr;

	fd_set master_list, watch_list;

	bzero((char *)&server_addr, sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (server_socket < 0)
	{
		cerr << "Cannot create socket";
		exit(-1);
	}

	if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		cerr << "Bind failed";
		exit(-1);
	}

	if (listen(server_socket, BACKLOG) < 0)
	{
		cerr << "Unable to listen on port";
		exit(-1);
	}

	FD_ZERO(&master_list);
	FD_ZERO(&watch_list);

	FD_SET(server_socket, &master_list);
	FD_SET(STDIN, &master_list);

	head_socket = server_socket;

	while (1)
	{
		fflush(stdout);
		memcpy(&watch_list, &master_list, sizeof(master_list));

		selret = select(head_socket + 1, &watch_list, NULL, NULL, NULL);
		if (selret < 0)
		{
			cerr << "select failed.";
		}

		/* Check if we have sockets/STDIN to process */
		if (selret > 0)
		{
			/* Loop through socket descriptors to check which ones are ready */
			for (sock_index = 0; sock_index <= head_socket; sock_index += 1)
			{

				if (FD_ISSET(sock_index, &watch_list))
				{

					/* Check if new command on STDIN */
					if (sock_index == STDIN)
					{

						char *cmd = (char *)malloc(sizeof(char) * CMD_SIZE);
						memset(cmd, '\0', CMD_SIZE);
						if (fgets(cmd, CMD_SIZE - 1, stdin) == NULL)
						{
							exit(-1);
						}
						cmd[strlen(cmd) - 1] = '\0';

						
						if (std::string(cmd) == "AUTHOR")
						{
							cse4589_print_and_log("[AUTHOR:SUCCESS]\n");
							cse4589_print_and_log("I, %s, have read and understood the course academic integrity policy.\n", UBIT_NAME);
							cse4589_print_and_log("[AUTHOR:END]\n");
						}
						else if (std::string(cmd) == "IP")
						{
							HandleIPCommand();
							cse4589_print_and_log("[IP:END]\n");
						}
						else if (std::string(cmd) == "PORT")
						{
							HandlePORTCommand(port);
						}
						else if (std::string(cmd) == "LIST")
						{

							cse4589_print_and_log("[LIST:SUCCESS]\n");
							HandleListCommand();
							cse4589_print_and_log("[LIST:END]\n");
						}
						else if (std::string(cmd) == "STATISTICS")
						{
							
							cse4589_print_and_log("[STATISTICS:SUCCESS]\n");
							HandleStatisticsCommand();
							cse4589_print_and_log("[STATISTICS:END]\n");
						}
						else if (std::string(cmd).substr(0, 8) == "BLOCKED ")
						{
							HandleBlockedCommand(std::string(cmd).substr(8, strlen(cmd) - 1));
							cse4589_print_and_log("[BLOCKED:END]\n");
						}
					}
					/* Check if new client is requesting connection */
					else if (sock_index == server_socket)
					{
						// client executed LOGIN
						socklen_t caddr_len = sizeof(client_addr);
						fdaccept = accept(server_socket, (struct sockaddr *)&client_addr, &caddr_len);

						// recv a message from client that has its port number
						int client_port;
						recv(fdaccept, &client_port, sizeof(client_port), 0);

						FD_SET(fdaccept, &master_list);
						if (fdaccept > head_socket)
							head_socket = fdaccept;

						char client_ip[INET_ADDRSTRLEN];
						char client_hostname[256];
						inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
						getnameinfo((struct sockaddr *)&client_addr, caddr_len, client_hostname, sizeof(host), 0, 0, 0);

						// here the logged in client may be a first timer or an old one that previously logged out

						// check if its an old one
						// then udpate its fd and status
						host *current = hostlisthead;
						bool old = false;
						while (current != NULL)
						{
							if (strcmp(current->ip_addr, client_ip) == 0)
							{
								old = true;
								current->fd = fdaccept;
								strcpy(current->status, "logged-in");
								current->is_logged_in = true;
								break;
							}
							current = current->next_host;
						}

						// if its a new one, insert into the list
						if (!old)
						{
							struct host *newclient = (struct host *)malloc(sizeof(host));
							strcpy(newclient->hostname, client_hostname);

							strcpy(newclient->ip_addr, client_ip);

							newclient->port_num = client_port;
							newclient->num_msg_sent = 0;
							newclient->num_msg_rcv = 0;
							strcpy(newclient->status, "logged-in");
							newclient->fd = fdaccept;
							newclient->next_host = NULL;
							newclient->is_logged_in = true;
							newclient->is_server = false;
							newclient->queue_size = 0;
							strcpy(newclient->blocked, "");

							// adding to the server-maintained LL of clients
							InsertIntoHostLL(newclient);
						}

						// construct an object and send to the client

						internalstoc payload;

						// get the list of logged in clients to server
						payload.clientcount = GetCurrentlyLoggedinList(payload.clientlist, fdaccept);
						payload.queue_size = 0;
						strcpy(payload.message_type, "logged-in-list");

						// if its an old one, it might also have queued messages
						if (old)
						{
							if (current->queue_size != 0)
							{
								for (int i = 0; i < current->queue_size; i++)
								{
									payload.pending_msgs[i] = current->queued_messages[i];
								}
								payload.queue_size = current->queue_size;

								// increase the num_recv count for this one after queue is sent back?
							}
						}

						// now send this object to the client
						if (send(fdaccept, &payload, sizeof(payload), 0) <= 0)
						{
							cout << "error in first time login send info";
							cout << strerror(errno);
						}
						else if (old)
						{
							// successfully sent. so , we can clear the queue
							if (current->queue_size != 0)
							{
								current->num_msg_rcv += current->queue_size;
								current->queue_size = 0;
								free(current->queued_messages);
							}
						}
					}
					/* Read from existing clients */
					else
					{
						// existing clients command metadata will be sent via an internal object internalctos
						internalctos recvdmsg;
						memset(&recvdmsg, '\0', sizeof(recvdmsg));
						if (recv(sock_index, &recvdmsg, sizeof(recvdmsg), 0) <= 0)
						{
							close(sock_index);
							FD_CLR(sock_index, &master_list);
						}
						else
						{
							// Process incoming data from existing clients here ...
							// client used send command ,1 - SEND
							if (recvdmsg.commandid == 1)
							{
								SendCommandServerSide(recvdmsg.ip, recvdmsg.msg, sock_index);
							}
							// client used refresh command, 2 - refresh
							else if (recvdmsg.commandid == 2)
							{
								// construct an object with currently logged in clients and send it
								internalstoc payload;
								payload.clientcount = GetCurrentlyLoggedinList(payload.clientlist, sock_index);
								send(sock_index, &payload, sizeof(payload), 0);
							}
							// client used broadcast command, 3 - broadcast
							else if (recvdmsg.commandid == 3)
							{
								BroadcastCommandServerSide(recvdmsg.msg, sock_index);
							}
							// client used block command, 4 - block
							else if (recvdmsg.commandid == 4)
							{

								BlockingServerSide(recvdmsg.ip, sock_index, true);
							}
							// client used unblock command, 5 - unblock
							else if (recvdmsg.commandid == 5)
							{

								BlockingServerSide(recvdmsg.ip, sock_index, false);
							}
							// client used logout command, 6 - logout
							else if (recvdmsg.commandid == 6)
							{
								// locate this host object object in the hostlist and mark it logged out
								host *current = hostlisthead;
								while (current != NULL)
								{
									if (current->fd == sock_index)
									{
										break;
									}
									current = current->next_host;
								}
								if (current != NULL)
								{
									current->is_logged_in = false;
									strcpy(current->status, "logged-out");
									close(sock_index);
									FD_CLR(sock_index, &master_list);
								}
							}

							fflush(stdout);
						}
					}
				}
			}
		}
	}
}

void SendCommandServerSide(string to_ip, string text, int sock_index)
{

	int to_fd = -1;

	// what is the reciever's fd?
	host *current = hostlisthead;
	host *receiver, *sender;
	while (current != NULL)
	{
		if (strcmp(current->ip_addr, to_ip.c_str()) == 0)
		{
			to_fd = current->fd;
			break;
		}
		current = current->next_host;
	}
	receiver = current;

	// check??

	// what is the sending client's ip?
	current = hostlisthead;
	while (current != NULL)
	{
		if (current->fd == sock_index)
		{
			// current->num_msg_sent += 1;
			break;
		}
		current = current->next_host;
	}
	sender = current;
	string from_ip = current->ip_addr;

	// check if sending client is blocked by receiver
	bool isblocked;

	
	if(string(receiver->blocked).find(string(current->ip_addr)) != std::string::npos)
	{

		char status_reply_to_client[20] = "send_complete";
		send(sock_index, status_reply_to_client, sizeof(status_reply_to_client), 0);
		sender->num_msg_sent += 1;
	}
	
	else
	{
		// receiever didnt block sender
		// have to relay the message to receiver

		// receiver is logged-in . send it
		if (receiver->is_logged_in)
		{
			internalstoc message;
			strcpy(message.message_type, "unicast-message");
			strcpy(message.message_text, text.c_str());
			// cout<<"message-text "<<message.message_text;
			strcpy(message.from_ip, sender->ip_addr);
			// send(to_fd, )

			if (send(to_fd, &message, sizeof(message), 0) == sizeof(message))
			{

				// message successfully relayed to recvr
				cse4589_print_and_log("[RELAYED:SUCCESS]\n");
				cse4589_print_and_log("msg from:%s, to:%s\n[msg]:%s\n", from_ip.c_str(), to_ip.c_str(), text.c_str());
				// send status reply to client
				char status_reply_to_client[20] = "send_complete";
				send(sock_index, status_reply_to_client, sizeof(status_reply_to_client), 0);
				cse4589_print_and_log("[RELAYED:END]\n");
				sender->num_msg_sent += 1;
				receiver->num_msg_rcv += 1;
			}
			else
			{
				//  failed
				cse4589_print_and_log("[RELAYED:ERROR]\n");
				char status_reply_to_client[20] = "send_failed";
				send(sock_index, &status_reply_to_client, sizeof(status_reply_to_client), 0);
				cse4589_print_and_log("[RELAYED:END]\n");
			}
		}

		// receiver logged out - store it to send when receiver logs in
		else
		{
			message new_message;
			strcpy(new_message.from_ip, from_ip.c_str());
			new_message.is_broadcast = false;
			strcpy(new_message.text, text.c_str());
			

			// insert in msg queue
			receiver->queued_messages[receiver->queue_size] = new_message;
			receiver->queue_size += 1;


			cse4589_print_and_log("[RELAYED:SUCCESS]\n");
			cse4589_print_and_log("msg from:%s, to:%s\n[msg]:%s\n", from_ip.c_str(), to_ip.c_str(), text.c_str());
			// send status reply to client
			char status_reply_to_client[20] = "send_complete";
			send(sock_index, status_reply_to_client, sizeof(status_reply_to_client), 0);
			cse4589_print_and_log("[RELAYED:END]\n");
		}
	}
}

void OperateInClientMode(int port)
{
	struct sockaddr_in client_addr, server_addr;
	int client_socket, head_socket, selret, sock_index, server_socket=0;
	bool this_client_logged_in = false;

	fd_set master_list, watch_list;

	bzero((char *)&client_addr, sizeof(client_addr));

	client_addr.sin_family = AF_INET;
	client_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	client_addr.sin_port = htons(port);

	client_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (client_socket < 0)
	{
		cerr << "Cannot create socket";
		exit(-1);
	}

	if (bind(client_socket, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0)
	{
		cerr << "Bind failed";
		exit(-1);
	}

	if (listen(client_socket, BACKLOG) < 0)
	{
		cerr << "Unable to listen on port";
		exit(-1);
	}

	FD_ZERO(&master_list);
	FD_ZERO(&watch_list);

	// FD_SET(client_socket, &master_list);
	FD_SET(STDIN, &master_list);

	head_socket = 0;

	while (1)
	{
		fflush(stdout);
		// FD_SET(STDIN, &master_list);
		memcpy(&watch_list, &master_list, sizeof(master_list));
		//
		head_socket = server_socket;
		selret = select(head_socket + 1, &watch_list, NULL, NULL, NULL);
		if (selret < 0)
		{
			cerr << "select failed.";
			exit(-1);
		}

		if (selret > 0)
		{
			for (sock_index = 0; sock_index <= head_socket; sock_index ++)
			{

				if (FD_ISSET(sock_index, &watch_list))
				{
					if (sock_index == STDIN)
					{

						// char cmd[CMD_SIZE];
						char *cmd = (char *)malloc(sizeof(char) * CMD_SIZE);
						memset(cmd, '\0', CMD_SIZE);
						if (fgets(cmd, CMD_SIZE - 1, stdin) == NULL)
						{
							exit(-1);
						}

						cmd[strlen(cmd) - 1] = '\0';

						string commandstring = std::string(cmd);

						if (commandstring == "AUTHOR")
						{
							cse4589_print_and_log("[AUTHOR:SUCCESS]\n");
							cse4589_print_and_log("I, %s, have read and understood the course academic integrity policy.\n", UBIT_NAME);
							cse4589_print_and_log("[AUTHOR:END]\n");
						}
						else if (commandstring == "IP")
						{
							HandleIPCommand();
							cse4589_print_and_log("[IP:END]\n");
						}
						else if (commandstring == "PORT")
						{
							HandlePORTCommand(port);
						}
						else if (commandstring == "LIST" && this_client_logged_in)
						{

							cse4589_print_and_log("[LIST:SUCCESS]\n");
							HandleListCommand();
							cse4589_print_and_log("[LIST:END]\n");
						}
						else if (commandstring.substr(0, 6) == "LOGIN " && !this_client_logged_in)
						{
							{
								vector<string> arguments = string_split(commandstring.substr(6, commandstring.size()));

								if (!(ValidateIPaddress(arguments[0]) && ValidatePortNumber(arguments[1])))
								{
									cse4589_print_and_log("[LOGIN:ERROR]\n");
									cse4589_print_and_log("[LOGIN:END]\n");
								}
							
								else
								{
									string serverip = arguments[0];
									int serverport = atoi(arguments[1].c_str());
									bool has_error = false;

									// have to connect and obtain the server socket
									server_socket = connect_to_host(serverip, serverport);
									if (server_socket == -1)
									{
										has_error = true;
									}
									else
									{
										FD_SET(server_socket, &master_list);
										this_client_logged_in = true;
										if(server_socket > head_socket){
										head_socket = server_socket;
										}
										send(server_socket, &port, sizeof(port), 0);
									}

				

									if (has_error)
									{
										cse4589_print_and_log("[LOGIN:ERROR]\n");
										cse4589_print_and_log("[LOGIN:END]\n");
									}
									else
									{

										// client can expect the server to send an internal msg with logged in list
										// and queued messages
										internalstoc server_reply;

										if (recv(server_socket, &server_reply, sizeof(server_reply), 0) <= 0)
										{
											cout << "error in login command while receiving the server reply";
											has_error = true;
											//  cout << strerror(errno);
										}

										// update the clients logged in list with the received logged in list
										if (server_reply.clientcount != 0)
										{
											free(hostlisthead);
											hostlisthead = NULL;
											for (int i = 0; i < server_reply.clientcount; i++)
											{
												struct host *newnode = (struct host *)malloc(sizeof(host));
												strcpy(newnode->hostname, server_reply.clientlist[i].hostname);
												strcpy(newnode->ip_addr, server_reply.clientlist[i].ip);
												newnode->port_num = server_reply.clientlist[i].port;
												newnode->next_host = NULL;
												newnode->is_logged_in = true;
												InsertIntoHostLL(newnode);
											}
										}

										cse4589_print_and_log("[LOGIN:SUCCESS]\n");
										// if this is not a first time login, then there might be queued msgs
										if (server_reply.queue_size != 0)
										{
											// log events for the received queued msgs
											for (int i = 0; i < server_reply.queue_size; i++)
											{
												cse4589_print_and_log("[RECEIVED:SUCCESS]\n");
												cse4589_print_and_log("msg from:%s\n[msg]:%s\n", server_reply.pending_msgs[i].from_ip, server_reply.pending_msgs[i].text);
												cse4589_print_and_log("[RECEIVED:END]\n");
											}
										}
										// completed the login command
										cse4589_print_and_log("[LOGIN:END]\n");
									}
								}
							}
						}
						else if (commandstring == "REFRESH" && this_client_logged_in)
						{
							// construct internal object
							internalctos send_info;
							// command id 2 indicates refresh command
							send_info.commandid = 2;
							// send the object to server
							send(server_socket, &send_info, sizeof(send_info), 0);

							bool has_error = false;
							// server sends a reply with updated list of logged in clients
							internalstoc server_reply;
							if (recv(server_socket, &server_reply, sizeof(server_reply), 0) <= 0)
							{
								cout << "error in refresh command while receiving the server reply";
								has_error = true;
								// cout << strerror(errno);
							}

							// update the client maintained list with the received list
							if (has_error)
							{
								cse4589_print_and_log("[REFRESH:ERROR]\n");
								cse4589_print_and_log("[REFRESH:END]\n");
							}
							else
							{
								if (server_reply.clientcount != 0)
								{
									free(hostlisthead);
									hostlisthead = NULL;
									for (int i = 0; i < server_reply.clientcount; i++)
									{
										struct host *newnode = (struct host *)malloc(sizeof(host));

										// host output;
										strcpy(newnode->hostname, server_reply.clientlist[i].hostname);
										strcpy(newnode->ip_addr, server_reply.clientlist[i].ip);
										newnode->port_num = server_reply.clientlist[i].port;
										newnode->next_host = NULL;
										newnode->is_logged_in = true;
										// return output;
										InsertIntoHostLL(newnode);
									}

									cse4589_print_and_log("[REFRESH:SUCCESS]\n");
									cse4589_print_and_log("[REFRESH:END]\n");
								}
							}
						}
						else if (commandstring.substr(0, 5) == "SEND " && this_client_logged_in)
						{
							vector<string> arguments = string_split(commandstring.substr(5, commandstring.size()));

							// handling exceptions
							//  if the given address is valid IP string and is in the list of logged in clients
							if (ValidateIPaddress(arguments[0]) && IPCurrentlyInClientList(arguments[0]))
							{
								// ip valid
								string message = commandstring.substr(5 + arguments[0].length() + 1, commandstring.size());
								// construct internal object to indicate server to send a message to this IP
								internalctos send_info;
								// command id 1 indicates send command
								send_info.commandid = 1;
								strcpy(send_info.ip, arguments[0].c_str());
								strcpy(send_info.msg, message.c_str());

								// send this object to server
								send(server_socket, &send_info, sizeof(send_info), 0);

								// after server processes send , it returns a status message
								char status_msg[20];
								recv(server_socket, &status_msg, sizeof(status_msg), 0);
								// cout<<status_msg;
								if (strcmp(status_msg, "send_complete") == 0)
								{
									// send successfully executed (no problem even if the receiver is blocked)
									cse4589_print_and_log("[SEND:SUCCESS]\n");
									cse4589_print_and_log("[SEND:END]\n");
								}
								else
								{
									cse4589_print_and_log("[SEND:ERROR]\n");
									cse4589_print_and_log("[SEND:END]\n");
								}
							}
							// invalid ip
							else
							{
								cse4589_print_and_log("[SEND:ERROR]\n");
								cse4589_print_and_log("[SEND:END]\n");
							}
							fflush(stdout);
						}
						else if (commandstring.substr(0, 10) == "BROADCAST " && this_client_logged_in)
						{
							string message = commandstring.substr(10, commandstring.size());

							// construct internal object to indicate server to send a message to this IP
							internalctos send_info;
							// command id 3 indicates broadcast command
							send_info.commandid = 3;
							// send_info.ip = arguments[0];
							strcpy(send_info.msg, message.c_str());

							// send this object to server
							send(server_socket, &send_info, sizeof(send_info), 0);

							// after server processes broadcast , it returns a status message
							char status_msg[20];
							recv(server_socket, &status_msg, sizeof(status_msg), 0);
							// cout<<status_msg;
							if (strcmp(status_msg, "bcast_complete") == 0)
							{
								// broadcast successfully executed
								cse4589_print_and_log("[BROADCAST:SUCCESS]\n");
								cse4589_print_and_log("[BROADCAST:END]\n");
							}
							else
							{
								cse4589_print_and_log("[BROADCAST:ERROR]\n");
								cse4589_print_and_log("[BROADCAST:END]\n");
							}

							fflush(stdout);
						}
						else if (commandstring.substr(0, 6) == "BLOCK " && this_client_logged_in)
						{
							string blockee_ip = commandstring.substr(6, commandstring.size());

							// handling exceptions
							//  if the given address is valid IP string and is in the list of logged in clients and not blocked
							if (ValidateIPaddress(blockee_ip) && IPCurrentlyInClientList(blockee_ip) && !IPCurrentlyBlockedByClient(blockee_ip))
							{
								// ip valid

								// construct internal object
								internalctos send_info;
								// command id 4 indicates block command
								send_info.commandid = 4;
								// ip to be blocked
								strcpy(send_info.ip, blockee_ip.c_str());
								// send_info.msg = arguments[1];

								// send this object to server
								send(server_socket, &send_info, sizeof(send_info), 0);

								// receive status msg
								char status_msg[20];
								recv(server_socket, &status_msg, sizeof(status_msg), 0);

								if (strcmp(status_msg, "success") == 0)
								{
									// block successfully executed
									// add this ip to the list of blocked IPS
									c_blocked_ips.push_back(blockee_ip);
									cse4589_print_and_log("[BLOCK:SUCCESS]\n");
									cse4589_print_and_log("[BLOCK:END]\n");
								}
								else
								{
									cse4589_print_and_log("[BLOCK:ERROR]\n");
									cse4589_print_and_log("[BLOCK:END]\n");
								}
							}
							// invalid ip
							else
							{
								cse4589_print_and_log("[BLOCK:ERROR]\n");
								cse4589_print_and_log("[BLOCK:END]\n");
							}
							fflush(stdout);
						}
						else if (commandstring.substr(0, 8) == "UNBLOCK " && this_client_logged_in)
						{
							string unblockee_ip = commandstring.substr(8, commandstring.size());

							// handling exceptions
							//  if the given address is valid IP string and is in the list of logged in clients and is currently blocked
							if (ValidateIPaddress(unblockee_ip) && IPCurrentlyInClientList(unblockee_ip) && IPCurrentlyBlockedByClient(unblockee_ip))
							{
								// ip valid

								// construct internal object
								internalctos send_info;
								// command id 5 indicates unblock command
								send_info.commandid = 5;
								strcpy(send_info.ip, unblockee_ip.c_str());

								// send this object to server
								send(server_socket, &send_info, sizeof(send_info), 0);
								// receive status msg
								char status_msg[20];
								recv(server_socket, &status_msg, sizeof(status_msg), 0);

								if (strcmp(status_msg, "success") == 0)
								{
									// unblock successfully executed
									// so remove this from the client maintained block list
									std::vector<string>::iterator itr = std::find(c_blocked_ips.begin(), c_blocked_ips.end(), unblockee_ip);
									if (itr != c_blocked_ips.end())
									{
										c_blocked_ips.erase(itr);
									}
									cse4589_print_and_log("[UNBLOCK:SUCCESS]\n");
									cse4589_print_and_log("[UNBLOCK:END]\n");
								}
								else
								{
									cse4589_print_and_log("[UNBLOCK:ERROR]\n");
									cse4589_print_and_log("[UNBLOCK:END]\n");
								}
							}
							// invalid ip
							else
							{
								cse4589_print_and_log("[UNBLOCK:ERROR]\n");
								cse4589_print_and_log("[UNBLOCK:END]\n");
							}
							fflush(stdout);
						}
						else if (commandstring == "LOGOUT")
						{
							// construct internal object
							internalctos send_info;
							// command id 6 indicates logout command
							send_info.commandid = 6;

							// send this object to server
							if (send(server_socket, &send_info, sizeof(send_info), 0) == sizeof(send_info))
							{
								
								cse4589_print_and_log("[LOGOUT:SUCCESS]\n");
								server_socket = close(server_socket);
								this_client_logged_in = false;
								cse4589_print_and_log("[LOGOUT:END]\n");
								// return ;
							}
							else
							{
								cse4589_print_and_log("[LOGOUT:ERROR]\n");
								cse4589_print_and_log("[LOGOUT:END]\n");
							}
							// fflush(stdout);
						}
						else if (commandstring == "EXIT")
						{
							//  have to logout from the server and exit from the program
							// to logout first send logout command again

							// construct internal object
							internalctos send_info;
							// command id 6 indicates logout command
							send_info.commandid = 6;

							// send this object to server
							send(server_socket, &send_info, sizeof(send_info), 0) == sizeof(send_info);

							cse4589_print_and_log("[LOGOUT:SUCCESS]\n");
							close(server_socket);
							exit(0);
							cse4589_print_and_log("[LOGOUT:END]\n");
							
						}
					}

					// /* Reading from server */
					else
					{
						// server relayed a message
						// EVENT RECEIVED
						internalstoc server_msg;
						
						if (recv(server_socket, &server_msg, sizeof(server_msg), 0) >= 0 && this_client_logged_in)
						{
							if (strcmp(server_msg.message_type, "unicast-message") == 0 || strcmp(server_msg.message_type, "broadcast-message") == 0)
							{
								cse4589_print_and_log("[RECEIVED:SUCCESS]\n");
								cse4589_print_and_log("msg from:%s\n[msg]:%s\n", server_msg.from_ip, server_msg.message_text);
								cse4589_print_and_log("[RECEIVED:END]\n");
							}
						}

						fflush(stdout);
					}
				}
			}
		}
	}
}

void HandleIPCommand()
{

	string destserver = "8.8.8.8";
	int destport = 53;

	struct sockaddr_in server_addr, hostname;
	char hostnamebuffer[256];
	socklen_t lenhostname = sizeof(hostname);

	bzero((char *)&server_addr, sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(destserver.c_str());
	server_addr.sin_port = htons(destport);

	int server_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (server_socket < 0)
	{
		cerr << "Cannot create socket";
		cse4589_print_and_log("[%s:ERROR]\n", "IP");
		return;
	}

	connect(server_socket, (const struct sockaddr *)&server_addr, sizeof(server_addr));

	getsockname(server_socket, (struct sockaddr *)&hostname, &lenhostname);

	const char *resultbuf = inet_ntop(AF_INET, &hostname.sin_addr, hostnamebuffer, sizeof(hostnamebuffer));

	if (resultbuf == NULL)
	{
		cse4589_print_and_log("[%s:ERROR]\n", "IP");
	}
	else
	{
		cse4589_print_and_log("[%s:SUCCESS]\n", "IP");
		cse4589_print_and_log("IP:%s\n", hostnamebuffer);
	}
}

void HandlePORTCommand(int port)
{
	cse4589_print_and_log("[%s:SUCCESS]\n", "PORT");
	cse4589_print_and_log("PORT:%d\n", port);
	cse4589_print_and_log("[PORT:END]\n");
}

void HandleBlockedCommand(string ipstring)
{
	if (!(ValidateIPaddress(ipstring) && IPCurrentlyInClientList(ipstring)))
	{
		
		cse4589_print_and_log("[BLOCKED:ERROR]\n");
		return;
	}
	// find this client in the list of hosts

	host *current = hostlisthead;

	while (current != NULL)
	{
		if (strcmp(current->ip_addr, ipstring.c_str()) == 0)
		{
			break;
		}
		current = current->next_host;
	}

	// get its blocked list

	string blocked_list = string(current->blocked);
	//  sort this list by port and print it (to sort go in the order of LL)
	cse4589_print_and_log("[BLOCKED:SUCCESS]\n");
	current = hostlisthead;
	int list_id = 1;
	while (current != NULL)
	{
		
		if (!blocked_list.empty() && (blocked_list.find(string(current->ip_addr)) != std::string::npos))
		{
			cse4589_print_and_log("%-5d%-35s%-20s%-8d\n", list_id, current->hostname, current->ip_addr, current->port_num);
			list_id++;
		}
		current = current->next_host;
	}
}

bool ValidateIPaddress(string ip)
{
	struct sockaddr_in sa;
	return inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr)) != 0;
}

vector<string> string_split(const string str)
{
	vector<string> result;
	std::istringstream iss(str);
	for (std::string s; iss >> s;)
	{
		result.push_back(s);
		
	}
	return result;
}

bool ValidatePortNumber(string portstring)
{
	for (int c = 0; c < strlen(portstring.c_str()); c++)
	{
		if (isdigit(portstring[c]) == 0)
			return false;
	}

	if (atoi(portstring.c_str()) < 0 || atoi(portstring.c_str()) > 65535)
	{
		return false;
	}

	return true;
}

int connect_to_host(string server_ip, int server_port)
{
	 int fdsocket;

	struct sockaddr_in  server_addr;


	bzero((char *)&server_addr, sizeof(server_addr));

	server_addr.sin_family = AF_INET;

	server_addr.sin_port = htons(server_port);
	inet_pton(AF_INET, server_ip.c_str(), &(server_addr.sin_addr));

	fdsocket = socket(AF_INET, SOCK_STREAM, 0);
	if (fdsocket < 0)
	{
		cerr << "Cannot create socket";
		exit(-1);
	}

	if (connect(fdsocket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		cerr << "connect failed";
		exit(-1);
	}

	// // if (listen(fdsocket, BACKLOG) < 0)
	// // {
	// // 	cerr << "Unable to listen on port";
	// // 	exit(-1);
	// }

	return fdsocket;
}

void InsertIntoHostLL(host *element)
{

	host **header = &hostlisthead;
	host *current;
	if (*header == NULL || (*header)->port_num > element->port_num)
	{
		host *temp = hostlisthead;
		hostlisthead = NULL;
		element->next_host = temp;
		hostlisthead = element;
	}
	else
	{
		current = *header;
		while (current->next_host != NULL && current->next_host->port_num < element->port_num)
		{
			current = current->next_host;
		}
		element->next_host = current->next_host;
		current->next_host = element;
	}

}

bool IPCurrentlyInClientList(string ip)
{
	host *current = hostlisthead;
	bool found = false;
	if (current == NULL)
	{
		return found;
	}
	while (current != NULL)
	{
		if (strcmp(current->ip_addr, ip.c_str()) == 0)
		{
			found = true;
			break;
		}
		current = current->next_host;
	}
	return found;
}

int GetCurrentlyLoggedinList(struct listitem *array, int clientfd)
{

	if (hostlisthead == NULL)
	{
		// return retlist;
		return 0;
	}
	int i = 0;
	host *current = hostlisthead;
	while (current != NULL)
	{

		if (current->is_logged_in)
		{

			strcpy(array[i].hostname, current->hostname);
			strcpy(array[i].ip, current->ip_addr);
			array[i].port = current->port_num;
			i++;
		}
		current = current->next_host;
	}
	// return retlist;
	return i;
}

void OldClientLoginServerSide(string client_ip, int client_socket)
{
	// an old client issued login command
	// send the currently logged in list
	// also send any queued messages that the client might have

	internalstoc payload;
	payload.clientcount = GetCurrentlyLoggedinList(payload.clientlist, client_socket);
	payload.queue_size = 0;
	// find this host from the list of hosts and get its queued msgs
	host *current = hostlisthead;
	while (current != NULL)
	{
		if (current->ip_addr == client_ip)
		{
			// vector<message> q;

			if (current->queue_size != 0)
			{
				
				for (int i = 0; i < current->queue_size; i++)
				{
					payload.pending_msgs[i] = current->queued_messages[i];
				}
				payload.queue_size = current->queue_size;

				// increase the num_recv count for this one after queue is sent back?
			}
			// payload.pending_msgs = q;
			// also set it to logged in
			current->is_logged_in = true;

			break;
		}
		current = current->next_host;
	}

	// send the constructed object to client
	if (send(client_socket, &payload, sizeof(payload), 0) <= 0)
	{
		cout << "error at server side";
	};
}



void HandleListCommand()
{
	
	if (hostlisthead != NULL)
	{
		host *current = hostlisthead;
		int list_id = 1;
		while (current != NULL)
		{
			if(current->is_logged_in){
			cse4589_print_and_log("%-5d%-35s%-20s%-8d\n", list_id, current->hostname, current->ip_addr, current->port_num);
			
			
			list_id++;
			}
			current = current->next_host;
		}
	}
}

listitem MapHostObjectToListItemObject(host input)
{
	listitem output;
	strcpy(output.hostname, input.hostname);
	strcpy(output.ip, input.ip_addr);
	output.port = input.port_num;
	return output;
}

host *MapListItemObjectToHost(listitem input)
{
	struct host *output = (struct host *)malloc(sizeof(host));

	// host output;
	strcpy(output->hostname, input.hostname);
	strcpy(output->ip_addr, input.ip);
	output->port_num = input.port;
	output->next_host = NULL;
	return output;
}

void BroadcastCommandServerSide(string text, int client_socket)
{


	// what is the sending client's ip?
	host *current = hostlisthead;
	host *sender;
	while (current != NULL)
	{
		if (current->fd == client_socket)
		{
			break;
		}
		current = current->next_host;
	}
	sender = current;
	string from_ip = current->ip_addr;

	// iterate through all the clients (except the sender)
	// send message to each client if its not blocked and logged in
	// if blocked do nothing
	// if not logged in quueue it
	current = hostlisthead;
	bool bcast_failed = false;
	string bcast_to_ip = "255.255.255.255";
	while (current != NULL)
	{
		if (strcmp(from_ip.c_str(), current->ip_addr) != 0)
		{
			// go ahead only if not blocked
			if (string(current->blocked).find(string(from_ip)) == std::string::npos)
			{
				// not blocked
				// now check if logged in
				if (current->is_logged_in)
				{
					internalstoc message;
					strcpy(message.message_type, "broadcast-message");
					strcpy(message.message_text, text.c_str());
					strcpy(message.from_ip, sender->ip_addr);

					if (send(current->fd, &message, sizeof(message), 0) == sizeof(message))
					{

						

						current->num_msg_rcv += 1;
					}
					else
					{
						bcast_failed = true;
					}
				}

				// receiver logged out - store it to send when receiver logs in
				else
				{
					message new_message;
					strcpy(new_message.from_ip, from_ip.c_str());
					new_message.is_broadcast = true;
					strcpy(new_message.text, text.c_str());

					// insert in msg queue
					current->queued_messages[current->queue_size] = new_message;
					current->queue_size += 1;

					
				}
			}
		}

		current = current->next_host;
	}

	// loop ended
	if (!bcast_failed)
	{
		sender->num_msg_sent += 1;
		cse4589_print_and_log("[RELAYED:SUCCESS]\n");
		cse4589_print_and_log("msg from:%s, to:%s\n[msg]:%s\n", from_ip.c_str(), bcast_to_ip.c_str(), text.c_str());
		cse4589_print_and_log("[RELAYED:END]\n");
		// send status reply msg to the client
		char status_reply_to_client[20] = "bcast_complete";
		send(client_socket, status_reply_to_client, sizeof(status_reply_to_client), 0);
	}
	else
	{
		char status_reply_to_client[20] = "bcast_failed";
		send(client_socket, status_reply_to_client, sizeof(status_reply_to_client), 0);
	}
}

bool IPCurrentlyBlockedByClient(string ip)
{
	return (std::find(c_blocked_ips.begin(), c_blocked_ips.end(), ip) != c_blocked_ips.end());
}

// search for blocker in host list
// add the blockee ip in the corresponding host's blockedlist
// return a status message command back to the client
void BlockingServerSide(string blockee_ip, int client_socket, bool block)
{
	// what is the client's ip?
	host *current = hostlisthead;
	host *client;
	// bool failed = false;
	while (current != NULL)
	{
		if (current->fd == client_socket)
		{

			break;
		}
		current = current->next_host;
	}
	client = current;
	if (client == NULL)
	{
		char status_reply_to_client[20] = "failed";
		send(client_socket, status_reply_to_client, sizeof(status_reply_to_client), 0);
		return;
	}
	string from_ip = current->ip_addr;

	// we found the clients ip and host object

	// to block this ip
	if (block)
	{
		// insert this blockee ip into the clients block list
		
		string b = string(client->blocked);
		
		if (b == "")
		{
			b.append(blockee_ip);
		}
		else
		{
			b.append(" ");
			b += blockee_ip;
		}
		strcpy(client->blocked, b.c_str());
	}

	// to unblock this ip
	else
	{
		// remove the unblockee ip from the clients block list
		
		string b = string(client->blocked);
		if (!b.empty())
		{
			std::string::size_type i = b.find(blockee_ip);

			if (i != std::string::npos)
			{
				// first in the list
				if (i == 0)
				{
					// only element in the list
					if (b.length() == blockee_ip.length())
					{
						b = "";
					}
					// there are other elements after the first element
					// remove next space
					else
					{
						b.erase(i, blockee_ip.length() + 1);
					}
				}
				// not first in the list, so remove the element and the space before it
				else
				{
					b.erase(i - 1, blockee_ip.length() + 1);
				}
			}
			strcpy(client->blocked, b.c_str());
		}
		//  client->blocked.remove(blockee_ip);
	}
	char status_reply_to_client[20] = "success";
	send(client_socket, status_reply_to_client, sizeof(status_reply_to_client), 0);
}

void HandleStatisticsCommand()
{
	if (hostlisthead != NULL)
	{
		host *current = hostlisthead;
		int list_id = 1;
		while (current != NULL)
		{
			cse4589_print_and_log("%-5d%-35s%-8d%-8d%-8s\n", list_id, current->hostname, current->num_msg_sent, current->num_msg_rcv, current->status);
			// cout<<current->port_num;
			current = current->next_host;
			list_id++;
		}
	}
}




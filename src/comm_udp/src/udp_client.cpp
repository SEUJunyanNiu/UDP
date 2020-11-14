#include <ros/ros.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <comm_udp/Roadstate.h>
#include <comm_udp/RoadstateArray.h>
#include <time.h>


using namespace std;

#define SERVER_PORT 51234
#define SERVER_IP_ADDRESS  "59.110.71 .188"
#define CLIENT_IP_ADDRESS  "192.168.11.128"
#define CLIENT_PORT 2020
#define TIME 3
#define MAXLEN 1024

int read_timeout(int fd,unsigned int wait_seconds);
void request(int sock_fd,char* send_buf,struct sockaddr_in addr_serv,char* recv_buf,int len,int recv_num);
void get_time(char* send_buf);


/* 重复发送消息直到收到回信 */
void request(int sock_fd,char* send_buf,struct sockaddr_in addr_serv,char* recv_buf,int len,int recv_num)
{
	get_time(send_buf);
	int send_num = sendto(sock_fd, send_buf, strlen(send_buf), 0, (struct sockaddr *)&addr_serv, len);
	
	if(send_num >= 0)
	{
		printf("client send: %s\n", send_buf);
		memset(send_buf, 0, sizeof(send_buf));
		strcat(send_buf,"FC");
	}
	int ret=read_timeout(sock_fd,TIME);	
	if(ret==0)
	{
		recv_num=recvfrom(sock_fd, recv_buf, MAXLEN, 0, (struct sockaddr *)&addr_serv, (socklen_t *)&len);
		recv_buf[recv_num] = '\0';
	}
	else
	{ 
		if(ret==-1&&errno==ETIMEDOUT)
		{
			request(sock_fd,send_buf,addr_serv,recv_buf,len,recv_num);
		}
		else
		{
			cout<<"read_timeout error";
		}
	}
}

/* 判断是否超时 */
int read_timeout(int fd,unsigned int wait_seconds)
{
	int ret=0;
	if(wait_seconds>0)
	{
		struct timeval timeout;
		fd_set read_fdset;

		FD_ZERO(&read_fdset);
		FD_SET(fd,&read_fdset);

		timeout.tv_sec=wait_seconds;
		timeout.tv_usec=0;

		do
		{
			ret=select(fd+1,&read_fdset,NULL,NULL,&timeout);
		}
		while(ret<0 && errno==EINTR);

		if(ret==0)
		{
			ret=-1;
			errno=ETIMEDOUT;
		}
		else if(ret==1)
		ret=0;
	}
	return ret;
}

/* 获取本地时间 */
void get_time(char* send_buf)
{
	time_t t = time(0);
	char currenttime[15];
	strftime(currenttime, sizeof(currenttime), "%Y%m%d%H%M%S", localtime(&t)); //年-月-日 时-分-秒
	//printf("%s",currenttime);
	strcat(send_buf,currenttime);
	send_buf[16]=0x20;
	send_buf[17]=0x20;
	strcat(send_buf,"IDID");
	send_buf[22]=0x20;
	send_buf[23]=0x20;
	send_buf[24]='\0';
	//printf("%s",send_buf);

}


int main (int argc, char** argv)
{
	ros::init(argc, argv, "udp_client");
	ros::NodeHandle nh;
	comm_udp::Roadstate road_state_;
	comm_udp::RoadstateArray road_states_;

	
	int num;//路的数量
	char send_buf[25] = "FC";
	char recv_buf[MAXLEN];

	ros::Publisher pub_roadstate=nh.advertise<comm_udp::RoadstateArray>("roadstate_info",1);
	ros::Rate loop_rate(1.0);




	/* 建立udp socket */
	int sock_fd;//socket文件描述符
	if((sock_fd = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("socket");
		exit(1);
	}

	/* 设置Client address */
	struct sockaddr_in  Client_addr;
	memset(&Client_addr,0,sizeof(Client_addr));
	Client_addr .sin_family  = AF_INET; //使用IPv4协议
	Client_addr .sin_port	= htons(CLIENT_PORT);   //
	Client_addr .sin_addr.s_addr = htonl(INADDR_ANY);//
	

	/* 设置地址可复用 */
	int udp_opt = 1;
	setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &udp_opt, sizeof(udp_opt));

	/* 绑定Client address */ 
	int ret;
	if((ret = bind(sock_fd,(struct sockaddr*)&Client_addr ,sizeof(Client_addr))) < 0)
	{
		perror("bind fail:");
		close(sock_fd);
		return -1;
	}


	/* 设置server address */
	struct sockaddr_in addr_serv;
	int len;
	int recv_num;
	memset(&addr_serv, 0, sizeof(addr_serv));
	addr_serv.sin_addr.s_addr = inet_addr(SERVER_IP_ADDRESS);
	addr_serv.sin_port = htons(SERVER_PORT);
	addr_serv.sin_family = AF_INET;
	len = sizeof(addr_serv);

	while(ros::ok())
	{

		request(sock_fd,send_buf,addr_serv,recv_buf,len,recv_num);
		//判断报文的准确性
		if(recv_buf[0]=='F' && recv_buf[1]=='C' && recv_buf[16]=='$')
		{
			num=(int)recv_buf[17];
			road_states_.Roadstates.reserve(100);
			for(int i=0;i<num;i++)
			{
				road_state_.id=recv_buf[18+i*10];
				road_state_.frontstate=recv_buf[18+i*10+8];
				road_state_.rearstate=recv_buf[18+i*10+9];
				road_states_.Roadstates.push_back(road_state_);		
			}

			pub_roadstate.publish(road_states_);
			loop_rate.sleep();
			printf("pharse success \n");
			//ROS_INFO("roadstate:ID=%d,frontstate=%c,rearstate=%c",road_states_.Roadstates.id,road_states_.Roadstates.frontstate,road_states_.Roadstates.rearstate);

		}
		printf("client receive: %s\n", recv_buf);
		memset(recv_buf, 0, sizeof(recv_buf));
		recv_num=0;
	}
	  
	close(sock_fd);

	return 0;
}

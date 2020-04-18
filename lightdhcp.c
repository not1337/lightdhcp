/*
 * This file is part of the lightdhcp project
 *
 * (C) 2020 Andreas Steinmetz, ast@domdv.de
 * The contents of this file is licensed under the GPL version 2 or, at
 * your choice, any later version of this license.
 */

#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/signalfd.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>

#define SOCKET		"/run/dhcpcd/sock"
#define DHCPCD		"/sbin/dhcpcd"
#define RESOLVCONF	"/etc/resolv.conf"
#define CHRONYC		"/usr/bin/chronyc"
#define INTERFACE	"eth0"

#define IFSTATE	0
#define CARRIER	1
#define IPV4DNS	2
#define IPV6DNS	3
#define DNSSDNS	4
#define IPV4NTP	5
#define IPV6NTP	6
#define SEARCH4	7
#define SEARCH6	8
#define SEARCHD	9

struct config
{
	int state;
	char v4dns[20];
	char v4ntp[20];
	char v6dns[48];
	char v6ntp[48];
	char radns[48];
	char srch4[64];
	char srch6[64];
	char srchr[64];
	char resopts[128];
	char currdns[512];
	char newdns[512];
	char ntpopts[128];
	char ntpv4[20];
	char ntpv6[48];
	char currntp[512];
	char newntp[512];
};

static const struct
{
	char *name;
	int type;
} dhcpopts[]=
{
	{"if_up",IFSTATE},
	{"ifcarrier",CARRIER},
	{"nd1_rdnss1_servers",DNSSDNS},
	{"nd1_dnssl1_search",SEARCHD},
	{"new_dhcp6_name_servers",IPV6DNS},
	{"new_dhcp6_domain_search",SEARCH6},
	{"new_dhcp6_sntp_servers",IPV6NTP},
	{"new_domain_name_servers",IPV4DNS},
	{"new_domain_name",SEARCH4},
	{"new_ntp_servers",IPV4NTP},
	{NULL,-1},
};

static int startdhcp(char *dhcp,char *dev)
{
	int pid;

	switch((pid=fork()))
	{
	case -1:return -1;
	case 0:	execl(dhcp,dhcp,"-q","-B","-A","-z",dev,NULL);
		exit(1);
	default:return pid;
	}
}

static int doconn(char *sock)
{
	int s;
	int i;
	struct sockaddr_un a;

	memset(&a,0,sizeof(a));
	a.sun_family=AF_UNIX;
	strncpy(a.sun_path,sock,sizeof(a.sun_path)-1);

	if((s=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0))==-1)
		return -1;

	for(i=0;i<40;i++)
	{
		usleep(50000);
		if(!connect(s,(struct sockaddr *)&a,sizeof(a)))break;
	}
	if(i==40)
	{
		close(s);
		return -1;
	}

	if(write(s,"--listen\n",9)!=9)
	{
		close(s);
		return -1;
	}

	if(fcntl(s,F_SETFL,O_NONBLOCK))
	{
		close(s);
		return -1;
	}

	return s;
}

static int update_resolvconf(char *resc,struct config *cfg)
{
	int l;
	int fd;
	char *ptr;

	ptr=cfg->newdns;

	if((cfg->state&3)!=3)
	{
		*ptr=0;
		goto done;
	}

	if(cfg->srch4[0])ptr+=sprintf(ptr,"domain %s\n",cfg->srch4);
	else if(cfg->srch6[0])ptr+=sprintf(ptr,"domain %s\n",cfg->srch6);
	else if(cfg->srchr[0])ptr+=sprintf(ptr,"domain %s\n",cfg->srchr);

	if(strcmp(cfg->srch4,cfg->srch6)&&cfg->srch4[0]&&cfg->srch6[0])
		goto search;
	if(strcmp(cfg->srch4,cfg->srchr)&&cfg->srch4[0]&&cfg->srchr[0])
		goto search;
	if(strcmp(cfg->srch6,cfg->srchr)&&cfg->srch6[0]&&cfg->srchr[0])
	{
search:		ptr+=sprintf(ptr,"search");
		if(cfg->srch4[0])ptr+=sprintf(ptr," %s",cfg->srch4);
		if(cfg->srch6[0])
		{
			if(!cfg->srch4[0])
				ptr+=sprintf(ptr," %s",cfg->srch6);
			else if(strcmp(cfg->srch4,cfg->srch6))
				ptr+=sprintf(ptr," %s",cfg->srch6);
		}
		if(cfg->srchr[0])
		{
			if(!cfg->srch4[0]&&!cfg->srch6[0])
				ptr+=sprintf(ptr," %s",cfg->srchr);
			else if(!cfg->srch6[0])
			{
				if(strcmp(cfg->srch4,cfg->srchr))
					ptr+=sprintf(ptr," %s",cfg->srchr);
			}
			else if(!cfg->srch4[0])
			{
				if(strcmp(cfg->srch6,cfg->srchr))
					ptr+=sprintf(ptr," %s",cfg->srchr);
			}
			else if(strcmp(cfg->srch4,cfg->srchr)&&
				strcmp(cfg->srch6,cfg->srchr))
					ptr+=sprintf(ptr," %s",cfg->srchr);
		}
		ptr+=sprintf(ptr,"\n");
	}
	if(cfg->v4dns[0])ptr+=sprintf(ptr,"nameserver %s\n",cfg->v4dns);
	if(cfg->v6dns[0])ptr+=sprintf(ptr,"nameserver %s\n",cfg->v6dns);
	if(cfg->radns[0])
	{
		if(!cfg->v6dns[0])
			ptr+=sprintf(ptr,"nameserver %s\n",cfg->radns);
		else if(strcmp(cfg->v6dns,cfg->radns))
			ptr+=sprintf(ptr,"nameserver %s\n",cfg->radns);
	}

	if(cfg->resopts[0])sprintf(ptr,"%s\n",cfg->resopts);

done:	if(!strcmp(cfg->currdns,cfg->newdns))return 0;

	strcpy(cfg->currdns,cfg->newdns);
	l=strlen(cfg->currdns);

	if((fd=open(resc,O_WRONLY|O_TRUNC|O_CREAT|O_CLOEXEC,0644))!=-1)
	{
		if(write(fd,cfg->currdns,l)!=l)
		{
			close(fd);
			return -1;
		}
		close(fd);
		return 0;
	}
	else return -1;
}

static int update_ntpfile(char *fn,struct config *cfg)
{
	int fd;
	int l;

	if(cfg->v4ntp[0]&&cfg->v6ntp[0])
		l=sprintf(cfg->newntp,"%s\n%s\n",cfg->v4ntp,cfg->v6ntp);
	else if(cfg->v4ntp[0]&&!cfg->v6ntp[0])
		l=sprintf(cfg->newntp,"%s\n",cfg->v4ntp);
	else if(!cfg->v4ntp[0]&&cfg->v6ntp[0])
		l=sprintf(cfg->newntp,"%s\n",cfg->v6ntp);
	else
	{
		cfg->newntp[0]=0;
		l=0;
	}

	if(!strcmp(cfg->currntp,cfg->newntp))return 0;

	strcpy(cfg->currntp,cfg->newntp);

	if((fd=open(fn,O_WRONLY|O_TRUNC|O_CREAT|O_CLOEXEC,0644))!=-1)
	{
		if(write(fd,cfg->currntp,l)!=l)
		{
			close(fd);
			return -1;
		}
		close(fd);
		return 0;
	}
	else return -1;
}

static int extract(int s,struct config *cfg)
{
	int i;
	int j;
	int k;
	int l;
	char *ptr;
	char *name;
	char *value;
	char bfr[8192];

	if((l=read(s,bfr,sizeof(bfr)))<=0)return -1;

	for(i=0;i<l;i=j+1)
	{
		for(j=i;j<l&&bfr[j];j++);
		if(!bfr[j])
		{
			name=bfr+i;
			if((value=strchr(name,'=')))*value++=0;
			else continue;

			for(k=0;dhcpopts[k].name;k++)
				if(!strcmp(name,dhcpopts[k].name))break;
			switch(dhcpopts[k].type)
			{
			case IFSTATE:
				if(!strcmp(value,"true"))cfg->state|=2;
				else cfg->state&=~2;
				break;
			case CARRIER:
				if(!strcmp(value,"up")||
					!strcmp(value,"unknown"))
						cfg->state|=1;
				else cfg->state&=~1;
				break;
			case IPV4DNS:
				if((ptr=strchr(value,' ')))*ptr=0;
				strncpy(cfg->v4dns,value,
					sizeof(cfg->v4dns)-1);
				break;
			case IPV6DNS:
				if((ptr=strchr(value,' ')))*ptr=0;
				strncpy(cfg->v6dns,value,
					sizeof(cfg->v6dns)-1);
				break;
			case DNSSDNS:
				if((ptr=strchr(value,' ')))*ptr=0;
				strncpy(cfg->radns,value,
					sizeof(cfg->radns)-1);
				break;
			case IPV4NTP:
				if((ptr=strchr(value,' ')))*ptr=0;
				strncpy(cfg->v4ntp,value,
					sizeof(cfg->v4ntp)-1);
				break;
			case IPV6NTP:
				if((ptr=strchr(value,' ')))*ptr=0;
				strncpy(cfg->v6ntp,value,
					sizeof(cfg->v6ntp)-1);
				break;
			case SEARCH4:
				if((ptr=strchr(value,' ')))*ptr=0;
				strncpy(cfg->srch4,value,
					sizeof(cfg->srch4)-1);
				break;
			case SEARCH6:
				if((ptr=strchr(value,' ')))*ptr=0;
				strncpy(cfg->srch6,value,
					sizeof(cfg->srch6)-1);
				break;
			case SEARCHD:
				if((ptr=strchr(value,' ')))*ptr=0;
				strncpy(cfg->srchr,value,
					sizeof(cfg->srchr)-1);
				break;
			}
		}
	}

	return 0;
}

static int startchronyc(char *pn)
{
	int sv[2];

	if(socketpair(AF_UNIX,SOCK_STREAM,0,sv))return -1;
	switch(fork())
	{
	case -1:close(sv[0]);
		close(sv[1]);
		return -1;
	case 0:	close(sv[0]);
		if(sv[1]!=0)dup2(sv[1],0);
		if(sv[1]!=1)dup2(sv[1],1);
		if(sv[1]!=2)dup2(sv[1],2);
		if(sv[1]>2)close(sv[1]);
		execl(pn,pn,"-c",NULL);
		exit(1);
	default:close(sv[1]);
		break;
	}
	return sv[0];
}

static int adddelntp(int s,char *addr,int mode,struct config *cfg)
{
	int l;
	int r=1;
	int rem;
	char *start;
	char *end;
	char *ptr;
	struct pollfd p;
	char bfr[1024];

	if(!mode)sprintf(bfr,"delete %s\nbla\n",addr);
	else if(!cfg->ntpopts[0])sprintf(bfr,"add server %s\nbla\n",addr);
	else sprintf(bfr,"add server %s %s\nbla\n",addr,cfg->ntpopts);

	l=strlen(bfr);
	if(write(s,bfr,l)!=l)goto err;

	p.fd=s;
	p.events=POLLIN;
	ptr=bfr;
	rem=sizeof(bfr);
	start=bfr;

	while(1)
	{
		if(!rem)goto err;
		if(poll(&p,1,1000)<1)goto err;
		if((l=read(s,ptr,rem))<=0)goto err;
		ptr+=l;
		rem-=l;
repeat:	
		for(end=start;end!=ptr&&*end!='\n';end++);
		if(*end!='\n')continue;
		*end++=0;
		if(!strcmp(start,"506 Cannot talk to daemon"))goto err;
		if(!strcmp(start,"501 Not authorised"))goto err;
		if(!strcmp(start,"Unrecognized command"))break;
		if(strcmp(start,"200 OK"))r=0;
		start=end;
		goto repeat;
	}

	return r;

err:	cfg->state&=~4;
	return -1;
}

static int getchronysrc(int s,struct config *cfg)
{
	int l;
	int rem;
	char *x;
	char *y;
	char *start;
	char *end;
	char *ptr;
	struct pollfd p;
	char bfr[1024];
	char dummy[16];

	if(!(cfg->state&4))
	{
		cfg->ntpv4[0]=0;
		cfg->ntpv6[0]=0;
	}

	if(write(s,"sources\nbla\n",12)!=12)goto err;

	p.fd=s;
	p.events=POLLIN;
	ptr=bfr;
	rem=sizeof(bfr);
	start=bfr;

	while(1)
	{
		if(!rem)goto err;
		if(poll(&p,1,1000)<1)goto err;
		if((l=read(s,ptr,rem))<=0)goto err;
		ptr+=l;
		rem-=l;
repeat:	
		for(end=start;end!=ptr&&*end!='\n';end++);
		if(*end!='\n')continue;
		*end++=0;
		if(!strcmp(start,"506 Cannot talk to daemon"))goto err;
		if(!strcmp(start,"501 Not authorised"))goto err;
		if(!strcmp(start,"Unrecognized command"))break;
		x=start;
		start=end;
		while(*x&&*x!=',')x++;
		if(!*x++)continue;
		while(*x&&*x!=',')x++;
		if(!*x++)continue;
		y=x;
		while(*x&&*x!=',')x++;
		*x=0;
		if(inet_pton(AF_INET,y,dummy)==1)
			strncpy(cfg->ntpv4,y,sizeof(cfg->ntpv4)-1);
		else if(inet_pton(AF_INET6,y,dummy)==1)
			strncpy(cfg->ntpv6,y,sizeof(cfg->ntpv6)-1);
		goto repeat;
	}

	cfg->state|=4;
	return 0;

err:	cfg->state&=~4;
	return -1;
}

static void usage(void)
{
	fprintf(stderr,"Usage lightdhcp [<options>]\n\nOptions:\n\n"
		"-I <device>      use device instead of eth0\n"
		"-t <timeout>     set resolv.conf timeout option\n"
		"-a <attempts>    set resolv.conf attempts option\n"
		"-s               set single-request resolv.conf option\n"
		"-r               set rotate resolv.conf option\n"
		"-e               set edns0 resolv.conf option\n"
		"-m <minpoll>     set minpoll time server option\n"
		"-M <maxpoll>     set manpoll time server option\n"
		"-i               set iburst time server option\n"
		"-p               set prefer time server option\n"
		"-C <chronyc>     use chronyc instead of /usr/bin/chronyc\n"
		"-D <dhcpcd>      use dhcpcd instead of /sbin/dhcpcd\n"
		"-S <socket>      use socket instead of /run/dhcpcd.sock\n"
		"-R <resolvconf>  use resolvconf insted of /etc/resolv.conf\n"
		"-n               do not modify resolvconf\n"
		"-N               do not modify chrony\n"
		"-F <ntpfile>     save ntp servers in ntpfile\n"
		"-d               become daemon\n");
	exit(1);
}

int main(int argc,char *argv[])
{
	int fd;
	int err=1;
	int s;
	int pid;
	union
	{
		struct
		{
			unsigned int tmo:5;
			unsigned int att:3;
			unsigned int sng:1;
			unsigned int rot:1;
			unsigned int ed0:1;
			unsigned int dmn:1;
			unsigned int ibs:1;
			unsigned int prf:1;
			unsigned int nrc:1;
			unsigned int nch:1;
			unsigned int mip:4;
			unsigned int map:4;
		};
		unsigned int bits;
	} u;
	char *dhcp=DHCPCD;
	char *chro=CHRONYC;
	char *sock=SOCKET;
	char *resc=RESOLVCONF;
	char *dev=INTERFACE;
	char *ntp=NULL;
	char *ptr;
	struct pollfd p[2];
	struct config cfg;
	sigset_t set;

	u.bits=0;

	while((s=getopt(argc,argv,"t:a:srem:M:ipC:D:S:R:dnNI:F:"))!=-1)switch(s)
	{
	case 't':
		if((fd=atoi(optarg))<1||fd>30)usage();
		u.tmo=fd;
		break;
	case 'a':
		if((fd=atoi(optarg))<1||fd>5)usage();
		u.att=fd;
		break;
	case 's':
		u.sng=1;
		break;
	case 'r':
		u.rot=1;
		break;
	case 'e':
		u.ed0=1;
		break;
	case 'm':
		if((fd=atoi(optarg))<1||fd>24)usage();
		u.mip=fd;
		break;
	case 'M':
		if((fd=atoi(optarg))<1||fd>24)usage();
		u.map=fd;
		break;
	case 'i':
		u.ibs=1;
		break;
	case 'p':
		u.prf=1;
		break;
	case 'C':
		chro=optarg;
		break;
	case 'D':
		dhcp=optarg;
		break;
	case 'S':
		sock=optarg;
		break;
	case 'R':
		resc=optarg;
		break;
	case 'd':
		u.dmn=1;
		break;
	case 'n':
		u.nrc=1;
		break;
	case 'N':
		u.nch=1;
		break;
	case 'I':
		dev=optarg;
		break;
	case 'F':
		ntp=optarg;
		break;
	default:usage();
	}

	if(u.mip>u.map)usage();

	memset(&cfg,0,sizeof(cfg));

	ptr=cfg.resopts;
	if(u.tmo)
	{
		if(ptr==cfg.resopts)
			ptr+=sprintf(ptr,"options timeout:%d",u.tmo);
		else ptr+=sprintf(ptr," timeout:%d",u.tmo);
	}
	if(u.tmo)
	{
		if(ptr==cfg.resopts)
			ptr+=sprintf(ptr,"options attempts:%d",u.att);
		else ptr+=sprintf(ptr," attempts:%d",u.att);
	}
	if(u.sng)
	{
		if(ptr==cfg.resopts)ptr+=sprintf(ptr,"options single-request");
		else ptr+=sprintf(ptr," single-request");
	}
	if(u.rot)
	{
		if(ptr==cfg.resopts)ptr+=sprintf(ptr,"options rotate");
		else ptr+=sprintf(ptr," rotate");
	}
	if(u.ed0)
	{
		if(ptr==cfg.resopts)ptr+=sprintf(ptr,"options edns0");
		else ptr+=sprintf(ptr," edns0");
	}

	ptr=cfg.ntpopts;
	if(u.mip)
	{
		if(ptr==cfg.ntpopts)ptr+=sprintf(ptr,"minpoll %d",u.mip);
		else ptr+=sprintf(ptr," minpoll %d",u.mip);
	}
	if(u.map)
	{
		if(ptr==cfg.ntpopts)ptr+=sprintf(ptr,"maxpoll %d",u.map);
		else ptr+=sprintf(ptr," maxpoll %d",u.map);
	}
	if(u.ibs)
	{
		if(ptr==cfg.ntpopts)ptr+=sprintf(ptr,"iburst");
		else ptr+=sprintf(ptr," iburst");
	}
	if(u.prf)
	{
		if(ptr==cfg.ntpopts)ptr+=sprintf(ptr,"prefer");
		else ptr+=sprintf(ptr," prefer");
	}

	sigfillset(&set);

	if(u.dmn)if(daemon(0,0))
	{
		perror("daemon");
		goto err1;
	}

	if((fd=open(resc,O_RDONLY|O_CLOEXEC))!=-1)
	{
		s=read(fd,cfg.currdns,sizeof(cfg.currdns)-1);
		close(fd);
	}

	if((pid=startdhcp(dhcp,dev))==-1)
	{
		fprintf(stderr,"can't start dhcpcd\n");
		goto err1;
	}

	sigprocmask(SIG_BLOCK,&set,NULL);

	if(!u.nch)s=startchronyc(chro);
	else s=-1;

	if((p[0].fd=doconn(sock))==-1)
	{
		fprintf(stderr,"can't connect to dhcpcd\n");
		goto err2;
	}
	p[0].events=POLLIN;

	sigemptyset(&set);
	sigaddset(&set,SIGINT);
	sigaddset(&set,SIGHUP);
	sigaddset(&set,SIGTERM);
	sigaddset(&set,SIGQUIT);

	if((p[1].fd=signalfd(-1,&set,SFD_NONBLOCK|SFD_CLOEXEC))==-1)
	{
		perror("signalfd");
		goto err3;
	}

	p[1].events=POLLIN;

	while(1)
	{
		if(poll(p,2,(u.nch||(cfg.state&4))?-1:1000)<1)
			if(u.nch||(cfg.state&4))continue;
		if(p[1].revents&POLLIN)break;
		if(!u.nch&&s==-1)s=startchronyc(chro);
		if(p[0].revents&POLLIN)
		{
			if(waitpid(pid,NULL,WNOHANG)>0)
			{
				fprintf(stderr,"dhcpcd died\n");
				pid=-1;
				break;
			}
			if(extract(p[0].fd,&cfg))
			{
				fprintf(stderr,"dhcpcd communication error\n");
				break;
			}
			if(!u.nrc)if(update_resolvconf(resc,&cfg))
			{
				fprintf(stderr,"cant update %s\n",resc);
				break;
			}
			if(ntp)if(update_ntpfile(ntp,&cfg))
			{
				fprintf(stderr,"cant update %s\n",ntp);
				break;
			}
		}
		if(u.nch||s==-1)continue;
		if(!(cfg.state&4))if(getchronysrc(s,&cfg))
		{
again:			close(s);
			s=-1;
			waitpid(-1,NULL,0);
			continue;
		}
		if(!(cfg.state&4))continue;
		if(strcmp(cfg.ntpv4,cfg.v4ntp))
		{
			if(!cfg.ntpv4[0])switch(adddelntp(s,cfg.v4ntp,1,&cfg))
			{
			case -1:goto again;
			case 1:	strcpy(cfg.ntpv4,cfg.v4ntp);
				break;
			}
			else if(!cfg.v4ntp[0])
				switch(adddelntp(s,cfg.ntpv4,0,&cfg))
			{
			case -1:goto again;
			case 1:	cfg.ntpv4[0]=0;
				break;
			}
			else
			{
				switch(adddelntp(s,cfg.ntpv4,0,&cfg))
				{
				case -1:goto again;
				case 1:	cfg.ntpv4[0]=0;
					switch(adddelntp(s,cfg.v4ntp,1,&cfg))
					{
					case -1:goto again;
					case 1:	strcpy(cfg.ntpv4,cfg.v4ntp);
						break;
					}
					break;
				}
			}
		}
		if(strcmp(cfg.ntpv6,cfg.v6ntp))
		{
			if(!cfg.ntpv6[0])switch(adddelntp(s,cfg.v6ntp,1,&cfg))
			{
			case -1:goto again;
			case 1:	strcpy(cfg.ntpv6,cfg.v6ntp);
				break;
			}
			else if(!cfg.v6ntp[0])
				switch(adddelntp(s,cfg.ntpv6,0,&cfg))
			{
			case -1:goto again;
			case 1:	cfg.ntpv6[0]=0;
				break;
			}
			else
			{
				switch(adddelntp(s,cfg.ntpv6,0,&cfg))
				{
				case -1:goto again;
				case 1:	cfg.ntpv6[0]=0;
					switch(adddelntp(s,cfg.v6ntp,1,&cfg))
					{
					case -1:goto again;
					case 1:	strcpy(cfg.ntpv6,cfg.v6ntp);
						break;
					}
					break;
				}
			}
		}
	}

	printf("\n");
	err=0;
	close(p[1].fd);
err3:	close(p[0].fd);
err2:	if(s!=-1)
	{
		close(s);
		waitpid(-1,NULL,0);
	}
	if(pid!=-1)
	{
		kill(pid,SIGTERM);
		waitpid(pid,NULL,0);
	}
err1:	return err;
}

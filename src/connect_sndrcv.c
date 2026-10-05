#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

/*
	Settings and defaults
*/

#define PC_CONNECT_BUFFER_SIZE 256
char g_serial_port[128]="dev/ttyS0";
int g_baud_rate=B115200;
char g_dir_root[512]="machikap/";

int g_baud_settings[]={
	B0,
	B50,
	B75,
	B110,
	B134,
	B150,
	B200,
	B300,
	B600,
	B1200,
	B1800,
	B2400,
	B4800,
	B9600,
	B19200,
	B38400,
	B57600,
	B115200,
	B230400,
};
char* g_baud_strings[]={
	"0",
	"50",
	"75",
	"110",
	"134",
	"150",
	"200",
	"300",
	"600",
	"1200",
	"1800",
	"2400",
	"4800",
	"9600",
	"19200",
	"38400",
	"57600",
	"115200",
	"230400",
};

/*
	The other global variables
*/

int g_serial_handle;
char g_curdir[512]="/";
int g_file_size;

/* Receive buffer: never discard bytes that arrive together with a command. */
#define RX_BUFFER_SIZE 8192
unsigned char g_rx_buffer[RX_BUFFER_SIZE];
int g_rx_length=0;

/*
	Prototypings
*/

void wait4command(const char* command);
void receive_machikania_output(void);

void communication_error(void){
	printf("\nAn error occured in PC during the commnucation\n");
	printf("Restart the application and reset MachiKania\n");
	while(1) usleep(1000000);
}

void listfiles(char *path,void* callback){
	DIR *dir;
	struct dirent *dp;
	char path2[256];
	void(*f)(char* path)=callback;
	dir = opendir(path);
	while((dp = readdir(dir)) != NULL){
		strcpy(path2,path);
		strcat(path2,dp->d_name);
		switch(dp->d_type){
			case DT_DIR:
				if ('.'==dp->d_name[0]) break;
				strcat(path2,"/");
				listfiles(path2,callback);
				break;
			case DT_REG:
			default:
				f(path2);
				break;
		}
	}
	closedir(dir);
}

void listfile_callback(char* path){
	printf("%s\n", path);
}

void listusb_callback(char* path){
	int i;
	const char* const avoid_list[]={
		"/dev/fd","/dev/stderr","/dev/stdin","/dev/stdout","/dev/clipboard","/dev/conin","/dev/conout","/dev/cons0",
		"/dev/console","/dev/dsp","/dev/full","/dev/null","/dev/ptmx","/dev/random","/dev/urandom","/dev/windows",
		"/dev/zero","/dev/sd","/dev/sr","/dev/dvd","/dev/snd","/dev/vhost","/dev/cpu","/dev/log","/dev/cdrom",
		"/dev/i2c","/dev/dri","/dev/disk","/dev/block","/dev/char","/dev/vcs","/dev/input","/dev/bus","/dev/loop",
		"/dev/cdrw","/dev/pts","/dev/."
	};
	for(i=0;i<(sizeof avoid_list/sizeof avoid_list[0]);i++){
		if (!strncmp(path,avoid_list[i],strlen(avoid_list[i]))) return;
	}
	if (!strncmp(path,"/dev/ttyACM",11)) printf("\x1b[45m%s\x1b[49m ", path);
	else if (!strncmp(path,"/dev/ttyUSB",11)) printf("\x1b[45m%s\x1b[49m ", path);
	else if (!strncmp(path,"/dev/ttyS",9)) printf("\x1b[45m%s\x1b[49m ", path);
	else printf("%s ", path);
}

void send_cd(char* path){
	int i;
	char c;
	char command[17]="CD:\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08";
	if ('/'==path[0] && 0==path[1]) {
		command[3]='/';
		command[4]=0;
	} else {
		for(i=0;i<13;i++){
			c=path[i];
			if ('a'<=c && c<='z') c-=0x20;
			else if ('/'==c || 0==c) break;
			command[3+i]=c;
		}
		command[3+i]=0;
	}
	write(g_serial_handle,command,16);
	wait4command("OK\x08\x08OK\x08\x08OK\x08\x08OK\x08\x08");
}

void send_size(int size){
	char command[17]="SIZE:\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08";
	g_file_size=size;
	snprintf(command+5,10,"%d",size);
	write(g_serial_handle,command,16);
	wait4command("OK\x08\x08OK\x08\x08OK\x08\x08OK\x08\x08");
}

void send_cp(char* path, char* pcpath){
	FILE* fh;
	int i;
	char c;
	char command[17]="CP:\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08";
	for(i=0;i<13;i++){
		c=path[i];
		if ('a'<=c && c<='z') c-=0x20;
		else if (0==c) break;
		command[3+i]=c;
	}
	command[3+i]=0;
	write(g_serial_handle,command,16);
	wait4command("SENDFILE\x08\x08\x08\x08\x08\x08\x08\x08");
	// Open the file
	fh=fopen(pcpath,"r");
	if (!fh) {
		printf("\nCannot open file: %s",path);
		communication_error();
	}
	// Send the file
	for(i=0;i<g_file_size;i++){
		c=fgetc(fh);
		command[i&15]=c;
		if ((i&15)==15) write(g_serial_handle,command,16);
		else if (i==g_file_size-1) write(g_serial_handle,command,g_file_size&15);
		if ((PC_CONNECT_BUFFER_SIZE-1)==((PC_CONNECT_BUFFER_SIZE-1)&i)) {
			wait4command("OK\x08\x08OK\x08\x08OK\x08\x08OK\x08\x08");
		}
	}
	// All done
	fclose(fh);
	wait4command("DONEDONE\x08\x08\x08\x08\x08\x08\x08\x08");
}

void copyfile_callback(char* path){
	int dirlen,i,j;
	char* mpath;
	struct stat filestat;
	// Convert to MachiKania path
	mpath=path+strlen(g_dir_root)-1;
	// Determine the length of the directory path
	for(dirlen=strlen(mpath);1<=dirlen;dirlen--){
		if ('/'==mpath[dirlen-1]) break;
	}
	while (strlen(g_curdir)!=dirlen || strncmp(g_curdir,mpath,dirlen)) {
		if (strlen(g_curdir)<dirlen) {
			if (!strncmp(g_curdir,mpath,strlen(g_curdir))) {
				// New directory is in the current directory
				i=j=strlen(g_curdir);
				memcpy(g_curdir,mpath,dirlen);
				while('/'!=g_curdir[j++]);
				g_curdir[j]=0;
				//printf("chdir %s\n", g_curdir+i);
				send_cd(g_curdir+i);	
			} else {
				// Let's go to root
				g_curdir[0]='/';
				g_curdir[1]=0;
				//printf("chdir /\n");
				send_cd("/");
			}
		} else if (strlen(g_curdir)>dirlen) {
			if (!strncmp(g_curdir,mpath,dirlen)) {
				// Current directory is in the new directory
				// Let's go to parent directory
				for(i=strlen(g_curdir)-1;'/'!=g_curdir[i-1];i--);
				g_curdir[i]=0;
				//printf("chdir ..\n");
				send_cd("..");
			} else {
				// Let's go to root
				g_curdir[0]='/';
				g_curdir[1]=0;
				//printf("chdir /\n");
				send_cd("/");
			}
		} else {
			// Let's go to root
			g_curdir[0]='/';
			g_curdir[1]=0;
			//printf("chdir /\n");
			send_cd("/");
		}
		printf("dir:  %s\n", g_curdir);	
	}
	//printf("copy %s\n", mpath+dirlen);
	if (stat(path,&filestat)) communication_error();
	send_size(filestat.st_size);
	send_cp(mpath+dirlen,path);
}

int open_serial(void){
	struct termios tio;
	int fd=-1;
	time_t t=time(NULL);
	int n;
	char devname[64];

	/*
		Try the port specified in connect.ini first.
		After a USB reconnect Linux may assign ttyACM1, ttyACM2, ...
		instead of the original ttyACM0, so also try ttyACM0..ttyACM9.
	*/
	for(n=0;n<10;n++){
		snprintf(devname,sizeof(devname),"/dev/ttyACM%d",n);
		fd=open(devname,O_RDWR | O_NOCTTY | O_NONBLOCK);
		if(fd>=0) break;
	}

	while(fd<0 && time(NULL)-t<3){
		fd=open(g_serial_port,O_RDWR | O_NOCTTY | O_NONBLOCK);
		if(fd>=0) break;
		for(n=0;n<10;n++){
			snprintf(devname,sizeof(devname),"/dev/ttyACM%d",n);
			fd=open(devname,O_RDWR | O_NOCTTY | O_NONBLOCK);
			if(fd>=0) break;
		}
		if(fd<0) usleep(10000);
	}

	if(fd<0) return -1;

	/* Initialization */
	memset(&tio,0,sizeof(tio));
	tio.c_cflag = CS8 | CLOCAL | CREAD;
	tio.c_cc[VTIME] = 100;
	tio.c_cc[VMIN] = 0;
	cfsetispeed(&tio,g_baud_rate);
	cfsetospeed(&tio,g_baud_rate);
	if(tcsetattr(fd,TCSANOW,&tio)<0){
		close(fd);
		return -1;
	}

	return fd;
}

void wait4command(const char* command){
	int i;
	int fd=g_serial_handle;
	struct pollfd pfd;

	while(1){
		/* First check bytes already received by an earlier read(). */
		for(i=0;i<=g_rx_length-16;i++){
			if(!memcmp(g_rx_buffer+i,command,16)){
				/* Consume only the command. Keep following bytes. */
				g_rx_length-=i+16;
				if(g_rx_length>0)
					memmove(g_rx_buffer,g_rx_buffer+i+16,g_rx_length);
				g_serial_handle=fd;
				return;
			}
		}

		pfd.fd=fd;
		pfd.events=POLLIN;
		pfd.revents=0;
		i=poll(&pfd,1,100);
		if(i<0){
			if(errno==EINTR) continue;
			if(0<=fd) close(fd);
			fd=-1;
		}

		if(i>0){
			if(pfd.revents & (POLLERR|POLLHUP|POLLNVAL)){
				printf("\nUSB serial disconnected. Waiting for MachiKania...\n");
				fflush(stdout);
				if(0<=fd) close(fd);
				fd=-1;
			}
			else if(pfd.revents & POLLIN){
				unsigned char buf[1024];
				int r=read(fd,buf,sizeof(buf));
				if(r>0){
					if(g_rx_length+r>RX_BUFFER_SIZE){
						/* Keep the newest data if the buffer ever becomes full. */
						int drop=(g_rx_length+r)-RX_BUFFER_SIZE;
						if(drop<g_rx_length){
							memmove(g_rx_buffer,g_rx_buffer+drop,g_rx_length-drop);
							g_rx_length-=drop;
						}else{
							g_rx_length=0;
						}
					}
					if(r<=RX_BUFFER_SIZE-g_rx_length){
						memcpy(g_rx_buffer+g_rx_length,buf,r);
						g_rx_length+=r;
					}
				}
				else if(r<0 && errno!=EINTR && errno!=EAGAIN && errno!=EWOULDBLOCK){
					if(0<=fd) close(fd);
					fd=-1;
				}
			}
		}

		if(fd<0){
			while(fd<0){
				fd=open_serial();
				if(fd>=0){
					printf("Serial port reconnected (fd=%d).\n",fd);
					fflush(stdout);
					g_serial_handle=fd;
					break;
				}
				usleep(100000);
			}
		}
	}
}

/*
	After the BASIC program has been transferred and MachiKania has
	finished compiling/running it, MachiKania sends its console/LCD
	text back through the USB serial connection.

	This function displays only the received text on the Brainux terminal.
	It does not display the pcconnect protocol used before this point.
*/
void wait_for_next_machikania(void){
	int fd=g_serial_handle;
	struct pollfd pfd[2];
	struct termios old_tio;
	struct termios key_tio;
	int keyboard_mode=0;
	unsigned char command_buf[16];
	int command_len=0;
	const char machikap[16]="MACHIKAP\x08\x08\x08\x08\x08\x08\x08\x08";

	/*
		After a successful transfer, keep the program alive and wait for
		the next MachiKania reset.  At this point MachiKania may send
		console text, so display it, but never lose the MACHIKAP request.
		A key press also returns to the normal reset-wait state.
	*/
	if(isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO,&old_tio)==0){
		key_tio=old_tio;
		key_tio.c_lflag &= ~(ICANON | ECHO);
		key_tio.c_cc[VMIN]=0;
		key_tio.c_cc[VTIME]=0;
		if(tcsetattr(STDIN_FILENO,TCSANOW,&key_tio)==0)
			keyboard_mode=1;
	}

	/*
		The file transfer is complete.  Start displaying MachiKania's
		console output immediately.  The message below is only a guide;
		it must NOT pause reception.  While the output is being displayed,
		pressing any Brainux key returns to the next MachiKania reset wait.
	*/
	printf("\n--- MachiKania output ---  Hit any key to continue.\n");
	fflush(stdout);

	/*
		Do not leave old data from the previous transaction in the
		command buffer.  Data already in g_rx_buffer is displayed below.
	*/
	command_len=0;

	while(1){
		pfd[0].fd=fd;
		pfd[0].events=POLLIN;
		pfd[0].revents=0;
		pfd[1].fd=keyboard_mode ? STDIN_FILENO : -1;
		pfd[1].events=POLLIN;
		pfd[1].revents=0;

		if(g_rx_length>0){
			int k;
			for(k=0;k<g_rx_length;k++){
				unsigned char b=g_rx_buffer[k];
				if(command_len<16) command_buf[command_len++]=b;
				else {
					memmove(command_buf,command_buf+1,15);
					command_buf[15]=b;
				}
				if(command_len==16 && !memcmp(command_buf,machikap,16)){
					g_rx_length=0;
					if(keyboard_mode) tcsetattr(STDIN_FILENO,TCSANOW,&old_tio);
					g_serial_handle=fd;
					printf("\nMachiKania reset detected.\n");
					fflush(stdout);
					return;
				}
				putchar(b);
			}
			fflush(stdout);
			g_rx_length=0;
		}

		int pr=poll(pfd,2,100);
		if(pr<0){
			if(errno==EINTR) continue;
			if(0<=fd) close(fd);
			fd=-1;
		}

		if(keyboard_mode && (pfd[1].revents & POLLIN)){
			char keybuf[32];
			read(STDIN_FILENO,keybuf,sizeof(keybuf));
			command_len=0;
			tcsetattr(STDIN_FILENO,TCSANOW,&old_tio);
			printf("\nKey pressed. Waiting for the next MachiKania reset...\n");
			fflush(stdout);
			g_serial_handle=fd;
			return;
		}

		if(pr>0 && (pfd[0].revents & (POLLERR|POLLHUP|POLLNVAL))){
			if(0<=fd) close(fd);
			fd=-1;
			command_len=0;
			printf("\nUSB serial disconnected. Waiting for MachiKania...\n");
			fflush(stdout);
		}
		else if(pr>0 && (pfd[0].revents & POLLIN)){
			unsigned char buf[1024];
			int r=read(fd,buf,sizeof(buf));
			if(r>0){
				int k;
				for(k=0;k<r;k++){
					unsigned char b=buf[k];
					if(command_len<16) command_buf[command_len++]=b;
					else {
						memmove(command_buf,command_buf+1,15);
						command_buf[15]=b;
					}
					if(command_len==16 && !memcmp(command_buf,machikap,16)){
						command_len=0;
						if(k+1<r){
							int remain=r-(k+1);
							if(remain>RX_BUFFER_SIZE) remain=RX_BUFFER_SIZE;
							memcpy(g_rx_buffer,buf+k+1,remain);
							g_rx_length=remain;
						}else{
							g_rx_length=0;
						}
						if(keyboard_mode) tcsetattr(STDIN_FILENO,TCSANOW,&old_tio);
						g_serial_handle=fd;
						printf("\nMachiKania reset detected.\n");
						fflush(stdout);
						return;
					}
					putchar(b);
				}
				fflush(stdout);
			}
			else if(r<0 && errno!=EINTR && errno!=EAGAIN && errno!=EWOULDBLOCK){
				if(0<=fd) close(fd);
				fd=-1;
				command_len=0;
			}
		}

		if(fd<0){
			while(fd<0){
				fd=open_serial();
				if(fd>=0){
					g_serial_handle=fd;
					printf("Serial port reconnected (fd=%d).\n",fd);
					fflush(stdout);
					break;
				}
				usleep(100000);
			}
		}
	}
}

void serialtest(void){
	int fd;
	
	printf("open serial port...");
	fd = open_serial();
	while (fd<0) {
		printf("\nSerial port is not ready. Waiting for MachiKania...\n");
		fflush(stdout);
		usleep(500000);
		fd = open_serial();
	}
	printf("opened as %d\n",fd);
	g_serial_handle=fd;

	while(1){
		// This is the main loop
		printf("Waiting for request...\n");
		wait4command("MACHIKAP\x08\x08\x08\x08\x08\x08\x08\x08");
		fd=g_serial_handle;
		printf("Request detected!\n");
		write(fd,"OK\x08\x08OK\x08\x08OK\x08\x08OK\x08\x08",16);
		printf("Waiting for SENDCMD...");
		wait4command("SENDCMDS\x08\x08\x08\x08\x08\x08\x08\x08");
		fd=g_serial_handle;
		printf("OK      \n");
		listfiles(g_dir_root,copyfile_callback);
		write(fd,"DONEDONE\x08\x08\x08\x08\x08\x08\x08\x08",16);
		wait4command("ALL DONE\x08\x08\x08\x08\x08\x08\x08\x08");
		printf("All done!\n");

		/*
			The file transfer is now complete.
			From this point on, display only text received from MachiKania.
		*/
		wait_for_next_machikania();
 	}
}

void open_ini(void){
	int i;
	FILE* fh;
	char buff[128];
	char* line;
	printf("Opening INI file...");
	fh=fopen("./connect.ini","r");
	if (!fh) {
		printf("not found\n");
		return;
	}
	printf("found\n");
	while((line=fgets(buff,128,fh))){
		//printf("%s",line);
		if (!strncmp(line,"SERIALPORT=",11)) {
			for(i=0;i<sizeof g_serial_port;i++){
				if (line[i+11]<=0x20) {
					g_serial_port[i]=0;
					break;
				}
				g_serial_port[i]=line[i+11];
			}
			g_serial_port[sizeof g_serial_port-1]=0;
			printf("Serial port: %s\n",g_serial_port);
		} else if (!strncmp(line,"BAUD=",5)) {
			for(i=(sizeof g_baud_strings/sizeof g_baud_strings[0])-1;0<=i;i--){
				if (strncmp(line+5,g_baud_strings[i],strlen(g_baud_strings[i]))) continue;
				printf("Baud: %s\n",g_baud_strings[i]);
				break;	
			}
		} else if (!strncmp(line,"ROOT=",5)) {
			for(i=0;i<sizeof g_dir_root;i++){
				if (line[i+5]<=0x20) {
					g_dir_root[i]=0;
					break;
				}
				g_dir_root[i]=line[i+5];
			}
			g_dir_root[sizeof g_dir_root-1]=0;
			printf("Transfer files in : %s\n",g_dir_root);
		}
	}
	fclose(fh);
}

int main(void){
	open_ini();
	printf("\nTransfer following files:\n");
	listfiles(g_dir_root,listfile_callback);
	printf("\n");
	serialtest();
	printf("\nPress ctrl+C to quit\n");
	while(1) usleep(1000000);
	return 0;
}

/*
	Connection sequence (P: PC; M: MachiKania):
	
	M: "MACHIKAP\x08\x08\x08\x08\x08\x08\x08\x08"
	P: "OK\x08\x08OK\x08\x08OK\x08\x08OK\x08\x08"
	M: "SENDCMDS\x08\x08\x08\x08\x08\x08\x08\x08"
	
	P: "CD:DIRNAME\x00\x08\x08\x08\x08\x08"
	M: "OK\x08\x08OK\x08\x08OK\x08\x08OK\x08\x08" or
	   "NG\x08\x08NG\x08\x08NG\x08\x08NG\x08\x08"
	
	P: "CP:FILENAME.BAS\x00"
	M: "OK\x08\x08OK\x08\x08OK\x08\x08OK\x08\x08"
	P: "SIZE:1048576\x08\x08\x08"
	M: "SENDFILE\x08\x08\x08\x08\x08\x08\x08\x08"
	P: Send 256 bytes
	M: "OK\x08\x08OK\x08\x08OK\x08\x08OK\x08\x08"
	P: Send 256 bytes
	M: "OK\x08\x08OK\x08\x08OK\x08\x08OK\x08\x08"
	...
	P: Send 256 bytes
	M: "OK\x08\x08OK\x08\x08OK\x08\x08OK\x08\x08"
	P: Send last bytes
	M: "DONEDONE\x08\x08\x08\x08\x08\x08\x08\x08"
	
	P: "DONEDONE\x08\x08\x08\x08\x08\x08\x08\x08"
	M: "ALL DONE\x08\x08\x08\x08\x08\x08\x08\x08"
*/

#include<unistd.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<stdio.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<linux/ioctl.h>
#include<string.h>
#include<signal.h>

#define MLBUF_IOC_MAGIC  222
#define MLBUF_IOCCLR   _IO(MLBUF_IOC_MAGIC, 0)
#define MLBUF_IOCHSIZE _IOR(MLBUF_IOC_MAGIC,  1, int)

#define MLBUF_IOC_MAXNR 1

/* global data filled with random data in main() */
unsigned char bufs[16][512]; 

void handler(int a) {
}

int test01(char *dev) {
	int fd, nw,nr, ret = 0;
	char bufs[10] = "ababababa";
	char buft[10] = "a--------";

	fd = open(dev, O_WRONLY);
	if (fd < 0) {
		perror("test01 write");
		return 0;
	}
	nw = write(fd, bufs, 10);

	close(fd);
	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		perror("test01 read");
		return 0;
	}
	nr = read(fd, buft, 10);
	close(fd);

	if (strncmp(bufs, buft, 10) == 0) {
		if (nr == 10 && nw == 10) 
			ret = 10;
		else
			ret = 8;
	} 
	fprintf(stderr, "%s: %.10s (%d) vs %.10s (%d) = %d\n",__func__,
			bufs, nw, buft, nr, ret);
	return ret;
}

int test02(char *dev) {
	int fd, nw,nr, ret = 0;
	char bufs[][10] = {"cagsdfsdf","d5fsdfhsc"};
	char buft[][10] = {"c","d"};

	fd = open(dev, O_WRONLY);
	if (fd < 0) {
		perror("test02 write");
		return 0;
	}
	nw = write(fd, bufs[0], 10);
	nw += write(fd, bufs[1], 10);

	close(fd);
	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		perror("test02 read");
		return 0;
	}
	nr = read(fd, buft[1], 10);
	nr += read(fd, buft[0], 10);
	close(fd);

	if (strncmp(bufs[0], buft[0], 10) == 0 && strncmp(bufs[1], buft[1], 10) == 0) {
		if (nr == 20 && nw == 20) 
			ret = 10;
		else
			ret = 8;
	} 
	fprintf(stderr, "%s: %.10s %.10s vs %.10s %.10s = %d\n", __func__,
			bufs[0], bufs[1], buft[0], buft[1], ret);
	return ret;
}

int test03(char *dev) {
	int fd, nw,nr, ret = 0;
	char *bufs[]={"abr","acd","abr","ar"};
	char buft[12] = "a-----------";
	char bufr[] = "abracdabrar";
	int i;

	fd = open(dev, O_WRONLY);
	if (fd < 0) {
		perror("test03 write");
		return 0;
	}
	for (i = 0; i < sizeof(bufs)/sizeof(bufs[0]); i++) {
		nw += write(fd, bufs[i], 3);
	}

	close(fd);
	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		perror("test03 read");
		return 0;
	}
	nr = read(fd, buft, 12);
	close(fd);

	if (strncmp(buft, bufr, 12) == 0) 
		ret = 10;
	else
		ret = 0;
	fprintf(stderr, "%s: %.12s vs %.12s = %d\n", __func__,
		buft, bufr, ret);
	return ret;
}

int test04(char *dev) {
	int fd, nw = 0,nr = 0, ret = 0;
	char bufs[]="abracadabra";
	char buft[15];
	int i;

	fd = open(dev, O_WRONLY);
	if (fd < 0) {
		perror("test04 write");
		return 0;
	}

	nw += write(fd, bufs, 12);

	close(fd);
	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		perror("test04 read");
		return 0;
	}
	for (i = 0; i < 12; i += 3) {
		buft[i] = 'a';
		nr += read(fd, buft+i, 3);
	}
	close(fd);

	if (strncmp(buft, bufs, 12) == 0) 
		ret = 10;
	else
		ret = 0;
	fprintf(stderr, "%s: %.12s vs %.12s = %d\n", __func__,
		bufs, buft, ret);
	return ret;
}

int test05(char *dev) {
	int fd, nw = 0,nr = 0, ret = 0;
	unsigned char buft[16][512];
	int i, j;


	/* use global random data */
	for (i = 0; i < 16; i++) 
		bufs[i][0] = bufs[i][256] = (i << 4) + 0xf;

	fd = open(dev, O_WRONLY);
	if (fd < 0) {
		perror("test05 write");
		return 0;
	}

	for (i = 0; i < 16; i++) {
		nw += write(fd, bufs[i], 512);
	}

	close(fd);

	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		perror("test05 read");
		return 0;
	}
	for (i = 15; i >= 0; i--) {
		int n;
		buft[i][0] = (i << 4) + 0xf;
		n = read(fd, &buft[i], 256);
		if (n < 0)
			perror("test05 read");
		else
			nr += n;

		buft[i][256] = (i << 4) + 0xf;
		n = read(fd, &(buft[i][256]), 256);
		if (n < 0)
			perror("test05 read");
		else
			nr += n;
	}
	close(fd);

	j = 0;
	for (i = 0; i < 16; i++) {
		if (strncmp(buft[i], bufs[i], 512) == 0)  {
			j++;
		}
	}
	ret = (j*10+9)/16;
	fprintf(stderr, "%s: %d/%d = %d\n", __func__,
		j, 16, ret);
	return ret;
}

int test06(char *dev) {
	int fd, nw,nr, ret = 0;
	char buft[10] = "a--------";
	int cid;
	struct sigaction sa = { 
		.sa_handler = handler,
		.sa_flags = 0};
	
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);

	bufs[0][0] = 'a';

	if ( (cid = fork())) {
		alarm(2); /* kill me in 2 seconds */
		fd = open(dev, O_RDONLY);
		if (fd < 0) {
			perror("test06 read");
			return 0;
		}
		nr = read(fd, buft, 10);
		alarm(0);
		close(fd);
		wait(&nw);
		nw = (WIFEXITED(nw))?WEXITSTATUS(nw):0;
	} else {
		usleep(200000);
		fd = open(dev, O_WRONLY);
		if (fd < 0) {
			perror("test06 write");
			return 0;
		}
		nw = write(fd, bufs[0], 10);
		close(fd);
		exit(nw);
	}

	if (strncmp(bufs[0], buft, 10) == 0) {
		if (nr == 10 && nw == 10) 
			ret = 10;
		else
			ret = 8;
	} 
	fprintf(stderr, "%s: %.10s (%d) vs %.10s (%d) = %d\n",__func__,
			bufs[0], nw, buft, nr, ret);
	return ret;
}

int test07(char *dev) {
	int fd, nw,nr, ret = 0;
	char buft[20] = "b--------";
	int cid;
	struct sigaction sa = { 
		.sa_handler = handler,
		.sa_flags = 0};
	
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);
	bufs[1][0] = 'b';
	bufs[1][10] = 'b';

	if ( (cid = fork()) ) {
		int r;
		alarm(2); /* kill me in 2 seconds */
		fd = open(dev, O_RDONLY);
		if (fd < 0) {
			perror("test07 read");
			return 0;
		}
		nr = read(fd, buft, 20);
		alarm(0);
		close(fd);
		wait(&r);
		nw = (WIFEXITED(r))?WEXITSTATUS(r):0;
	} else {
		usleep(100000);
		fd = open(dev, O_WRONLY);
		if (fd < 0) {
			perror("test06 write");
			return 0;
		}
		nw = write(fd, bufs[1], 10);
		usleep(100000);
		nw += write(fd, &(bufs[1][10]), 10);
		close(fd);
		exit(nw);
	}

	if (strncmp(bufs[1], buft, 20) == 0) {
		if (nr == 20 && nw == 20) 
			ret = 10;
		else
			ret = 8;
	} 
	fprintf(stderr, "%s: %.20s (%d) vs %.20s (%d) = %d\n",__func__,
			bufs[1], nw, buft, nr, ret);
	return ret;
}

int test08(char *dev) {
	int fd, nw,nr, ret = 0;
	char buft[10] = "C--------";
	int cid1, cid2;
	char *sbuf = bufs[2];

	struct sigaction sa = { 
		.sa_handler = handler,
		.sa_flags = 0};
	
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);
	sbuf[0] = 'C';

	if ( (cid1 = fork())) {
		if ((cid2 = fork()) ) {
			int r;
			fd = open(dev, O_RDONLY);
			if (fd < 0) {
				perror("test08 read");
				return 0;
			}
			alarm(2); /* kill me in 2 seconds */
			nr = read(fd, buft, 10);
			alarm(0);
			close(fd);
			wait(&r);
			nw = (WIFEXITED(r))?WEXITSTATUS(r):0;
			wait(&r);
			nw += (WIFEXITED(r))?WEXITSTATUS(r):0;
		} else {
			fd = open(dev, O_RDONLY);
			if (fd < 0) {
				perror("test08 read");
				return 0;
			}
			alarm(2); /* kill me in 2 seconds */
			nr = read(fd, buft, 10);
			alarm(0);
			close(fd);
			exit(nr);
		}
	} else {
		usleep(100000);
		fd = open(dev, O_WRONLY);
		if (fd < 0) {
			perror("test08 write");
			return 0;
		}
		nw = write(fd, sbuf, 20);
		usleep(1000000);
		close(fd);
		exit(nw);
	}

	if (strncmp(sbuf, buft, 10) == 0 || strncmp(sbuf+10, buft, 10) == 0) {
		if (nr == 10 && nw == 30) 
			ret = 10;
		else
			ret = 8;
	} 
	fprintf(stderr, "%s: %.20s (%d) vs %.10s (%d) = %d\n",__func__,
			sbuf, nw, buft, nr, ret);
	return ret;
}

int test09(char *dev) {
	int fd, nw,nr, ret = 0;
	char buft[10] = "---------";
	int cid1, cid2;
	char *sbuf = bufs[3];
	
	sbuf[0] = 'W'; 

	fd = open(dev, O_WRONLY);
	if (fd < 0) {
		perror("test09 write");
		return 0;
	}
	nw = write(fd, sbuf, 10);
	if (nw < 0) {
		perror("test09 write");
		return 0;
	}
	close(fd);


	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		perror("test09 read");
		return 0;
	}
	buft[0] = '\0'; /* ZERO READ */

	nr = read(fd, buft, 10);
	close(fd);

	if (strncmp(sbuf, buft, 10) == 0) {
		if (nr == 10 && nw == 10) 
			ret = 10;
		else
			ret = 8;
	} 
	fprintf(stderr, "%s: %.10s (%d) vs %.10s (%d) = %d\n",__func__,
			sbuf, nw, buft, nr, ret);
	return ret;
}

int test10(char *dev) {
	int i, fd, nw,nr, ret = 0;
	char buft[30] = "-----------";
	int cid1, cid2;
	char *sbuf[] = {bufs[4], bufs[5], bufs[6]} ;
	
	fd = open(dev, O_WRONLY);
	if (fd < 0) {
		perror("test10 write");
		return 0;
	}
	nw = 0;
	for (i = 2; i >= 0 ; i--) {
		sbuf[i][0] = 64+i*10;
		nw += write(fd, sbuf[i], 10);
	}
	close(fd);


	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		perror("test10 read");
		return 0;
	}
	buft[0] = '\0'; /* ZERO READ */
	nr = read(fd, buft, 30);
	close(fd);

	ret = 0;
	for (i = 0; i < 3; i++) {
		if (strncmp(sbuf[i], buft+i*10, 10) == 0) 
	       		ret += 3;
	}
	if (nr == 30 && nw == 30) 
		ret++;
	fprintf(stderr, "%s: %.10s (%d) vs %.10s (%d) = %d\n",__func__,
			sbuf[0], nw, buft, nr, ret);
	return ret;
}

int (*testlist[])(char *) = {
	test01,
	test02,
	test03,
	test04,
	test05,
	test06,
	test07,
	test08,
	test09,
	test10,
};


int main(int argc, char *argv[]) {
	int i,j;
	int sum = 0;
	int fd;

	srand(536);
	fd = open("/dev/urandom", O_RDONLY);
	for (i = 0; i < 16; i++) {
		read(fd, bufs[i], 512);
		for (j = 0; j < 512; j++)  {
			bufs[i][j] = 32 + (bufs[i][j] % 92); /* printable ascii */
		}
	}
	close(fd);
	
	for (i = 0; i < sizeof(testlist)/sizeof(testlist[0]); i++)
		sum += testlist[i]("/dev/mlbuf0");

	printf("SUM: %d\n", sum);
	return 0;
	
}

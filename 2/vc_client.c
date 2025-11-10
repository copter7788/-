/*
 * ตัวอย่างโปรแกรมไคลเอนต์ (vc_client.c)
 * ทำงานแบบ TCP: เชื่อมต่อไปยังเซิร์ฟเวอร์และร้องขอไฟล์ตามชื่อที่ผู้ใช้ระบุ
 * โครงสร้างหลัก:
 *  - main(): รับชื่อโฮสต์, แก้ชื่อเป็น IP, สร้างการเชื่อมต่อ และเรียกฟังก์ชันรับไฟล์
 *  - setup_vcclient(): สร้างซ็อกเก็ตและ connect ไปยังเซิร์ฟเวอร์
 *  - receive_file(): ส่งชื่อไฟล์ให้เซิร์ฟเวอร์และแสดงผลเนื้อหาไฟล์ที่ได้รับ
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h> /* ส่วนติดต่อซ็อกเก็ตพื้นฐาน (socket, connect, send, recv) */
#include <netinet/in.h> /* โครงสร้างและคงที่ของเครือข่ายแบบอินเทอร์เน็ต (struct sockaddr_in, htons ฯลฯ) */
#include <netdb.h>      /* ฟังก์ชันแก้ชื่อโฮสต์ เช่น gethostbyname */
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#define  MAXHOSTNAME    64
#define	 S_TCP_PORT	(u_short)5050
#define  MAXFILENAME	255
#define	 MAXBUFLEN	512  
#define	 ERR		0 /* โค้ดตอบกลับ: เกิดข้อผิดพลาด */
#define  OK		1 /* โค้ดตอบกลับ: สำเร็จ */
int setup_vcclient(struct hostent*, u_short);
static int recv_line(int socd, char *buf, size_t maxlen);
static void do_get_legacy(struct hostent *hostent, u_short port, const char *filename);
static void do_get_multi(struct hostent *hostent, u_short port, const char *list);
static void do_put(struct hostent *hostent, u_short port, const char *filename);
static void trim_trailing(char *s);

int main()
{
	int	socd;
	char	s_hostname[MAXHOSTNAME];
	struct hostent	*s_hostent;

	/* รับชื่อโฮสต์ของเซิร์ฟเวอร์จากผู้ใช้ */
	printf("server host name?: "); 
	if (scanf("%63s", s_hostname) != 1) { fprintf(stderr, "invalid host\n"); exit(1); }

	/* แปลงชื่อโฮสต์ให้เป็นข้อมูล hostent (ใช้หาที่อยู่ IP) */
	if((s_hostent = gethostbyname(s_hostname)) == NULL) {
		fprintf(stderr, "server host does not exist\n");
		exit(1);
	}

	/* consume trailing newline after scanf */
	int c; while ((c = getchar()) != '\n' && c != EOF) {}

	/* วนรับคำสั่งจนกว่าจะกด CTRL-D */
	char line[1024];
	for (;;) {
		printf("> "); fflush(stdout);
		if (!fgets(line, sizeof(line), stdin)) break; /* CTRL-D */
		trim_trailing(line);
		if (line[0] == '\0') continue;

		/* รูปแบบที่รองรับ:
		   - ชื่อไฟล์เดียว        -> legacy GET (เข้ากันได้ของเดิม)
		   - GET:fname1 fname2.. -> multi GET
		   - หลายชื่อคั่นด้วยช่องว่าง -> แปลงเป็น GET:... อัตโนมัติ
		   - PUT:fname           -> อัปโหลดไฟล์ (อ่านจากดิสก์)
		   - PUT fname           -> แปลงเป็น PUT:fname
		*/
		if (strncmp(line, "PUT ", 4) == 0) {
			char *fname = line + 4;
			while (*fname==' ') fname++;
			if (*fname == '\0') { fprintf(stderr, "PUT: missing filename\n"); continue; }
			char cmd[1024]; snprintf(cmd, sizeof(cmd), "PUT:%s", fname);
			do_put(s_hostent, S_TCP_PORT, cmd+4); /* pass filename only */
		} else if (strncmp(line, "PUT:", 4) == 0) {
			do_put(s_hostent, S_TCP_PORT, line+4);
		} else if (strncmp(line, "GET:", 4) == 0) {
			do_get_multi(s_hostent, S_TCP_PORT, line+4);
		} else if (strchr(line, ' ') != NULL) {
			/* หลายไฟล์แต่ไม่ได้ใส่ GET: */
			do_get_multi(s_hostent, S_TCP_PORT, line);
		} else {
			/* ไฟล์เดียว (legacy) */
			do_get_legacy(s_hostent, S_TCP_PORT, line);
		}
	}

	printf("\nbye.\n");
	return 0;
}

int setup_vcclient(struct hostent *hostent, u_short port)
{
        int     socd;
        struct sockaddr_in      s_address;
 
	/* สร้างซ็อกเก็ต TCP (SOCK_STREAM) */
        if((socd = socket(AF_INET, SOCK_STREAM, 0)) < 0) { perror("socket");exit(1); }
 
	/* ตั้งค่าข้อมูลที่อยู่ปลายทาง (IP + PORT) */
        bzero((char *)&s_address, sizeof(s_address));
        s_address.sin_family = AF_INET;
        s_address.sin_port = htons(port);
        bcopy((char *)hostent->h_addr, (char *)&s_address.sin_addr, hostent->h_length);

	/* เชื่อมต่อไปยังเซิร์ฟเวอร์ */
	if(connect(socd, (struct sockaddr *)&s_address, sizeof(s_address)) < 0) { perror("connect");exit(1); }

	return socd;
}

static void trim_trailing(char *s)
{
	size_t n = strlen(s);
	while (n && (s[n-1]=='\n' || s[n-1]=='\r' || s[n-1]==' ' || s[n-1]=='\t')) { s[--n] = '\0'; }
}

static int recv_line(int socd, char *buf, size_t maxlen)
{
	size_t i = 0;
	while (i + 1 < maxlen) {
		char c;
		ssize_t n = recv(socd, &c, 1, 0);
		if (n <= 0) return (int)n;
		buf[i++] = c;
		if (c == '\n') break;
	}
	buf[i] = '\0';
	return (int)i;
}

static void do_get_legacy(struct hostent *hostent, u_short port, const char *filename)
{
	int socd = setup_vcclient(hostent, port);
	/* ส่งชื่อไฟล์ (รวม '\0' เหมือนเดิม) */
	send(socd, filename, (int)strlen(filename)+1, 0);

	char ack;
	if (recv(socd, &ack, 1, 0) <= 0) { perror("recv"); close(socd); return; }
	switch (ack) {
		case OK: {
			printf("Downloading file %s ...\n", filename);
			char buf[MAXBUFLEN];
			int length;
			while ((length = recv(socd, buf, MAXBUFLEN, 0)) > 0) {
				fwrite(buf, 1, (size_t)length, stdout);
			}
			break;
		}
		case ERR:
			fprintf(stderr, "File access error: %s\n", filename);
			break;
		default:
			fprintf(stderr, "Unknown ACK: %d\n", (int)ack);
	}
	close(socd);
}

static void do_get_multi(struct hostent *hostent, u_short port, const char *list)
{
	int socd = setup_vcclient(hostent, port);
	char cmd[2048];
	snprintf(cmd, sizeof(cmd), "GET:%s\n", list);
	send(socd, cmd, (int)strlen(cmd), 0);

	char line[1024];
	for (;;) {
		int n = recv_line(socd, line, sizeof(line));
		if (n <= 0) break; /* หมดข้อมูล */

		if (strncmp(line, "OK ", 3) == 0) {
			/* รูปแบบ: OK <name> <size>\n */
			char name[512]; long size = 0;
			if (sscanf(line+3, "%511s %ld", name, &size) != 2) { fprintf(stderr, "bad header: %s", line); break; }
			printf(">>> %s (%ld bytes)\n", name, size);

			long remaining = size;
			char buf[MAXBUFLEN];
			while (remaining > 0) {
				int chunk = (remaining > MAXBUFLEN) ? MAXBUFLEN : (int)remaining;
				int r = recv(socd, buf, chunk, 0);
				if (r <= 0) { fprintf(stderr, "recv truncated for %s\n", name); remaining = 0; break; }
				fwrite(buf, 1, (size_t)r, stdout);
				remaining -= r;
			}
			/* หลังอ่านครบไฟล์ ให้ขึ้นบรรทัดใหม่แยกไฟล์ (เพื่อความอ่านง่าย) */
			if (size > 0) printf("\n");
		} else if (strncmp(line, "ERR ", 4) == 0) {
			char name[512];
			if (sscanf(line+4, "%511s", name) == 1) {
				fprintf(stderr, "ERR: not found -> %s\n", name);
			} else {
				fprintf(stderr, "ERR (unknown file)\n");
			}
		} else {
			fprintf(stderr, "unknown header: %s", line);
		}
	}
	close(socd);
}

static void do_put(struct hostent *hostent, u_short port, const char *filename)
{
	/* เปิดไฟล์จากดิสก์เพื่อต้องการอัปโหลด */
	FILE *fp = fopen(filename, "rb");
	if (!fp) { perror("open local file"); return; }

	int socd = setup_vcclient(hostent, port);
	/* ส่งคำสั่ง PUT:filename ตามโปรโตคอล */
	char header[1024];
	snprintf(header, sizeof(header), "PUT:%s\n", filename);
	send(socd, header, (int)strlen(header), 0);

	/* ส่งบอดี้ไฟล์ทั้งหมด */
	char buf[MAXBUFLEN];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		if (send(socd, buf, (int)n, 0) < 0) { perror("send"); break; }
	}
	fclose(fp);

	/* แจ้งฝั่งเซิร์ฟเวอร์ว่าจบการส่งข้อมูล */
	shutdown(socd, SHUT_WR);

	/* อ่านผลลัพธ์บรรทัดเดียว: OK/ERR filename */
	char line[1024];
	if (recv_line(socd, line, sizeof(line)) > 0) {
		fputs(line, stdout);
	}
	close(socd);
}